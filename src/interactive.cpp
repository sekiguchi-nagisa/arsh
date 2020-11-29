/*
 * Copyright (C) 2015-2020 Nagisa Sekiguchi
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

#include <unistd.h>
#include <fcntl.h>

#include <cstring>
#include <string>
#include <memory>
#include <cerrno>
#include <cstdlib>

#include <linenoise.h>

#include <ydsh/ydsh.h>
#include "misc/unicode.hpp"
#include "misc/resource.hpp"

static DSState *state;

/**
 * line is not nullptr
 */
static bool isSkipLine(const ydsh::CStrPtr &line) {
    for(const char *ptr = line.get(); *ptr != '\0'; ptr++) {
        switch(*ptr) {
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
static bool checkLineContinuation(const ydsh::CStrPtr &line) {
    const char *begin = line.get();
    unsigned int count = 0;
    for(const char *ptr = begin + strlen(begin) - 1; ptr != begin && *ptr == '\\'; ptr--) {
        count++;
    }
    return count % 2 != 0;
}

static void addHistory(const char *line) {
    DSState_lineEdit(state, DS_EDIT_HIST_ADD, 0, &line);
}

static void loadHistory() {
    DSState_lineEdit(state, DS_EDIT_HIST_LOAD, 0, nullptr);
}

static void saveHistory() {
    DSState_lineEdit(state, DS_EDIT_HIST_SAVE, 0, nullptr);
}

static const char *prompt(unsigned int n) {
    const char *buf = "";
    DSState_lineEdit(state, DS_EDIT_PROMPT, n, &buf);
    return buf;
}

static const std::string *lineBuf = nullptr;

static bool readLine(std::string &line) {
    line.clear();
    lineBuf = &line;

    bool continuation = false;
    while(true) {
        errno = 0;
        auto str = ydsh::CStrPtr(linenoise(prompt(continuation ? 2 : 1)));
        if(str == nullptr) {
            if(errno == EAGAIN) {
                continuation = false;
                line.clear();
                continue;
            }
            if(DSState_mode(state) != DS_EXEC_MODE_NORMAL) {
                return false;
            }
            line = "exit\n";
            return true;
        }

        if(isSkipLine(str)) {
            continue;
        }
        line += str.get();
        continuation = checkLineContinuation(str);
        if(continuation) {
            line.pop_back(); // remove '\\'
            continue;
        }
        break;
    }

    addHistory(line.c_str());
    line += '\n';    // terminate newline
    return true;
}

// for linenoise encoding function
using namespace ydsh;

static std::size_t prevUtf8CharLen(const char *buf, int pos) {
    int end = pos--;
    while(pos >= 0 && (static_cast<unsigned char>(buf[pos]) & 0xC0) == 0x80) {
        pos--;
    }
    return end - pos;
}

static std::size_t encoding_nextCharLen(const char *buf, std::size_t bufSize,
                                        std::size_t pos, std::size_t *columSize) {
    auto charWidth = UnicodeUtil::AmbiguousCharWidth::HALF_WIDTH;
    if(linenoiseEastAsianWidth() == 2) {
        charWidth = UnicodeUtil::AmbiguousCharWidth::FULL_WIDTH;
    }

    std::size_t startPos = pos;
    const char *limit = buf + bufSize;
    int codePoint = 0;
    unsigned int byteSize = UnicodeUtil::utf8ToCodePoint(buf + pos, limit, codePoint);
    int width = UnicodeUtil::width(codePoint, charWidth);
    if(width == -2) {
        return 1;   // may be broken string
    }

    if(columSize != nullptr) {
        *columSize = width < 2 ? 1 : 2;
    }
    pos += byteSize;

    // skip next combining character
    while(pos < bufSize) {
        byteSize = UnicodeUtil::utf8ToCodePoint(buf + pos, limit, codePoint);
        if(!UnicodeUtil::isCombiningChar(codePoint)) {
            break;
        }
        pos += byteSize;
    }
    return pos - startPos;
}

static std::size_t encoding_prevCharLen(const char *buf, std::size_t,
                                        std::size_t pos, std::size_t *columSize) {
    auto charWidth = UnicodeUtil::AmbiguousCharWidth::HALF_WIDTH;
    if(linenoiseEastAsianWidth() == 2) {
        charWidth = UnicodeUtil::AmbiguousCharWidth::FULL_WIDTH;
    }

    std::size_t end = pos;
    while (pos > 0) {
        std::size_t len = prevUtf8CharLen(buf, pos);
        pos -= len;
        int codePoint = UnicodeUtil::utf8ToCodePoint(buf + pos, len);
        int width = UnicodeUtil::width(codePoint, charWidth);
        if(width != -2) {
            if(columSize != nullptr) {
                *columSize = width < 2 ? 1 : 2;
            }
            return end - pos;
        }
    }
    return 1;   // may be broken string
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
    std::string actualBuf(*lineBuf);
    size_t actualCursor = actualBuf.size() + cursor;
    actualBuf += buf;
    actualBuf += '\n';

    const char *line = actualBuf.c_str();
    DSState_complete(state, DS_COMP_INVOKE, actualCursor, &line);
    lc->len = DSState_complete(state, DS_COMP_SIZE, 0, nullptr);
    lc->cvec = static_cast<char**>(malloc(sizeof(char *) * lc->len));
    for(unsigned int i = 0; i < lc->len; i++) {
        const char *value = nullptr;
        DSState_complete(state, DS_COMP_GET, i, &value);
        lc->cvec[i] = strdup(value);
    }
    DSState_complete(state, DS_COMP_CLEAR, 0, nullptr);
}

static const char *historyCallback(const char *buf, int *historyIndex, historyOp op) {
    const unsigned int size = DSState_lineEdit(state, DS_EDIT_HIST_SIZE, 0, nullptr);
    switch(op) {
    case LINENOISE_HISTORY_OP_NEXT:
    case LINENOISE_HISTORY_OP_PREV: {
        if(size > 1) {
            DSState_lineEdit(state, DS_EDIT_HIST_SET, size - *historyIndex - 1, &buf);
            *historyIndex += (op == LINENOISE_HISTORY_OP_PREV) ? 1 : -1;
            if(*historyIndex < 0) {
                *historyIndex = 0;
                return nullptr;
            }
            if(static_cast<unsigned int>(*historyIndex) >= size) {
                *historyIndex = size - 1;
                return nullptr;
            }
            const char *ret = nullptr;
            DSState_lineEdit(state, DS_EDIT_HIST_GET, size - *historyIndex - 1, &ret);
            return ret;
        }
        break;
    }
    case LINENOISE_HISTORY_OP_DELETE:
        DSState_lineEdit(state, DS_EDIT_HIST_DEL, size - 1, nullptr);
        break;
    case LINENOISE_HISTORY_OP_INIT:
        DSState_lineEdit(state, DS_EDIT_HIST_INIT, 0, nullptr);
        break;
    case LINENOISE_HISTORY_OP_SEARCH:
        DSState_lineEdit(state, DS_EDIT_HIST_SEARCH, size - *historyIndex - 1, &buf);
        return buf;
    }
    return nullptr;
}

static std::pair<DSErrorKind, int> loadRC(const std::string &rcfile) {
    CStrPtr fullpath;
    if(!rcfile.empty()) {
        fullpath.reset(realpath(rcfile.c_str(), nullptr));
    }
    if(!fullpath) {
        return {DS_ERROR_KIND_SUCCESS, 0};
    }

    DSError e{};
    int ret = DSState_loadModule(state, fullpath.get(), DS_MOD_IGNORE_ENOENT, &e);
    auto kind = e.kind;
    DSError_release(&e);

    // reset line num
    DSState_setLineNum(state, 1);

    return {kind, ret};
}

int exec_interactive(DSState *dsState, const std::string &rcfile) {
    state = dsState;

    *linenoiseInputFD() = fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 0);
    *linenoiseOutputFD() = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0);
    *linenoiseErrorFD() = *linenoiseOutputFD();

    linenoiseSetEncodingFunctions(
            encoding_prevCharLen,
            encoding_nextCharLen,
            encoding_readCode);

    linenoiseSetMultiLine(1);

    linenoiseSetCompletionCallback(completeCallback);

    linenoiseSetHistoryCallback(historyCallback);

    unsigned int option = DS_OPTION_JOB_CONTROL | DS_OPTION_INTERACTIVE;
    DSState_setOption(dsState, option);

    auto ret = loadRC(rcfile);
    if(ret.first != DS_ERROR_KIND_SUCCESS) {
        return ret.second;
    }

    loadHistory();

    int status = 0;
    for(std::string line; readLine(line);) {
        DSError e;  //NOLINT
        status = DSState_eval(dsState, nullptr, line.c_str(), line.size(), &e);
        auto kind = e.kind;
        DSError_release(&e);
        if(kind == DS_ERROR_KIND_EXIT || kind == DS_ERROR_KIND_ASSERTION_ERROR) {
            break;
        }
    }
    saveHistory();
    return status;
}

