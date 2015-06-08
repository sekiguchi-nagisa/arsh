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

#ifndef PARSER_LEXER_H_
#define PARSER_LEXER_H_

#include <stdlib.h>
#include <stdint.h>
#include <vector>

#include "Token.h"
#include "InputBuffer.hpp"

#define EACH_LEXER_MODE(OP) \
    OP(yycSTMT) \
    OP(yycEXPR) \
    OP(yycNAME) \
    OP(yycDSTRING) \
    OP(yycCMD) \
    OP(yycTYPE)

namespace ydsh {
namespace parser {

typedef enum {
#define GEN_ENUM(ENUM) ENUM,
    EACH_LEXER_MODE(GEN_ENUM)
#undef GEN_ENUM
} LexerMode;

class Lexer : public InputBuffer,
              public ydsh::parser_base::LexerBase<Token, Lexer> {
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

    static const char *lexerModeNames[];

public:
    Lexer(const char *source) :
            InputBuffer(source), lineNum(1), modeStack(1, yycSTMT), prevNewLine(false) {
    }

    Lexer(FILE *fp) :
            InputBuffer(fp), lineNum(1), modeStack(1, yycSTMT), prevNewLine(false) {
    }

    ~Lexer() = default;

    void setPos(unsigned int pos);

    bool isPrevNewLine() {
        return this->prevNewLine;
    }

    void setLineNum(unsigned int lineNum) {
        this->lineNum = lineNum;
    }

    unsigned int getLineNum() const {
        return this->lineNum;
    }

    void pushLexerMode(LexerMode mode) {
        this->modeStack.push_back(mode);
    }

    void popLexerMode() {
        this->modeStack.pop_back();
    }

    const char *getLexerModeName(LexerMode mode) const {
        return lexerModeNames[mode];
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
     * get text of token.
     */
    std::string toTokenText(const Token &token) const;

    /**
     * convert token to string (single quote string or double quote string)
     */
    std::string toString(const Token &token, bool isSingleQuote = true) const;

    /**
     * convert token to command argument
     * if expandTilde is true, the replace first tilde to HOME
     */
    std::string toCmdArg(const Token &token, bool expandTilde = false) const;

    /**
     * convert token to name(remove '$' char)
     * ex. $hoge, ${hoge}, hoge
     */
    std::string toName(const Token &token) const;

    /**
     * if converted number is out of range, status is 1.
     */
    char toInt8(const Token &token, int &status) const;

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

#endif /* PARSER_LEXER_H_ */
