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

#ifndef PARSER_PARSEERROR_H_
#define PARSER_PARSEERROR_H_

#include <parser/Token.h>
#include <parser/Lexer.h>

class ParseError {
protected:
    /**
     * lineNum of error token.
     */
    unsigned int lineNum;

    TokenKind errorToken;

public:
    ParseError(unsigned int lineNum, TokenKind errorToken);
    virtual ~ParseError();

    unsigned int getLineNum();
    TokenKind getErrorToken();

    bool operator==(const ParseError &e);

private:
    virtual bool equalsImpl(const ParseError &e) = 0;

protected:
    bool baseEquals(const ParseError &e);
};

class TokenMismatchError : public ParseError {
private:
    TokenKind expected;

public:
    TokenMismatchError(unsigned int lineNum, TokenKind actual, TokenKind expected);
    ~TokenMismatchError();

    TokenKind getExpectedToken();

    bool operator==(const TokenMismatchError &e);

private:
    bool equalsImpl(const ParseError &e); // override
};

class NoViableAlterError : public ParseError {
private:
    TokenKind* alters;
    unsigned int alterSize;

public:
    /**
     * the last element of alters must be DUMMY.
     * copy alters to this->alters.
     */
    NoViableAlterError(unsigned int lineNum, TokenKind actual, TokenKind *alters);
    ~NoViableAlterError();

    TokenKind *getAlters();
    unsigned int getAlterSize();

    bool operator==(const NoViableAlterError &e);

private:
    bool equalsImpl(const ParseError &e); // override
};

class InvalidTokenError : public ParseError {
private:
    Token token;

public:
    InvalidTokenError(unsigned int lineNum, Token token);
    ~InvalidTokenError();

    Token getInvalidToken();

    bool operator==(const InvalidTokenError &e);

private:
    bool equalsImpl(const ParseError &e); // override
};

#endif /* PARSER_PARSEERROR_H_ */
