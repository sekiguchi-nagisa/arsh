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
#include "string_ref.hpp"

namespace ydsh {

template <bool T>
class SourceInfoImpl : public RefCount<SourceInfoImpl<T>> {
private:
    static_assert(T, "not allowed instantiation");

    std::string sourceName;

    /**
     * default value is 1.
     */
    unsigned int lineNumOffset{1};

    /**
     * contains newline character position.
     */
    std::vector<unsigned int> lineNumTable;

public:
    explicit SourceInfoImpl(const char *sourceName) : sourceName(sourceName) { }
    ~SourceInfoImpl() = default;

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

// ############################
// ##     SourceInfoImpl     ##
// ############################

template <bool T>
void SourceInfoImpl<T>::addNewlinePos(unsigned int pos) {
    if(this->lineNumTable.empty() || pos > this->lineNumTable.back()) {
        this->lineNumTable.push_back(pos);
    }
}

template <bool T>
unsigned int SourceInfoImpl<T>::getLineNum(unsigned int pos) const {
    auto iter = std::lower_bound(this->lineNumTable.begin(), this->lineNumTable.end(), pos);
    if(this->lineNumTable.end() == iter) {
        return this->lineNumTable.size() + this->lineNumOffset;
    }
    return iter - this->lineNumTable.begin() + this->lineNumOffset;
}

using SourceInfo = IntrusivePtr<SourceInfoImpl<true>>;

namespace __detail {

/**
 * base lexer for re2c
 */
template<bool T>
class LexerBase {
protected:
    static_assert(T, "not allowed instantiation");

    SourceInfo srcInfo;

    /**
     * must be terminated with null character
     */
    ByteBuffer buf;

    /**
     * current reading pointer of buf.
     */
    const char *cursor{nullptr};

    /**
     * limit of buf.
     */
    const char *limit{nullptr};

    /**
     * for backtracking.
     */
    const char *marker{nullptr};

    /**
     * for trailing context
     */
    const char *ctxMarker{nullptr};

    LexerBase() = default;

    ~LexerBase() = default;

public:
    NON_COPYABLE(LexerBase);

    explicit LexerBase(const char *sourceName) : srcInfo(SourceInfo::create(sourceName)) {}

    LexerBase(LexerBase &&) noexcept = default;

    /**
     *
     * @param src
     * must be null terminated.
     * @return
     */
    explicit LexerBase(const char *sourceName, const char *src) : LexerBase(sourceName, src, strlen(src)) {}

    /**
     *
     * @param data
     * @param size
     * @return
     */
    LexerBase(const char *sourceName, const char *data, unsigned int size) : LexerBase(sourceName) {
        this->appendToBuf(data, size, true);
    }

    LexerBase(const char *sourceName, ByteBuffer &&buffer) : LexerBase(sourceName) {
        this->buf = std::move(buffer);
        if(this->buf.empty() || this->buf.back() != '\n') {
            this->buf += '\n';
        }
        this->buf += '\0';
        this->cursor = this->buf.get();
        this->limit = this->cursor + this->getUsedSize();
    }

    LexerBase &operator=(LexerBase &&lex) noexcept {
        this->swap(lex);
        return *this;
    }

    void swap(LexerBase &lex) noexcept {
        std::swap(this->srcInfo, lex.srcInfo);
        this->buf.swap(lex.buf);
        std::swap(this->cursor, lex.cursor);
        std::swap(this->limit, lex.limit);
        std::swap(this->marker, lex.marker);
        std::swap(this->ctxMarker, lex.ctxMarker);
    }

    const SourceInfo &getSourceInfo() const {
        return this->srcInfo;
    }

    void setLineNum(unsigned int lineNum) {
        this->srcInfo->setLineNumOffset(lineNum);
    }

    unsigned int getLineNum() const {
        return this->srcInfo->getLineNumOffset() +
               this->srcInfo->getLineNumTable().size();
    }

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
        return this->buf.size() - 1;
    }

    bool isEnd() const {
        return this->cursor - 1 == this->limit;
    }

    bool withinRange(Token token) const {
        return token.pos + token.size <= this->getUsedSize();
    }

    StringRef getStrRef(Token token) const {
        assert(this->withinRange(token));
        return StringRef(this->buf.get() + token.pos, token.size);
    }

    /**
     * get text of token.
     */
    std::string toTokenText(Token token) const {
        assert(this->withinRange(token));
        return std::string(this->buf.get() + token.pos, token.size);
    }

