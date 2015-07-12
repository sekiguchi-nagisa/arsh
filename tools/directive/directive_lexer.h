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

#ifndef YDSH_DIRECTIVE_LEXER_H
#define YDSH_DIRECTIVE_LEXER_H

#include <ostream>

#include <parser/LexerBase.hpp>

#define EACH_TOKEN(TOKEN) \
    TOKEN(DUMMY) \
    TOKEN(INVALID) \
    TOKEN(EOS) \
    TOKEN(APPLIED_NAME) \
    TOKEN(INT_LITERAL) \
    TOKEN(STRING_LITERAL) \
    TOKEN(ARRAY_OPEN) /* [ */\
    TOKEN(ARRAY_CLOSE) /* ] */\
    TOKEN(COMMA) /* , */\
    TOKEN(ASSIGN) /* = */\
    TOKEN(LP) /* ( */\
    TOKEN(RP) /* ) */\
    TOKEN(TRUE_LITERAL) /* TREU, True, true */\
    TOKEN(FALSE_LITERAL) /* FALSE, False, false */


namespace ydsh {
namespace directive {

typedef enum {
#define GEN_ENUM(ENUM) ENUM,
    EACH_TOKEN(GEN_ENUM)
#undef GEN_ENUM
} TokenKind;

const char *toString(TokenKind kind);

std::ostream &operator<<(std::ostream &stream, TokenKind);

typedef ydsh::parser_base::Token<TokenKind> Token;

class Lexer : public ydsh::parser_base::LexerBase {
private:
    unsigned int lineNum;

public:
    explicit Lexer(const char *src) : LexerBase(src, true) { }
    ~Lexer() = default;

    void nextToken(Token &token);

    int toInt(const Token &token, int &status) const;
    long toInt64(const Token &token, int &status) const;
    std::string toName(const Token &token) const;
    std::string toString(const Token &token) const;

    unsigned int getLineNum() const {
        return this->lineNum;
    }

    void setLineNum(unsigned int lineNum) {
        this->lineNum = lineNum;
    }

    static bool isInvalidToken(TokenKind kind) {
        return kind == INVALID;
    }
};


} // namespace directive
} // namespace ydsh


#endif //YDSH_DIRECTIVE_LEXER_H
