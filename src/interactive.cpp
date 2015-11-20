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

#include <clocale>
#include <csetjmp>
#include <csignal>
#include <cstring>
#include <string>
#include <cerrno>
#include <memory>

#include <linenoise.h>
#include <encodings/utf8.h>

#include <ydsh/ydsh.h>
#include "misc/fatal.h"

static DSContext *dsContext;

struct Deleter {
    void operator()(char *ptr) {
        free(ptr);
    }
};

typedef std::unique_ptr<char, Deleter> StrWrapper;

static inline bool isSkipLine(const StrWrapper &line) {
    if(line == nullptr) {
        return false;
    }
    const char *ptr = line.get();
    for(int i = 0; ptr[i] != '\0'; i++) {
        switch(ptr[i]) {
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

static bool checkLineContinuation(const StrWrapper &line) {
    if(line == nullptr) {
        return false;
    }
    const char *ptr = line.get();
    for(unsigned int i = 0; ptr[i] != '\0'; i++) {
        if(ptr[i] == '\\' && ptr[i + 1] == '\0') {
            return true;
        }
    }
    return false;
}

static bool readLine(std::string &line) {
    line.clear();

    bool continuation = false;
    while(true) {
        errno = 0;
        auto str = StrWrapper(linenoise(DSContext_getPrompt(dsContext, continuation ? 2 : 1)));
        if(isSkipLine(str)) {
            continue;
        }

        if(str == nullptr) {
            if(errno == EAGAIN) {
                continue;
            }
            return false;
        }
        line += str.get();
        continuation = checkLineContinuation(str);
        if(continuation) {
            line.pop_back(); // remove '\\'
            continue;
        }
        break;
    }

    linenoiseHistoryAdd(line.c_str());
    line += '\n';    // terminate newline
    return true;
}

static void ignoreSignal() {
    struct sigaction ignore_act;
    ignore_act.sa_handler = SIG_IGN;
    ignore_act.sa_flags = 0;
    sigemptyset(&ignore_act.sa_mask);

    sigaction(SIGINT, &ignore_act, NULL);
    sigaction(SIGQUIT, &ignore_act, NULL);
    sigaction(SIGCONT, &ignore_act, NULL);
    sigaction(SIGTSTP, &ignore_act, NULL);  //FIXME: job control
}

void exec_interactive(DSContext *ctx) {   // never return
    setlocale(LC_ALL, "");

    linenoiseSetEncodingFunctions(
            linenoiseUtf8PrevCharLen,
            linenoiseUtf8NextCharLen,
            linenoiseUtf8ReadCode);

    DSContext_setOption(ctx, DS_OPTION_TOPLEVEL);
    dsContext = ctx;

    int exitStatus = 0;
    std::string line;
    while(readLine(line)) {
        ignoreSignal();
        DSStatus *status;
        int ret = DSContext_eval(ctx, line.c_str(), &status);
        unsigned int type = DSStatus_getType(status);
        DSStatus_free(&status);
        if(type == DS_STATUS_ASSERTION_ERROR || type == DS_STATUS_EXIT) {
            exitStatus = ret;
            break;
        }
    }

    DSContext_delete(&ctx);
    exit(exitStatus);
}

