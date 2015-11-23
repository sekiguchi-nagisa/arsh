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
#include <vector>
#include <memory>
#include <algorithm>

#include "../misc/utf8.hpp"
#include "../misc/noncopyable.h"

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

inline std::ostream &operator<<(std::ostream &stream, const Token &token) {
    return stream << "(pos = " << token.pos << ", size = " << token.size << ")";
}

namespace __detail_srcinfo {

template <bool T>
class SourceInfo {
private:
    static_assert(T, "not allowed instantiation");

    std::string sourceName;

    /**
     * default value is 1.
     */
    unsigned int lineNumOffset;

    /**
     * contains newline character position.
     */
    std::vector<unsigned int> lineNumTable;

public:
    explicit SourceInfo(const char *sourceName) :
            sourceName(sourceName), lineNumOffset(1), lineNumTable() { }
    ~SourceInfo() = default;

    const std::string &getSourceName() const {
        return this->sourceName;
    }

    void setLineNumOffset(unsigned int offset) {
        this->lineNumOffset = offset;
    }

    unsigned int getLineNumOffset() const {
        return this->lineNumOffset;
    }

    const std::vector<unsigned int> &getLineNumTable() const {
        return this->lineNumTable;
    }

    void addNewlinePos(unsigned int pos);
    unsigned int getLineNum(unsigned int pos) const;
};

template <bool T>
void SourceInfo<T>::addNewlinePos(unsigned int pos) {
    if(this->lineNumTable.empty()) {
        this->lineNumTable.push_back(pos);
    } else if(pos > this->lineNumTable.back()) {
        this->lineNumTable.push_back(pos);
    }
}

template <bool T>
unsigned int SourceInfo<T>::getLineNum(unsigned int pos) const {
    auto iter = std::lower_bound(this->lineNumTable.begin(), this->lineNumTable.end(), pos);
    if(this->lineNumTable.end() == iter) {
        return this->lineNumTable.size() + this->lineNumOffset;
    }
    return iter - this->lineNumTable.begin() + this->lineNumOffset;
}

} // namespace __detail_srcinfo

typedef __detail_srcinfo::SourceInfo<true> SourceInfo;
typedef std::shared_ptr<SourceInfo> SourceInfoPtr;


namespace __detail {

/**
 * base lexer for re2c
 */
template<bool T>
class LexerBase {
protected:
    static_assert(T, "not allowed instantiation");

    SourceInfoPtr srcInfoPtr;

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
    explicit LexerBase(const char *sourceName) :
            srcInfoPtr(std::make_shared<SourceInfo>(sourceName)),
            fp(nullptr), bufSize(0), buf(nullptr), cursor(nullptr),
            limit(nullptr), marker(nullptr), ctxMarker(nullptr),
            endOfFile(false), endOfString(false), zeroCopyBuf(false) { }

public:
    NON_COPYABLE(LexerBase);

    /**
     * FILE must be opened with binary mode.
     * insert newline if not terminated by it.
     */
    LexerBase(const char *sourceName, FILE *fp);

    /**
     * must be null terminated.
     * if the last character of string(exclude null character) is newline, not copy it.
     * otherwise, copy it.
     */
    LexerBase(const char *sourceName, const char *src);

    virtual ~LexerBase() {
        if(!this->zeroCopyBuf) {
            delete[] this->buf;
        }
    }

    const SourceInfoPtr &getSourceInfoPtr() const {
        return this->srcInfoPtr;
    }

    void setLineNum(unsigned int lineNum) {
        this->srcInfoPtr->setLineNumOffset(lineNum);
    }

    unsigned int getLineNum() const {
        return this->srcInfoPtr->getLineNumOffset() +
               this->srcInfoPtr->getLineNumTable().size();
    }

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

    std::string formatLineMarker(Token lineToken, Token token) const;

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
LexerBase<T>::LexerBase(const char *sourceName, FILE *fp) : LexerBase(sourceName) {
    this->fp = fp;
    this->bufSize = DEFAULT_SIZE;
    this->buf = new unsigned char[this->bufSize];

    this->cursor = this->buf;
    this->limit = this->buf;
}

template<bool T>
LexerBase<T>::LexerBase(const char *sourceName, const char *src) : LexerBase(sourceName) {
    this->bufSize = strlen(src) + 1;
    if(this->bufSize == 1) {    // empty string
        src = "\n";
        this->bufSize = 2;
    }

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
std::string LexerBase<T>::formatLineMarker(Token lineToken, Token token) const {
    assert(lineToken.pos <= token.pos);

    std::string marker;
    for(unsigned int i = lineToken.pos; i < token.pos; i++) {
        marker += " ";
    }
    const unsigned int stopPos = token.size + token.pos;
    for(unsigned int i = token.pos; i < stopPos;) {
        unsigned int prev = i;
        i = misc::UTF8Util::getNextPos(i, this->buf[i]);
        if(i - prev == 1) { // ascii
            marker += (prev == token.pos ? "^" : "~");
        } else {
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
