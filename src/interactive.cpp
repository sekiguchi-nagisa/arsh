/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#include <csignal>
#include <cstring>
#include <string>
#include <cerrno>
#include <memory>
#include <cstdlib>

#include <unistd.h>

#include <linenoise.h>

#include <ydsh/ydsh.h>
#include "misc/unicode.hpp"

static DSContext *dsContext;

struct Deleter {
    void operator()(char *ptr) {
        free(ptr);
    }
};

typedef std::unique_ptr<char, Deleter> StrWrapper;

/**
 * line is not nullptr
 */
static bool isSkipLine(const StrWrapper &line) {
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

/**
 * line is not nullptr
 */
static bool checkLineContinuation(const StrWrapper &line) {
    const char *ptr = line.get();
    for(unsigned int i = 0; ptr[i] != '\0'; i++) {
        if(ptr[i] == '\\' && ptr[i + 1] == '\0') {
            return true;
        }
    }
    return false;
}

static std::string lineBuf;

static const char *readLine() {
    lineBuf.clear();

    bool continuation = false;
    while(true) {
        errno = 0;
        auto str = StrWrapper(linenoise(DSContext_prompt(dsContext, continuation ? 2 : 1)));
        if(str == nullptr) {
            if(errno == EAGAIN) {
                continuation = false;
                lineBuf.clear();
                continue;
            }
            return nullptr;
        }

        if(isSkipLine(str)) {
            continue;
        }
        lineBuf += str.get();
        continuation = checkLineContinuation(str);
        if(continuation) {
            lineBuf.pop_back(); // remove '\\'
            continue;
        }
        break;
    }

    linenoiseHistoryAdd(lineBuf.c_str());
    lineBuf += '\n';    // terminate newline
    return lineBuf.c_str();
}

static void ignoreSignal() {
    struct sigaction ignore_act;
    ignore_act.sa_handler = SIG_IGN;
    ignore_act.sa_flags = 0;
    sigemptyset(&ignore_act.sa_mask);

    sigaction(SIGINT, &ignore_act, NULL);
    sigaction(SIGQUIT, &ignore_act, NULL);
    sigaction(SIGTSTP, &ignore_act, NULL);  //FIXME: job control
}


// for linenoise encoding function
using namespace ydsh::misc;

static std::size_t prevUtf8CharLen(const char *buf, int pos) {
    int end = pos--;
    while(pos >= 0 && ((unsigned char)buf[pos] & 0xC0) == 0x80) {
        pos--;
    }
    return end - pos;
}

static std::size_t encoding_nextCharLen(const char *buf, std::size_t bufSize,
                                        std::size_t pos, std::size_t *columSize) {
    std::size_t startPos = pos;
    int codePoint = 0;
    unsigned int byteSize = UnicodeUtil::utf8ToCodePoint(buf + pos, bufSize - pos, codePoint);
    int width = UnicodeUtil::localeAwareWidth(codePoint);
    if(width < 1) {
        return 0;
    }

    if(columSize != nullptr) {
        *columSize = width;
    }
    pos += byteSize;

    // skip next combining character
    while(pos < bufSize) {
        byteSize = UnicodeUtil::utf8ToCodePoint(buf + pos, bufSize - pos, codePoint);
        if(UnicodeUtil::localeAwareWidth(codePoint) > 0) {
            break;
        }
        pos += byteSize;
    }
    return pos - startPos;
}

static std::size_t encoding_prevCharLen(const char *buf, std::size_t,
                                        std::size_t pos, std::size_t *columSize) {
    std::size_t end = pos;
    while (pos > 0) {
        std::size_t len = prevUtf8CharLen(buf, pos);
        pos -= len;
        int codePoint = UnicodeUtil::utf8ToCodePoint(buf + pos, len);
        int width = UnicodeUtil::localeAwareWidth(codePoint);
        if(width > 0) {
            if(columSize != nullptr) {
                *columSize = width;
            }
            return end - pos;
        }
    }
    return 0;
}


static std::size_t encoding_readCode(int fd, char *buf, std::size_t bufSize, int *codePoint) {
    if(bufSize < 1) {
        return -1;
    }

    int readSize = read(fd, &buf[0], 1);
    if(readSize <= 0) {
        return readSize;
    }

    unsigned int byteSize = UnicodeUtil::utf8ByteSize(buf[0]);
    if(byteSize < 1 || byteSize > 4) {
        return -1;
    }

    if(byteSize > 1) {
        if(bufSize < byteSize) {
            return -1;
        }
        readSize = read(fd, &buf[1], byteSize - 1);
        if(readSize <= 0) {
            return readSize;
        }
    }
    return UnicodeUtil::utf8ToCodePoint(buf, bufSize, *codePoint);
}

static void completeCallback(const char *buf, size_t cursor, linenoiseCompletions *lc) {
    std::string actualBuf(lineBuf);
    size_t acutalCursor = actualBuf.size() + cursor;
    actualBuf += buf;
    actualBuf += '\n';

    DSCandidates c;
    DSContext_complete(dsContext, actualBuf.c_str(), acutalCursor, &c);
    lc->len = c.size;
    lc->cvec = c.values;
}

/**
 * after execution, delete ctx
 */
int exec_interactive(DSContext *ctx) {
    *linenoiseInputFD() = dup(STDIN_FILENO);
    *linenoiseOutputFD() = dup(STDOUT_FILENO);
    *linenoiseErrorFD() = dup(STDERR_FILENO);

    linenoiseSetEncodingFunctions(
            encoding_prevCharLen,
            encoding_nextCharLen,
            encoding_readCode);

    linenoiseSetCompletionCallback(completeCallback);

    DSContext_setOption(ctx, DS_OPTION_TOPLEVEL);
    dsContext = ctx;

    int exitStatus = 0;
    for(const char *line = nullptr; (line = readLine()) != nullptr; ) {
        ignoreSignal();
        int ret = DSContext_eval(ctx, nullptr, line);
        unsigned int type = DSContext_status(ctx);
        if(type == DS_STATUS_ASSERTION_ERROR || type == DS_STATUS_EXIT) {
            exitStatus = ret;
            break;
        }
    }

    DSContext_delete(&ctx);
    return exitStatus;
}

