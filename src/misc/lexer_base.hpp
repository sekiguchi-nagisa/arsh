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

#ifndef YDSH_MISC_LEXER_BASE_HPP
#define YDSH_MISC_LEXER_BASE_HPP

#include <cstdio>
#include <cstring>
#include <cassert>
#include <string>
#include <type_traits>
#include <vector>
#include <memory>
#include <algorithm>

#include "unicode.hpp"
#include "noncopyable.h"

namespace ydsh {
namespace parser_base {

struct Token {
    unsigned int pos;
    unsigned int size;

    bool operator==(const Token &token) const {
        return this->pos == token.pos && this->size == token.size;
    }

    bool operator!=(const Token &token) const {
        return !(*this == token);
    }
};

inline std::string toString(Token token) {
    std::string str = "(pos = ";
    str += std::to_string(token.pos);
    str += ", size = ";
    str += std::to_string(token.size);
    str += ")";
    return str;
}

namespace __detail {

/**
 * base lexer for re2c
 */
template<bool T>
class LexerBase {
protected:
    static_assert(T, "not allowed instantiation");

    /**
     * may be null, if input source is string. not closed it.
     * must be binary mode.
     */
    FILE *fp;

    unsigned int bufSize;

    /**
     * must terminate null character.
     */
    unsigned char *buf;

    /**
     * current reading pointer of buf.
     */
    unsigned char *cursor;

    /**
     * limit of buf.
     */
    unsigned char *limit;

    /**
     * for backtracking.
     */
    unsigned char *marker;

    /**
     * if fp is null or fp reach EOF, it it true.
     */
    bool endOfFile;

    /**
     * if true, reach end of string. nextToken() always return EOS.
     */
    bool endOfString;

    static constexpr unsigned int DEFAULT_SIZE = 256;
    static constexpr int DEFAULT_READ_SIZE = 128;

private:
    LexerBase() :
            fp(nullptr), bufSize(0), buf(nullptr), cursor(nullptr),
            limit(nullptr), marker(nullptr), endOfFile(false), endOfString(false) { }

public:
    NON_COPYABLE(LexerBase);

    /**
     * FILE must be opened with binary mode.
     * insert newline if not terminated by it.
     */

    /**
     *
     * @param fp
     * must be opened with binary mode.
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

protected:
    ~LexerBase() {
        delete[] this->buf;
    }

public:
    /**
     * get current reading position.
     */
    unsigned int getPos() const {
        return this->cursor - this->buf;
    }

    /**
     * used size of buf. must be this->getUsedSize() <= this->getBufSize().
     */
    unsigned int getUsedSize() const {
        return this->limit - this->buf + 1;
    }

    bool withinRange(Token token) const {
        return token.pos < this->getUsedSize()
               && token.pos + token.size <= this->getUsedSize();
    }

    /**
     * get text of token.
     */
    std::string toTokenText(Token token) const {
        assert(this->withinRange(token));
        return std::string((char *) (this->buf + token.pos), token.size);
    }

    /**
     * buf size must be equivalent to base.size
     */
    void copyTokenText(Token token, char *buf) const {
        assert(this->withinRange(token));
        memcpy(buf, (char *)this->buf + token.pos, token.size);
    }

    bool startsWith(Token token, char ch) const {
        assert(this->withinRange(token));
        return this->buf[token.pos] == ch;
    }

