/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#ifndef YDSH_PARSER_PARSER_BASE_HPP
#define YDSH_PARSER_PARSER_BASE_HPP

#include <vector>
#include <ostream>

#include "lexer_base.hpp"

namespace ydsh {
namespace parser_base {

template <typename T>
class ParseError {
private:
    T kind;
    Token errorToken;
    const char *errorKind;
    std::string message;

public:
    ParseError(T kind, Token errorToken, const char *errorKind, std::string &&message) :
            kind(kind), errorToken(errorToken),
            errorKind(errorKind), message(std::move(message)) { }

    ~ParseError() = default;

    const Token &getErrorToken() const {
        return this->errorToken;
    }

    T getTokenKind() const {
        return this->kind;
    }

    const char *getErrorKind() const {
        return this->errorKind;
    }

    const std::string &getMessage() const {
        return this->message;
    }
};

template <typename T>
struct EmptyTokenTracker {
    void operator()(T, Token) {}
};

template<typename T, typename LexerImpl, typename Tracker = EmptyTokenTracker<T>>
class ParserBase {
protected:
    /**
     * need 'T nextToken(Token)'
     */
    LexerImpl *lexer;

    T curKind;
    Token curToken;

    /**
     * need 'void operator()(T, Token)'
     */
    Tracker *tracker;

public:
    ParserBase() : lexer(nullptr), curKind(), curToken(), tracker(nullptr) { }

    void setTracker(Tracker *tracker) {
        this->tracker = tracker;
    }

    Tracker *getTracker() {
        return this->tracker;
    }

protected:
    ~ParserBase() = default;

    /**
     * low level api. not directly use it.
     */
    void fetchNext() {
        this->curKind = this->lexer->nextToken(this->curToken);
    }

    void trace() {
        if(this->tracker != nullptr) {
            (*this->tracker)(this->curKind, this->curToken);
        }
    }

    Token expect(T kind, bool fetchNext = true);

    T consume();

    [[noreturn]]
    void alternativeError(const unsigned int size, const T *const alters) const throw(ParseError<T>);

    [[noreturn]]
    static void raiseTokenMismatchedError(T kind, Token errorToken, T expected);
    [[noreturn]]
    static void raiseNoViableAlterError(T kind, Token errorToken,
                                        const unsigned int size, const T *const alters);
    [[noreturn]]
    static void raiseInvalidTokenError(T kind, Token errorToken);
};

// ########################
// ##     ParserBase     ##
// ########################

template<typename T, typename LexerImpl, typename Tracker>
Token ParserBase<T, LexerImpl, Tracker>::expect(T kind, bool fetchNext) {
    if(this->curKind != kind) {
        if(LexerImpl::isInvalidToken(this->curKind)) {
            raiseInvalidTokenError(this->curKind, this->curToken);
        }
        raiseTokenMismatchedError(this->curKind, this->curToken, kind);
    }
    this->trace();
    Token token = this->curToken;
    if(fetchNext) {
        this->fetchNext();
    }
    return token;
}

template<typename T, typename LexerImpl, typename Tracker>
T ParserBase<T, LexerImpl, Tracker>::consume() {
    T kind = this->curKind;
    this->trace();
    this->fetchNext();
    return kind;
}

template<typename T, typename LexerImpl, typename Tracker>
void ParserBase<T, LexerImpl, Tracker>::alternativeError(
        const unsigned int size, const T *const alters) const throw(ParseError<T>) {
    if(LexerImpl::isInvalidToken(this->curKind)) {
        raiseInvalidTokenError(this->curKind, this->curToken);
    }
    raiseNoViableAlterError(this->curKind, this->curToken, size, alters);
}

template<typename T, typename LexerImpl, typename Tracker>
void ParserBase<T, LexerImpl, Tracker>::raiseTokenMismatchedError(T kind, Token errorToken, T expected) {
    std::string message("mismatched token: ");
    message += toString(kind);
    message += ", expected: ";
    message += toString(expected);

    throw ParseError<T>(kind, errorToken, "TokenMismatched", std::move(message));
}

template<typename T, typename LexerImpl, typename Tracker>
void ParserBase<T, LexerImpl, Tracker>::raiseNoViableAlterError(T kind, Token errorToken,
                                                       const unsigned int size, const T *const alters) {
    std::string message = "no viable alternative: ";
    message += toString(kind);
    if(size > 0 && alters != nullptr) {
        message += ", expected: ";
        for(unsigned int i = 0; i < size; i++) {
            if(i > 0) {
                message += ", ";
            }
            message += toString(alters[i]);
        }
    }

    throw ParseError<T>(kind, errorToken, "NoViableAlter", std::move(message));
}

template<typename T, typename LexerImpl, typename Tracker>
void ParserBase<T, LexerImpl, Tracker>::raiseInvalidTokenError(T kind, Token errorToken) {
    throw ParseError<T>(kind, errorToken, "InvalidToken", std::string("invalid token"));
}

} //namespace parser_base
} //namespace ydsh


#endif //YDSH_PARSER_PARSER_BASE_HPP
