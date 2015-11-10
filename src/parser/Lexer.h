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

#ifndef YDSH_LEXER_H
#define YDSH_LEXER_H

#include <cstdlib>
#include <cstdint>
#include <vector>

#include "TokenKind.h"
#include "LexerBase.hpp"

#define EACH_LEXER_MODE(OP) \
    OP(yycSTMT) \
    OP(yycEXPR) \
    OP(yycNAME) \
    OP(yycDSTRING) \
    OP(yycCMD) \
    OP(yycTYPE)

namespace ydsh {
namespace parser {

enum LexerMode{
#define GEN_ENUM(ENUM) ENUM,
    EACH_LEXER_MODE(GEN_ENUM)
#undef GEN_ENUM
};

const char *toModeName(LexerMode mode);

typedef ydsh::parser_base::Token<TokenKind> Token;

class Lexer : public ydsh::parser_base::LexerBase {
private:
    /**
     * initial value is 1.
     */
    unsigned int lineNum;

    /**
     * default mode is yycSTMT
     */
    std::vector<LexerMode> modeStack;

    bool prevNewLine;

    /**
     * only available in command mode.
     */
    bool prevSpace;

    LexerMode prevMode;

public:
    explicit Lexer(const char *source) :
            LexerBase(source),
            lineNum(1), modeStack(1, yycSTMT), prevNewLine(false), prevSpace(false), prevMode(yycSTMT) {}

    /**
     * FILE must be opened with binary mode.
     */
    explicit Lexer(FILE *fp) :
            LexerBase(fp),
            lineNum(1), modeStack(1, yycSTMT), prevNewLine(false), prevSpace(false), prevMode(yycSTMT) {}

    ~Lexer() = default;

    void setPos(unsigned int pos);

    bool isPrevNewLine() const {
        return this->prevNewLine;
    }

    bool isPrevSpace() const {
        return this->prevSpace;
    }

    LexerMode getPrevMode() const {
        return this->prevMode;
    }

    void setLineNum(unsigned int lineNum) {
        this->lineNum = lineNum;
    }

    unsigned int getLineNum() const {
        return this->lineNum;
    }

    void setLexerMode(LexerMode mode) {
        this->modeStack[this->modeStack.size() - 1] = mode;
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

    static bool isInvalidToken(TokenKind kind) {
        return kind == INVALID;
    }

    /**
     * lexer entry point.
     * write next token to token.
     * return the kind of next token.
     */
    void nextToken(Token &token);

    // some token api

    /**
     * get line token which token belongs to.
     */
    Token getLineToken(const Token &token, bool skipEOS = false) const;

    Token getLineTokenImpl(const Token &token) const;

    // token to value converting api.

    /**
     * convert single quote string literal token to string.
     */
    std::string singleToString(const Token &token) const;

    /**
     * convert escaped single quote string literal token to string.
     */
    std::string escapedSingleToString(const Token &token) const;

    /**
     * convert double quote string element token to string.
     */
    std::string doubleElementToString(const Token &token) const;

    /**
     * convert token to command argument
     */
    std::string toCmdArg(const Token &token) const;

    /**
     * convert token to name(remove '$' char)
     * ex. $hoge, ${hoge}, hoge
     */
    std::string toName(const Token &token) const;

    /**
     * if converted number is out of range, status is 1.
     */
    unsigned char toUint8(const Token &token, int &status) const;
    short toInt16(const Token &token, int &status) const;
    unsigned short toUint16(const Token &token, int &status) const;

    /**
     * equivalent to toInt32().
     */
    int toInt(const Token &token, int &status) const;

    int toInt32(const Token &token, int &status) const;
    unsigned int toUint32(const Token &token, int &status) const;
    long toInt64(const Token &token, int &status) const;
    unsigned long toUint64(const Token &token, int &status) const;

    /**
     * if converted number is out of range, status is 1.
     */
    double toDouble(const Token &token, int &status) const;
};

} // namespace parser
} // namespace ydsh

#endif //YDSH_LEXER_H
