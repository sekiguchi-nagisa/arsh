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
    T kind;
    Token errorToken;

public:
    ParseError(T kind, Token errorToken) :
            kind(kind), errorToken(errorToken) { }

    virtual ~ParseError() = default;

    const Token &getErrorToken() const {
        return this->errorToken;
    }

    T getTokenKind() const {
        return this->kind;
    }
};

template<typename T>
class TokenMismatchedError : public ParseError<T> {
private:
    T expected;

public:
    TokenMismatchedError(T kind, Token errrorToken, T expected) :
            ParseError<T>(kind, errrorToken), expected(expected) { }

    ~TokenMismatchedError() = default;

    T getExpectedKind() const {
        return this->expected;
    }

    bool operator==(const TokenMismatchedError<T> &e) {
        return this->errorToken == e.errorToken && this->expected == e.expected;
    }
};

template<typename T>
std::ostream &operator<<(std::ostream &stream, const TokenMismatchedError<T> &e) {
    stream << "mismatched token: " << e.getTokenKind() << ", expected: " << e.getExpectedKind();
    return stream;
}


template<typename T>
class NoViableAlterError : public ParseError<T> {
private:
    std::vector<T> alters;

public:
    NoViableAlterError(T kind, Token errorToken, std::vector<T> &&alters) :
            ParseError<T>(kind, errorToken), alters(std::move(alters)) { }

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
std::ostream &operator<<(std::ostream &stream, const NoViableAlterError<T> &e) {
    stream << "no viable alternative: " << e.getTokenKind() << ", expected: ";
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
    InvalidTokenError(T kind, Token errorToken) : ParseError<T>(kind, errorToken) { }

    ~InvalidTokenError() = default;

    bool operator==(const InvalidTokenError<T> &e) {
        return this->errorToken == e.errorToken;
    }
};

template<typename T>
std::ostream &operator<<(std::ostream &stream, const InvalidTokenError<T> &) {
    return stream << "invalid token";
}

} // namespace __parser_error


template<typename T, typename LexerImpl>
class ParserBase {
public:
    typedef __parser_error::ParseError<T>           ParseError;
    typedef __parser_error::TokenMismatchedError<T> TokenMismatchedError;
    typedef __parser_error::NoViableAlterError<T>   NoViableAlterError;
    typedef __parser_error::InvalidTokenError<T>    InvalidTokenError;

protected:
    LexerImpl *lexer;
    T curKind;
    Token curToken;

public:
    ParserBase() = default;

    virtual ~ParserBase() = default;

protected:
    void fetchNext() {
        this->curKind = this->lexer->nextToken(this->curToken);
    }

    Token expect(T kind, bool fetchNext = true);

    T consume();

    void alternativeError(std::vector<T> &&alters);
};

// ########################
// ##     ParserBase     ##
// ########################

template<typename T, typename LexerImpl>
Token ParserBase<T, LexerImpl>::expect(T kind, bool fetchNext) {
    if(this->curKind != kind) {
        if(LexerImpl::isInvalidToken(this->curKind)) {
            throw InvalidTokenError(this->curKind, this->curToken);
        }
        throw TokenMismatchedError(this->curKind, this->curToken, kind);
    }
    Token token = this->curToken;
    if(fetchNext) {
        this->fetchNext();
    }
    return token;
}

template<typename T, typename LexerImpl>
T ParserBase<T, LexerImpl>::consume() {
    T kind = this->curKind;
    this->fetchNext();
    return kind;
}

template<typename T, typename LexerImpl>
void ParserBase<T, LexerImpl>::alternativeError(std::vector<T> &&alters) {
    if(LexerImpl::isInvalidToken(this->curKind)) {
        throw InvalidTokenError(this->curKind, this->curToken);
    }
    throw NoViableAlterError(this->curKind, this->curToken, std::move(alters));
}

} //namespace parser_base
} //namespace ydsh


#endif //YDSH_PARSERBASE_HPP
