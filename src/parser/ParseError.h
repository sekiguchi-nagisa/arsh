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

#include "Token.h"
#include "Lexer.hpp"

namespace ydsh {
namespace parser {

class ParseErrorVisitor;

class ParseError {
protected:
    /**
     * lineNum of error token.
     */
    unsigned int lineNum;

    TokenKind kind;
    Token errorToken;

public:
    ParseError(unsigned int lineNum, TokenKind kind, Token errorToken);

    virtual ~ParseError();

    unsigned int getLineNum() const;

    TokenKind getTokenKind() const;

    Token getErrorToken() const;

    bool operator==(const ParseError &e);

    virtual void accept(ParseErrorVisitor &visitor) const = 0;

private:
    virtual bool equalsImpl(const ParseError &e) = 0;

protected:
    bool baseEquals(const ParseError &e);
};

class TokenMismatchError : public ParseError {
private:
    TokenKind expected;

public:
    TokenMismatchError(unsigned int lineNum, TokenKind actual,
                       Token errorToken, TokenKind expected);

    ~TokenMismatchError();

    TokenKind getExpectedTokenKind() const;

    bool operator==(const TokenMismatchError &e);

    void accept(ParseErrorVisitor &visitor) const; // override

private:
    bool equalsImpl(const ParseError &e); // override
};

class NoViableAlterError : public ParseError {
private:
    std::vector<TokenKind> alters;

public:
    /**
     * the last element of alters must be DUMMY.
     * copy alters to this->alters.
     */
    NoViableAlterError(unsigned int lineNum, TokenKind actual,
                       Token errorToken, TokenKind *alters);

    ~NoViableAlterError();

    const std::vector<TokenKind> &getAlters() const;

    bool operator==(const NoViableAlterError &e);

    void accept(ParseErrorVisitor &visitor) const; // override

private:
    bool equalsImpl(const ParseError &e); // override
};

class InvalidTokenError : public ParseError {
public:
    InvalidTokenError(unsigned int lineNum, Token token);

    ~InvalidTokenError();

    bool operator==(const InvalidTokenError &e);

    void accept(ParseErrorVisitor &visitor) const; // override

private:
    bool equalsImpl(const ParseError &e); // override
};

class OutOfRangeNumError : public ParseError {
public:
    OutOfRangeNumError(unsigned int lineNum, TokenKind kind, Token token);

    ~OutOfRangeNumError();

    bool operator==(const OutOfRangeNumError &e);

    void accept(ParseErrorVisitor &visitor) const; // override

private:
    bool equalsImpl(const ParseError &e); // override
};


// for error message formatting.
class ParseErrorVisitor {
public:
    ParseErrorVisitor();

    virtual ~ParseErrorVisitor();

    virtual void visit(const TokenMismatchError &e) = 0;

    virtual void visit(const NoViableAlterError &e) = 0;

    virtual void visit(const InvalidTokenError &e) = 0;

    virtual void visit(const OutOfRangeNumError &e) = 0;
};

} // namespace parser
} // namespace ydsh

#endif /* PARSER_PARSEERROR_H_ */
