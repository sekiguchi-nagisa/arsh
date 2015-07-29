/*
 * Copyright (C) 2015 Nagisa Sekiguchi
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

extern "C" {
#include <histedit.h>
}

#include <csetjmp>
#include <csignal>
#include <cstring>
#include <string>

#include <ydsh/ydsh.h>
#include "misc/debug.h"

static DSContext *dsContext;

// for prompt
static bool continuation = false;

static char *prompt(EditLine *el) {
    return (char *)DSContext_getPrompt(dsContext, continuation ? 2 : 1);
}

// for signal handler
static sigjmp_buf jmp_ctx;
static volatile sig_atomic_t gotsig = 0;

static void handler(int num) {
    gotsig = num;
    siglongjmp(jmp_ctx, 1);
}

static void setupSignalHandler() {
    // set sigint
    struct sigaction act;
    act.sa_handler = handler;
    act.sa_flags = 0;
    sigfillset(&act.sa_mask);

    if(sigaction(SIGINT, &act, NULL) < 0) {
        perror("setup signal handeler failed\n");
        exit(1);
    }

    // ignore some signal
    struct sigaction ignore_act;
    ignore_act.sa_handler = SIG_IGN;
    ignore_act.sa_flags = 0;
    sigemptyset(&ignore_act.sa_mask);

    sigaction(SIGQUIT, &ignore_act, NULL);
    sigaction(SIGSTOP, &ignore_act, NULL);  //FIXME: foreground job
    sigaction(SIGCONT, &ignore_act, NULL);
    sigaction(SIGTSTP, &ignore_act, NULL);  //FIXME: background job
}

// for editline
static EditLine *el;
static History *ydsh_history;
static HistEvent event;

// contains previous read line
std::string lineBuf;

static void initEditLine(const char *progName) {
    setupSignalHandler();

    el = el_init(progName, stdin, stdout, stderr);
    el_set(el, EL_PROMPT, prompt);
    el_set(el, EL_EDITOR, "emacs");
    el_set(el, EL_SIGNAL, 1);

    ydsh_history = history_init();
    if(ydsh_history == 0) {
        fatal("editline history initialization failed\n");
    }

    history(ydsh_history, &event, H_SETSIZE, 200);
    el_set(el, EL_HIST, history, ydsh_history);
}

static void endEditLine() {
    history_end(ydsh_history);
    el_end(el);
}


static inline bool isSkipLine(const char *line, int count) {
    if(line == nullptr) {
        return false;
    }
    for(int i = 0; i < count; i++) {
        switch(line[i]) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
            break;
        default:
            return false;
        }
    }
    return true;
}

static bool checkLineContinuation(const char *line) {
    if(line == 0) {
        return false;
    }
    unsigned int size = strlen(line);
    for(unsigned int i = 0; i < size; i++) {
        if(line[i] == '\\' && i + 1 < size && line[i + 1] == '\n') {
            return true;
        }
    }
    return false;
}

static void addHistory() {
    std::string target("\\\n");
    std::string buf(lineBuf);
    std::string::size_type pos = buf.find(target);
    while(pos != std::string::npos) {
        buf.replace(pos, target.size(), "");
        pos = buf.find(target, pos);
    }
    history(ydsh_history, &event, H_ENTER, buf.c_str());
}

static const char *readLineImpl() {
    int count;
    const char *line;

    do {
        line = el_gets(el, &count);
    } while(isSkipLine(line, count));
    return line;
}

static const char *readLine() {
    if(sigsetjmp(jmp_ctx, 1) != 0) {
        if(gotsig == SIGINT) {
            printf("\n");
        }
        gotsig = 0;
    }

    lineBuf = std::string();
    const char *line;
    do {
        line = readLineImpl();
        if(line == 0) {
            return line;
        }
        lineBuf += line;
        continuation = true;
    } while(checkLineContinuation(line));

    continuation = false;
    addHistory();
    return lineBuf.c_str();
}

void exec_interactive(const char *progName, DSContext *ctx) {   // never return
    initEditLine(progName);
    DSContext_setOption(ctx, DS_OPTION_TOPLEVEL);
    dsContext = ctx;

    const char *line;
    int exitStatus = 0;

    for(unsigned int lineNum = 1; (line = readLine()) != 0; lineNum = DSContext_getLineNum(ctx)) {
        DSContext_setLineNum(ctx, lineNum);
        DSStatus *status;
        int ret = DSContext_eval(ctx, line, &status);
        unsigned int type = DSStatus_getType(status);
        DSStatus_free(&status);
        if(type == DS_STATUS_ASSERTION_ERROR || type == DS_STATUS_EXIT) {
            exitStatus = ret;
            goto END;
        }
    }
    printf("\n");

    END:
    DSContext_delete(&ctx);
    endEditLine();
    exit(exitStatus);
}

