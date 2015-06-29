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

#include <signal.h>
#include <setjmp.h>

extern "C" {
#include <histedit.h>
}

#include <cstring>
#include <string>

#include <ydsh/ydsh.h>
#include "misc/debug.h"

namespace {

static bool continuation = false;

static char *prompt(EditLine *el) {
    return continuation ? (char *) "> " : (char *) "ydsh> ";
}

static sigjmp_buf jmp_ctx;
static volatile sig_atomic_t gotsig = 0;

static void handler(int num) {
    gotsig = num;
    siglongjmp(jmp_ctx, 1);
}

class Terminal {
private:
    EditLine *el;
    History *ydsh_history;
    HistEvent event;

    /**
     * contains previous read line.
     */
    std::string lineBuf;

public:
    explicit Terminal(const char *progName);

    ~Terminal();

    /**
     * not delete return value.
     * return null if reach end of file or occurs error.
     * skip white space and empty string.
     */
    const char *readLine();

private:
    const char *readLineImpl();

    void addHistory();
};

static void setupSignalHandler() {
    // set sigint
    struct sigaction act;
    act.sa_handler = handler;
    act.sa_flags = 0;
    sigfillset(&act.sa_mask);

    if(sigaction(SIGINT, &act, NULL) < 0) {
        fatal("setup signal handeler failed\n");
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

Terminal::Terminal(const char *progName) :
        el(0), ydsh_history(0), event(), lineBuf() {
    setupSignalHandler();

    this->el = el_init(progName, stdin, stdout, stderr);
    el_set(this->el, EL_PROMPT, prompt);
    el_set(this->el, EL_EDITOR, "emacs");
    el_set(this->el, EL_SIGNAL, 1);

    this->ydsh_history = history_init();
    if(this->ydsh_history == 0) {
        fatal("editline history initialization failed\n");
    }

    history(this->ydsh_history, &this->event, H_SETSIZE, 200);
    el_set(el, EL_HIST, history, this->ydsh_history);
}

Terminal::~Terminal() {
    history_end(this->ydsh_history);
    el_end(this->el);
}

static inline bool isSkipLine(const char *line, int count) {
    if(line == 0) {
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

const char *Terminal::readLine() {
    if(sigsetjmp(jmp_ctx, 1) != 0) {
        if(gotsig == SIGINT) {
            printf("\n");
        }
        gotsig = 0;
    }

    this->lineBuf = std::string();
    const char *line;
    do {
        line = this->readLineImpl();
        if(line == 0) {
            return line;
        }
        this->lineBuf += line;
        continuation = true;
    } while(checkLineContinuation(line));

    continuation = false;
    this->addHistory();
    return this->lineBuf.c_str();
}

const char *Terminal::readLineImpl() {
    int count;
    const char *line;

    do {
        line = el_gets(this->el, &count);
    } while(isSkipLine(line, count));
    return line;
}

void Terminal::addHistory() {
    std::string target("\\\n");
    std::string buf(this->lineBuf);
    std::string::size_type pos = buf.find(target);
    while(pos != std::string::npos) {
        buf.replace(pos, target.size(), "");
        pos = buf.find(target, pos);
    }
    history(this->ydsh_history, &this->event, H_ENTER, buf.c_str());
}

} // namespace

void exec_interactive(const char *progName, DSContext *ctx) {
    Terminal term(progName);
    DSContext_setOption(ctx, DS_OPTION_TOPLEVEL);

    unsigned int lineNum = 1;
    const char *line;

    while((line = term.readLine()) != 0) {
        DSContext_setLineNum(ctx, lineNum);
        DSStatus *status;
        int ret = DSContext_eval(ctx, line, &status);
        unsigned int type = DSStatus_getType(status);
        if(type == DS_STATUS_ASSERTION_ERROR || type == DS_STATUS_EXIT) {
            DSStatus_free(&status);
            DSContext_delete(&ctx);
            exit(ret);
        }
        lineNum = DSContext_getLineNum(ctx);
        DSStatus_free(&status);
    }
    printf("\n");
    DSContext_delete(&ctx);
    exit(0);
}