    bool startsWith(Token token, int ch) const {
        assert(this->withinRange(token));
        return this->buf[token.pos] == ch;
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

    /**
     * get token text without invalid characters.
     * @param token
     * @return
     */
    std::string formatTokenText(Token token) const;

    /**
     *
     * @param lineToken
     * @param token
     * @param eaw
     * width of east asian Ambiguous character.
     * if 0, auto set width (CJK aware).
     * if 1, halfwidth.
     * if 2, fullwidth
     * @return
     */
    std::string formatLineMarker(Token lineToken, Token token, int eaw = 0) const;

    /**
     *
     * @param data
     * @param size
     * size of data
     * @param isEnd
     * if true, append '\n\0'
     */
    void appendToBuf(const char *data, unsigned int size, bool isEnd);

private:
    unsigned int toCodePoint(unsigned int offset, int &code) const {
        return UnicodeUtil::utf8ToCodePoint(this->buf.get() + offset, this->getUsedSize() - offset, code);
    }

protected:
    void updateNewline(unsigned int pos);
};

// #######################
// ##     LexerBase     ##
// #######################

template <bool T>
Token LexerBase<T>::shiftEOS(Token token) const {
    if(token.size == 0) {
        unsigned int startIndex = token.pos;
        for(; startIndex > 0; startIndex--) {
            int ch = this->buf[startIndex];
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
    Token lineToken = {
            .pos = static_cast<unsigned int>(startIndex),
            .size = stopIndex - static_cast<unsigned int>(startIndex)
    };
    return lineToken;
}

template <bool T>
std::string LexerBase<T>::formatTokenText(Token token) const {
    std::string str;
    unsigned int limit = token.pos + token.size;
    for(unsigned int i = token.pos; i < limit;) {
        int code = 0;
        unsigned int size = this->toCodePoint(i, code);
        if(code < 0) {
            break;
        }
        str.append(this->buf.get() + i, size);
        i += size;
    }
    return str;
}

template<bool T>
std::string LexerBase<T>::formatLineMarker(Token lineToken, Token token, int eaw) const {
    assert(lineToken.pos <= token.pos);

    auto charWidth = UnicodeUtil::AmbiguousCharWidth::HALF_WIDTH;
    if(eaw == 2 || (eaw != 1 && UnicodeUtil::isCJKLocale())) {
        charWidth = UnicodeUtil::AmbiguousCharWidth::FULL_WIDTH;
    }

    std::string lineMarker;
    for(unsigned int i = lineToken.pos; i < token.pos;) {
        int code = 0;
        i += this->toCodePoint(i, code);
        if(code < 0) {
            return lineMarker;
        }
        if(code == '\t' || code == '\n') {
            lineMarker += static_cast<char>(code);
            continue;
        }
        int width = UnicodeUtil::width(code, charWidth);
        if(width == 1) {
            lineMarker += " ";
        } else if(width == 2) {
            lineMarker += "  ";
        }
    }
    const unsigned int stopPos = token.size + token.pos;
    if(token.size == 0) {
        lineMarker += "  ^";
    }
    for(unsigned int i = token.pos; i < stopPos;) {
        unsigned int prev = i;
        int code = 0;
        i += this->toCodePoint(i, code);
        if(code < 0) {
            return lineMarker;
        }
        if(code == '\t' || code == '\n') {
            lineMarker += static_cast<char>(code);
            continue;
        }
        int width = UnicodeUtil::width(code, charWidth);
        if(width == 1) {
            lineMarker += (prev == token.pos ? "^" : "~");
        } else if(width == 2) {
            lineMarker += (prev == token.pos ? "^~" : "~~");
        }
    }
    return lineMarker;
}

template <bool T>
void LexerBase<T>::appendToBuf(const char *data, unsigned int size, bool isEnd) {
    // save position
    const unsigned int pos = this->getPos();
    const unsigned int markerPos = this->marker - this->buf.get();
    const unsigned int ctxMarkerPos = this->ctxMarker - this->buf.get();

    if(!this->buf.empty()) {
        this->buf.pop_back();   // pop null character
    }
    this->buf.append(data, size);
    if(isEnd && (this->buf.empty() || this->buf.back() != '\n')) {
        this->buf += '\n';
    }
    this->buf += '\0';

    // restore position
    this->cursor = this->buf.get() + pos;
    this->limit = this->buf.get() + this->getUsedSize();
    this->marker = this->buf.get() + markerPos;
    this->ctxMarker = this->buf.get() + ctxMarkerPos;
}

template<bool T>
void LexerBase<T>::updateNewline(unsigned int pos) {
    const unsigned int stopPos = this->getPos();
    for(unsigned int i = pos; i < stopPos; ++i) {
        if(this->buf[i] == '\n') {
            this->srcInfo->addNewlinePos(i);
        }
    }
}

} // namespace __detail

using LexerBase = __detail::LexerBase<true>;

} // namespace ydsh

#endif //YDSH_LEXER_BASE_HPP