    bool equals(Token token, const char *str) const {
        assert(this->withinRange(token));
        return strlen(str) == token.size &&
                memcmp(this->buf + token.pos, str, token.size) == 0;
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
    /**
     * if this->usedSize + needSize > this->maxSize, expand buf.
     */
    void expandBuf(unsigned int needSize);

    /**
     * swap new buffer and old one, after swapping, update some pointers and bufSize
     */
    void swapBuffer(unsigned char *&newBuf, unsigned int &newSize);

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
    this->fp = fp;
    this->bufSize = DEFAULT_SIZE;
    this->buf = new unsigned char[this->bufSize];

    this->cursor = this->buf;
    this->limit = this->buf;
}

template<bool T>
LexerBase<T>::LexerBase(const char *data, unsigned int size) : LexerBase() {
    const bool insertingNewline = size == 0 || data[size - 1] != '\n';
    this->bufSize = size + 1 + (insertingNewline ? 1 : 0);

    this->buf = new unsigned char[this->bufSize];
    memcpy(this->buf, data, sizeof(unsigned char) * size);
    if(insertingNewline) {
        this->buf[this->bufSize - 2] = '\n';
    }
    this->buf[this->bufSize - 1] = '\0';

    this->cursor = this->buf;
    this->limit = this->buf + this->bufSize - 1;
    this->endOfFile = true;
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
    unsigned int startIndex = token.pos;
    for(; startIndex > 0; startIndex--) {
        if(this->buf[startIndex] == '\n') {
            startIndex += (startIndex == token.pos) ? 0 : 1;
            break;
        }
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
    Token lineToken;
    lineToken.pos = startIndex;
    lineToken.size = stopIndex - startIndex;
    return lineToken;
}

template<bool T>
std::string LexerBase<T>::formatLineMarker(Token lineToken, Token token) const {
    assert(lineToken.pos <= token.pos);

    std::string marker;
    for(unsigned int i = lineToken.pos; i < token.pos;) {
        int code = 0;
        i += UnicodeUtil::utf8ToCodePoint((char *)(this->buf + i), this->getUsedSize() - i, code);
        if(code < 0) {
            return marker;
        }
        if(code == '\t') {
            marker += "\t";
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
        i += UnicodeUtil::utf8ToCodePoint((char *)(this->buf + i), this->getUsedSize() - i, code);
        if(code < 0) {
            return marker;
        }
        if(code == '\t') {
            marker += "\t";
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

template<bool T>
void LexerBase<T>::expandBuf(unsigned int needSize) {
    unsigned int usedSize = this->getUsedSize();
    unsigned int size = usedSize + needSize;
    if(size > this->bufSize) {
        unsigned int newSize = this->bufSize;
        do {
            newSize += (newSize >> 1);
        } while(newSize < size);

        // swap to new buffer
        unsigned char *newBuf = new unsigned char[newSize];
        memcpy(newBuf, this->buf, sizeof(unsigned char) * usedSize);
        this->swapBuffer(newBuf, newSize);
        delete[] newBuf;
    }
}

template <bool T>
void LexerBase<T>::swapBuffer(unsigned char *&newBuf, unsigned int &newSize) {
    // save position
    const unsigned int usedSize = this->getUsedSize();
    const unsigned int pos = this->getPos();
    const unsigned int markerPos = this->marker - this->buf;

    // swap
    std::swap(this->buf, newBuf);
    std::swap(this->bufSize, newSize);

    // restore position
    this->cursor = this->buf + pos;
    this->limit = this->buf + usedSize - 1;
    this->marker = this->buf + markerPos;
}

template<bool T>
bool LexerBase<T>::fill(int n) {
    if(this->endOfString && this->limit - this->cursor <= 0) {
        return false;
    }

    if(!this->endOfFile) {
        int needSize = n - (this->limit - this->cursor);
        assert(needSize > -1);
        needSize = (needSize > DEFAULT_READ_SIZE) ? needSize : DEFAULT_READ_SIZE;
        this->expandBuf(needSize);
        int readSize = fread(this->limit, sizeof(unsigned char), needSize, this->fp);
        this->limit += readSize;
        *this->limit = '\0';
        if(readSize < needSize) {
            this->endOfFile = true;
            if(*(this->limit - 1) != '\n') {    // terminated newline
                this->expandBuf(1);
                *this->limit = '\n';
                this->limit += 1;
                *this->limit = '\0';
            }
        }
    }
    return true;
}

} // namespace __detail

using LexerBase = __detail::LexerBase<true>;


} // namespace parser_base
} // namespace ydsh

#endif //YDSH_LEXER_BASE_HPP
