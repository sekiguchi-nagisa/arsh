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

#ifndef YDSH_LEXERBASE_HPP
#define YDSH_LEXERBASE_HPP

#include <cstdio>
#include <cstring>
#include <cassert>
#include <string>
#include <ostream>
#include <type_traits>

#include "../misc/utf8.hpp"

namespace ydsh {
namespace parser_base {

struct TokenBase {
    unsigned int startPos;
    unsigned int size;
};

template<typename T>
struct Token : public TokenBase {
    static_assert(std::is_enum<T>::value, "must be enum type");

    unsigned int lineNum;
    T kind;

    bool operator==(const Token<T> &token) {
        return this->lineNum == token.lineNum && this->kind == token.kind &&
               this->startPos == token.startPos && this->size == token.size;
    }

    bool operator!=(const Token<T> &token) const {
        return !(*this == token);
    }
};

template <typename T>
std::ostream &operator<<(std::ostream &stream, const Token<T> &token) {
    return stream << "{ lineNum = " << token.lineNum << ", kind = " << token.kind
           << ", startPos = " << token.startPos << ", size = " << token.size << " }";
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
     * for trailing context.
     */
    unsigned char *ctxMarker;

    /**
     * if fp is null or fp reach EOF, it it true.
     */
    bool endOfFile;

    /**
     * if true, reach end of string. nextToken() always return EOS.
     */
    bool endOfString;

    bool zeroCopyBuf;

    static constexpr unsigned int DEFAULT_SIZE = 256;
    static constexpr int DEFAULT_READ_SIZE = 128;

private:
    LexerBase() :
            fp(0), bufSize(0), buf(0), cursor(0),
            limit(0), marker(0), ctxMarker(0),
            endOfFile(false), endOfString(false), zeroCopyBuf(false) { }

public:
    /**
     * FILE must be opened with binary mode.
     * insert newline if not terminated by it.
     */
    explicit LexerBase(FILE *fp);

    /**
     * must be null terminated.
     * if the last character of string(exclude null character) is newline, not copy it.
     * otherwise, copy it.
     */
    explicit LexerBase(const char *src);

    explicit LexerBase(const LexerBase &buffer) = delete;

    virtual ~LexerBase() {
        if(!this->zeroCopyBuf) {
            delete[] this->buf;
            this->buf = nullptr;
        }
    }

    LexerBase &operator=(const LexerBase &o) = delete;

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

    bool withinRange(const TokenBase &token) const {
        return token.startPos < this->getUsedSize()
               && token.startPos + token.size <= this->getUsedSize();
    }

    /**
     * get text of token.
     */
    std::string toTokenText(const TokenBase &token) const {
        assert(this->withinRange(token));
        return std::string((char *) (this->buf + token.startPos), token.size);
    }

    /**
     * buf size must be equivalent to base.size
     */
    void copyTokenText(const TokenBase &token, char *buf) const {
        assert(this->withinRange(token));
        memcpy(buf, (char *)this->buf + token.startPos, token.size);
    }

    bool startsWith(const TokenBase &token, char ch) const {
        assert(this->withinRange(token));
        return this->buf[token.startPos] == ch;
    }

    bool equals(const TokenBase &token, const char *str) const {
        assert(this->withinRange(token));
        return strlen(str) == token.size &&
                memcmp(this->buf + token.startPos, str, token.size) == 0;
    }

    std::string formatLineMarker(const TokenBase &lineToken, const TokenBase &token) const;

private:
    /**
     * if this->usedSize + needSize > this->maxSize, expand buf.
     */
    void expandBuf(unsigned int needSize);

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
LexerBase<T>::LexerBase(FILE *fp) : LexerBase<T>() {
    this->fp = fp;
    this->bufSize = DEFAULT_SIZE;
    this->buf = new unsigned char[this->bufSize];

    this->cursor = this->buf;
    this->limit = this->buf;
}

template<bool T>
LexerBase<T>::LexerBase(const char *src) : LexerBase<T>() {
    this->bufSize = strlen(src) + 1;
    this->zeroCopyBuf = src[this->bufSize - 2] == '\n';

    if(this->zeroCopyBuf) {
        this->buf = (unsigned char *) src;
    } else {    // copy src and insert newline
        unsigned int srcSize = this->bufSize - 1;
        this->bufSize++;
        this->buf = new unsigned char[this->bufSize];
        memcpy(this->buf, src, sizeof(unsigned char) * srcSize);
        this->buf[this->bufSize - 2] = '\n';
        this->buf[this->bufSize - 1] = '\0';
    }

    this->cursor = this->buf;
    this->limit = this->buf + this->bufSize - 1;
    this->endOfFile = true;
}

template<bool T>
std::string LexerBase<T>::formatLineMarker(const TokenBase &lineToken, const TokenBase &token) const {
    assert(lineToken.startPos <= token.startPos);

    std::string marker;
    for(unsigned int i = lineToken.startPos; i < token.startPos; i++) {
        marker += " ";
    }
    const unsigned int stopPos = token.size + token.startPos;
    for(unsigned int i = token.startPos; i < stopPos;) {
        unsigned int prev = i;
        i = misc::UTF8Util::getNextPos(i, this->buf[i]);
        if(i - prev == 1) { // ascii
            marker += (prev == token.startPos ? "^" : "~");
        } else {
            marker += (prev == token.startPos ? "^~" : "~~");
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
        unsigned int pos = this->getPos();
        unsigned int markerPos = this->marker - this->buf;
        unsigned int ctxMarkerPos = this->ctxMarker - this->buf;
        unsigned char *newBuf = new unsigned char[newSize];
        memcpy(newBuf, this->buf, sizeof(unsigned char) * usedSize);
        delete[] this->buf;
        this->buf = newBuf;
        this->bufSize = newSize;
        this->cursor = this->buf + pos;
        this->limit = this->buf + usedSize - 1;
        this->marker = this->buf + markerPos;
        this->ctxMarker = this->buf + ctxMarkerPos;
    }
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

typedef __detail::LexerBase<true> LexerBase;


} // namespace parser_base
} // namespace ydsh

#endif //YDSH_LEXERBASE_HPP
