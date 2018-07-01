/*
 * Copyright (C) 2015-2018 Nagisa Sekiguchi
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

#ifndef YDSH_MISC_LEXER_BASE_HPP
#define YDSH_MISC_LEXER_BASE_HPP

#include <cstring>
#include <cassert>
#include <string>
#include <vector>
#include <algorithm>

#include "unicode.hpp"
#include "noncopyable.h"
#include "token.hpp"
#include "buffer.hpp"
#include "resource.hpp"

namespace ydsh {
namespace parser_base {


namespace __detail {

/**
 * base lexer for re2c
 */
template<bool T>
class LexerBase {
protected:
    static_assert(T, "not allowed instantiation");

    /**
     * may be null, if input source is string.
     * must be binary mode.
     */
    FilePtr file;

    FlexBuffer<unsigned char> buf;

    /**
     * current reading pointer of buf.
     */
    unsigned char *cursor{nullptr};

    /**
     * limit of buf.
     */
    unsigned char *limit{nullptr};

    /**
     * for backtracking.
     */
    unsigned char *marker{nullptr};

    /**
     * for trailing context
     */
    unsigned char *ctxMarker{nullptr};

    static constexpr unsigned int DEFAULT_SIZE = 256;
    static constexpr int DEFAULT_READ_SIZE = 128;

protected:
    LexerBase() = default;

    ~LexerBase() = default;

public:
    NON_COPYABLE(LexerBase);

    LexerBase(LexerBase &&) = default;

    /**
     *
     * @param fp
     * must be opened with binary mode. after call it, not close fp.
     * @return
     */
    explicit LexerBase(FILE *fp);

    /**
     *
     * @param src
     * must be null terminated.
     * @return
     */
    explicit LexerBase(const char *src) : LexerBase(src, strlen(src)) {}

    /**
     *
     * @param data
     * @param size
     * @return
     */
    LexerBase(const char *data, unsigned int size);

    /**
     * get current reading position.
     */
    unsigned int getPos() const {
        return this->cursor - this->buf.get();
    }

    /**
     * used size of buf. must be this->getUsedSize() <= this->getBufSize().
     */
    unsigned int getUsedSize() const {
        return this->buf.size();
    }

    bool isEnd() const {
        return this->file == nullptr && this->cursor == this->limit;
    }

    bool withinRange(Token token) const {
        return token.pos + token.size <= this->getUsedSize();
    }

    /**
     * get text of token.
     */
    std::string toTokenText(Token token) const {
        assert(this->withinRange(token));
        return std::string((char *) (this->buf.get() + token.pos), token.size);
    }

    /**
     * buf size must be equivalent to base.size
     */
    void copyTokenText(Token token, char *buf) const {
        assert(this->withinRange(token));
        memcpy(buf, (char *)this->buf.get() + token.pos, token.size);
    }

    bool startsWith(Token token, char ch) const {
        assert(this->withinRange(token));
        return this->buf[token.pos] == ch;
    }

    bool equals(Token token, const char *str) const {
        assert(this->withinRange(token));
        return strlen(str) == token.size &&
                memcmp(this->buf.get() + token.pos, str, token.size) == 0;
    }

    /**
     * shift EOS token to left.
     * @param token
     * @return
     * if token is EOS, skip redundant white spaces and shift to left.
     * otherwise, return token.
     */
    Token shiftEOS(Token token) const;

    /**
     * get line token which token belongs to.
     */
    Token getLineToken(Token token) const;

    std::string formatLineMarker(Token lineToken, Token token) const;

private:
    void appendToBuf(const unsigned char *data, unsigned int size, bool isEnd);

    unsigned int toCodePoint(unsigned int offset, int &code) const {
        return UnicodeUtil::utf8ToCodePoint((char *)(this->buf.get() + offset), this->getUsedSize() - offset, code);
    }

protected:
    /**
     * fill buffer. called from this->nextToken().
     */
    bool fill(int n);
};

// #######################
// ##     LexerBase     ##
// #######################

template<bool T>
LexerBase<T>::LexerBase(FILE *fp) : LexerBase() {
    this->file.reset(fp);
    this->cursor = this->buf.get();
    this->limit = this->buf.get();
}

template<bool T>
LexerBase<T>::LexerBase(const char *data, unsigned int size) : LexerBase() {
    this->appendToBuf(reinterpret_cast<const unsigned char *>(data), size, true);
}

