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

#ifndef YDSH_LEXER_H
#define YDSH_LEXER_H

#include <cstdlib>
#include <cstdint>

#include "token_kind.h"
#include "misc/lexer_base.hpp"

namespace ydsh {

#include <yycond.h>

const char *toModeName(LexerMode mode);


class Lexer : public ydsh::LexerBase {
private:
    /**
     * default mode is yycSTMT
     */
    std::vector<LexerMode> modeStack;

    bool prevNewLine{false};

    /**
     * only available in command mode.
     */
    bool prevSpace{false};

    LexerMode prevMode{yycSTMT};

public:
    NON_COPYABLE(Lexer);

    Lexer(Lexer&&) = default;

    Lexer() = default;

    /**
     *
     * @param sourceName
     * must not be null.
     * @param source
     * must be null terminated.
     * @return
     */
    Lexer(const char *sourceName, const char *source) : Lexer(sourceName, source, strlen(source)) {}

    /**
     *
     * @param sourceName
     * must not be null
     * @param source
     * must not be null
     * @param size
     * @return
     */
    Lexer(const char *sourceName, const char *source, unsigned int size) :
            LexerBase(sourceName, source, size), modeStack(1, yycSTMT) {}

    /**
     * 
     * @param sourceName
     * must not be null.
     * @param file
     * must be opened with binary mode.
     * @return
     */
    Lexer(const char *sourceName, FilePtr &&file) :
            LexerBase(sourceName, std::move(file)), modeStack(1, yycSTMT) {}

    ~Lexer() = default;

    void setPos(unsigned int pos) {
        assert(this->buf.get() + pos <= this->limit);
        this->cursor = this->buf.get() + pos;
    }

    bool isPrevNewLine() const {
        return this->prevNewLine;
    }

    bool isPrevSpace() const {
        return this->prevSpace;
    }

    LexerMode getPrevMode() const {
        return this->prevMode;
    }

    void setLexerMode(LexerMode mode) {
        this->modeStack.back() = mode;
    }

    void pushLexerMode(LexerMode mode) {
        this->modeStack.push_back(mode);
    }

    void popLexerMode() {
        this->modeStack.pop_back();
    }

    LexerMode getLexerMode() const {
        return this->modeStack.back();
    }

    unsigned int lexerModeSize() const {
        return this->modeStack.size();
    }

    /**
     * lexer entry point.
     * write next token to token.
     * return the kind of next token.
     */
    TokenKind nextToken(Token &token);

    // token to value converting api.

    /**
     * convert single quote string literal token to string.
     * if token is illegal format(ex. illegal escape sequence), return false.
     */
    bool singleToString(Token token, std::string &out) const;

    /**
     * convert escaped single quote string literal token to string.
     * if token is illegal format(ex. illegal escape sequence), return false.
     */
    bool escapedSingleToString(Token token, std::string &out) const;

    /**
     * convert double quote string element token to string.
     */
    std::string doubleElementToString(Token token) const;

    /**
     * convert token to command argument
     */
    std::string toCmdArg(Token token) const;

    /**
     * convert token to name(remove '$' char)
     * ex. $hoge, ${hoge}, hoge
     */
    std::string toName(Token token) const;

    /**
     * if converted number is out of range, status is 1.
     */
    unsigned char toUint8(Token token, int &status) const;
    short toInt16(Token token, int &status) const;
    unsigned short toUint16(Token token, int &status) const;
    int toInt32(Token token, int &status) const;
    unsigned int toUint32(Token token, int &status) const;
    long toInt64(Token token, int &status) const;
    unsigned long toUint64(Token token, int &status) const;

    /**
     * if converted number is out of range, status is 1.
     */
    double toDouble(Token token, int &status) const;

private:
    bool isDecimal(Token token) const;
};

} // namespace ydsh

#endif //YDSH_LEXER_H
