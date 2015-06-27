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

#ifndef YDSH_PARSERBASE_HPP
#define YDSH_PARSERBASE_HPP

#include <vector>
#include <ostream>
#include "LexerBase.hpp"

namespace ydsh {
namespace parser_base {

namespace __parser_error {

template<typename T>
class ParseError {
protected:
    Token<T> errorToken;

public:
    ParseError(Token<T> errorToken) : errorToken(errorToken) { }

    virtual ~ParseError() = default;

    const Token<T> &getErrorToken() const {
        return this->errorToken;
    }

    T getTokenKind() const {
        return this->errorToken.kind;
    }

    unsigned int getLineNum() const {
        return this->errorToken.lineNum;
    }

    bool operator==(const ParseError<T> &e) const {
        return this->errorToken == e.errorToken;
    }
};

template<typename T>
class TokenMismatchedError : public ParseError<T> {
private:
    T expected;

public:
    TokenMismatchedError(Token<T> errrorToken, T expected) :
            ParseError<T>(errrorToken), expected(expected) { }

    ~TokenMismatchedError() = default;

    T getExpectedKind() const {
        return this->expected;
    }

    bool operator==(const TokenMismatchedError<T> &e) {
        return this->errorToken == e.errorToken && this->expected == e.expected;
    }
};

template<typename T>
std::ostream &operator<<(std::ostream &stream, const TokenMismatchedError<T> &e);

template<typename T>
std::ostream &operator<<(std::ostream &stream, const TokenMismatchedError<T> &e) {
    stream << "mismatched token: " << e.getTokenKind() << ", expect for" << e.getExpectedKind();
    return stream;
}


template<typename T>
class NoViableAlterError : public ParseError<T> {
private:
    std::vector<T> alters;

public:
    NoViableAlterError(Token<T> errorToken, std::vector<T> &&alters) :
            ParseError<T>(errorToken), alters(std::move(alters)) { }

    ~NoViableAlterError() = default;

    const std::vector<T> &getAlters() const {
        return this->alters;
    }

    bool operator==(const NoViableAlterError<T> &e);
};

template<typename T>
bool NoViableAlterError<T>::operator==(const NoViableAlterError<T> &e) {
    if(this->errorToken != e.errorToken) {
        return false;
    }

    // check size
    unsigned int size = this->alters.size();
    if(size != e.alters.size()) {
        return false;
    }

    // check each alters
    for(unsigned int i = 0; i < size; i++) {
        if(this->alters[i] != e.alters[i]) {
            return false;
        }
    }
    return true;
}

template<typename T>
std::ostream &operator<<(std::ostream &stream, const NoViableAlterError<T> &e);

template<typename T>
std::ostream &operator<<(std::ostream &stream, const NoViableAlterError<T> &e) {
    stream << "no viable alternative: " << e.getTokenKind() << ", expect for ";
    unsigned int count = 0;
    for(auto &a : e.getAlters()) {
        if(count++ > 0) {
            stream << ", ";
        }
        stream << a;
    }
    return stream;
}


template<typename T>
class InvalidTokenError : public ParseError<T> {
public:
    InvalidTokenError(Token<T> errorToken) : ParseError<T>(errorToken) { }

    ~InvalidTokenError() = default;

    bool operator==(const InvalidTokenError<T> &e) {
        return this->errorToken == e.errorToken;
    }
};

template<typename T>
std::ostream &operator<<(std::ostream &stream, const InvalidTokenError<T> &e);

template<typename T>
std::ostream &operator<<(std::ostream &stream, const InvalidTokenError<T> &e) {
    stream << "invalid token";
    return stream;
}

} // namespace __parser_error


template<typename T, typename LexerImpl>
class ParserBase {
public:
    typedef __parser_error::ParseError<T> ParseError;
    typedef __parser_error::TokenMismatchedError<T> TokenMismatchedError;
    typedef __parser_error::NoViableAlterError<T> NoViableAlterError;
    typedef __parser_error::InvalidTokenError<T> InvalidTokenError;

protected:
    LexerImpl *lexer;
    Token<T> curToken;

public:
    ParserBase() = default;

    virtual ~ParserBase() = default;

protected:
    void fetchNext() {
        this->lexer->nextToken(this->curToken);
    }

    void expect(T kind, bool fetchNext = true);

    void expect(T kind, Token<T> &token, bool fetchNext = true);

    T consume();

    void alternativeError(std::vector<T> &&alters);
};

// ########################
// ##     ParserBase     ##
// ########################

template<typename T, typename LexerImpl>
void ParserBase<T, LexerImpl>::expect(T kind, bool fetchNext) {
    if(this->curToken.kind != kind) {
        if(LexerImpl::isInvalidToken(this->curToken.kind)) {
            throw InvalidTokenError(this->curToken);
        }
        throw TokenMismatchedError(this->curToken, kind);
    }
    if(fetchNext) {
        this->fetchNext();
    }
}

template<typename T, typename LexerImpl>
void ParserBase<T, LexerImpl>::expect(T kind, Token<T> &token, bool fetchNext) {
    if(this->curToken.kind != kind) {
        if(LexerImpl::isInvalidToken(this->curToken.kind)) {
            throw InvalidTokenError(this->curToken);
        }
        throw TokenMismatchedError(this->curToken, kind);
    }
    token = this->curToken;
    if(fetchNext) {
        this->fetchNext();
    }
}

template<typename T, typename LexerImpl>
T ParserBase<T, LexerImpl>::consume() {
    T kind = this->curToken.kind;
    this->fetchNext();
    return kind;
}

template<typename T, typename LexerImpl>
void ParserBase<T, LexerImpl>::alternativeError(std::vector<T> &&alters) {
    if(LexerImpl::isInvalidToken(this->curToken.kind)) {
        throw InvalidTokenError(this->curToken);
    }
    throw NoViableAlterError(this->curToken, std::move(alters));
}

} //namespace parser_base
} //namespace ydsh


#endif //YDSH_PARSERBASE_HPP