template <bool T>
Token LexerBase<T>::shiftEOS(Token token) const {
    if(token.size == 0) {
        unsigned int startIndex = token.pos;
        for(; startIndex > 0; startIndex--) {
            char ch = this->buf[startIndex];
            if(ch == ' ' || ch == '\t' || ch == '\n' || ch == '\000') {
                continue;
            }
            if(ch == '\\' && startIndex + 1 < token.pos) {
                char next = this->buf[startIndex + 1];
                if(next == ' ' || next == '\t' || next == '\n') {
                    continue;
                }
            }
            break;
        }
        token.pos = startIndex;
    }
    return token;
}

template <bool T>
Token LexerBase<T>::getLineToken(Token token) const {
    assert(this->withinRange(token));

    // find start index of line.
    long startIndex = token.pos;
    for(; startIndex > -1; startIndex--) {
        if(this->buf[startIndex] == '\n') {
            startIndex += (startIndex == token.pos) ? 0 : 1;
            break;
        }
    }
    if(startIndex == -1) {
        startIndex = 0;
    }

    // find stop index of line
    unsigned int stopIndex = token.pos + token.size;
    if(token.size > 0) {
        for(unsigned int usedSize = this->getUsedSize(); stopIndex < usedSize; stopIndex++) {
            if(this->buf[stopIndex] == '\n') {
                break;
            }
        }
    } else {
        stopIndex++;
    }

    assert(startIndex > -1);
    Token lineToken;
    lineToken.pos = static_cast<unsigned int>(startIndex);
    lineToken.size = stopIndex - static_cast<unsigned int>(startIndex);
    return lineToken;
}

template<bool T>
std::string LexerBase<T>::formatLineMarker(Token lineToken, Token token) const {
    assert(lineToken.pos <= token.pos);

    std::string marker;
    for(unsigned int i = lineToken.pos; i < token.pos;) {
        int code = 0;
        i += this->toCodePoint(i, code);
        if(code < 0) {
            return marker;
        }
        if(code == '\t' || code == '\n') {
            marker += static_cast<char>(code);
            continue;
        }
        int width = UnicodeUtil::localeAwareWidth(code);
        if(width == 1) {
            marker += " ";
        } else if(width == 2) {
            marker += "  ";
        }
    }
    const unsigned int stopPos = token.size + token.pos;
    if(token.size == 0) {
        marker += "  ^";
    }
    for(unsigned int i = token.pos; i < stopPos;) {
        unsigned int prev = i;
        int code = 0;
        i += this->toCodePoint(i, code);
        if(code < 0) {
            return marker;
        }
        if(code == '\t' || code == '\n') {
            marker += static_cast<char>(code);
            continue;
        }
        int width = UnicodeUtil::localeAwareWidth(code);
        if(width == 1) {
            marker += (prev == token.pos ? "^" : "~");
        } else if(width == 2) {
            marker += (prev == token.pos ? "^~" : "~~");
        }
    }
    return marker;
}

template <bool T>
void LexerBase<T>::appendToBuf(const unsigned char *data, unsigned int size, bool isEnd) {
    // save position
    const unsigned int pos = this->getPos();
    const unsigned int markerPos = this->marker - this->buf.get();
    const unsigned int ctxMarkerPos = this->ctxMarker - this->buf.get();

    this->buf.appendBy(size + 2, [&](unsigned char *ptr){
        unsigned int writeSize = size;
        memcpy(ptr, data, size);
        if(isEnd) {
            if(size == 0) {
                if(this->buf.empty() || this->buf.back() != '\n') {
                    *(ptr + writeSize) = '\n';
                    writeSize++;
                }
            } else if(data[size - 1] != '\n') {
                *(ptr + writeSize) = '\n';
                writeSize++;
            }
            *(ptr + writeSize) = '\0';
            writeSize++;
        }
        return writeSize;
    });

    // restore position
    this->cursor = this->buf.get() + pos;
    this->limit = this->buf.get() + this->buf.size();
    this->marker = this->buf.get() + markerPos;
    this->ctxMarker = this->buf.get() + ctxMarkerPos;
}

template<bool T>
bool LexerBase<T>::fill(int n) {
    if(this->file != nullptr) {
        int needSize = (n > DEFAULT_READ_SIZE) ? n : DEFAULT_READ_SIZE;
        unsigned char data[needSize];
        int readSize = fread(data, sizeof(unsigned char), needSize, this->file.get());
        if(readSize < needSize) {
            this->file.reset();
        }
        this->appendToBuf(data, readSize, this->file == nullptr);
    }
    return !this->isEnd();
}

} // namespace __detail

using LexerBase = __detail::LexerBase<true>;


} // namespace parser_base
} // namespace ydsh

#endif //YDSH_LEXER_BASE_HPP
