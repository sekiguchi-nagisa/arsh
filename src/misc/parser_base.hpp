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

#ifndef YDSH_MISC_PARSER_BASE_HPP
#define YDSH_MISC_PARSER_BASE_HPP

#include <vector>

#include "misc/lexer_base.hpp"

namespace ydsh {
namespace parser_base {

template <typename T>
class ParseError {
private:
    T kind;
    Token errorToken;
    const char *errorKind;
    std::vector<T> expectedTokens;
    std::string message;

public:
    ParseError(T kind, Token errorToken, const char *errorKind, std::string &&message) :
            kind(kind), errorToken(errorToken), errorKind(errorKind),
            expectedTokens(), message(std::move(message)) { }

    ParseError(T kind, Token errorToken, const char *errorKind,
               std::vector<T> &&expectedTokens, std::string &&message) :
            kind(kind), errorToken(errorToken), errorKind(errorKind),
            expectedTokens(std::move(expectedTokens)), message(std::move(message)) { }

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

    const std::vector<T> &getExpectedTokens() const {
        return this->expectedTokens;
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
class AbstractParser {
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

private:
    std::unique_ptr<ParseError<T>> error;

public:
    AbstractParser() : lexer(nullptr), curKind(), curToken(), tracker(nullptr), error() { }

    void setTracker(Tracker *tracker) {
        this->tracker = tracker;
    }

    Tracker *getTracker() {
        return this->tracker;
    }

    bool hasError() const {
        return static_cast<bool>(this->error);
    }

    const ParseError<T> &getError() const {
        return *this->error;
    }

    void clear() {
        this->error.reset();
    }

protected:
    ~AbstractParser() = default;

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

    template <std::size_t N>
    void raiseNoViableAlterError(const T (&alters)[N]) {
        this->raiseNoViableAlterError(N, alters);
    }

    void raiseTokenMismatchedError(T expected);

    void raiseNoViableAlterError(unsigned int size, const T *const alters);

    void raiseInvalidTokenError(unsigned int size, const T *const alters);

    template <typename ...Arg>
    void createError(Arg && ...arg) {
        this->error.reset(new ParseError<T>(std::forward<Arg>(arg)...));
    }
};

// ############################
// ##     AbstractParser     ##
// ############################

template<typename T, typename LexerImpl, typename Tracker>
Token AbstractParser<T, LexerImpl, Tracker>::expect(T kind, bool fetchNext) {
    if(this->curKind != kind) {
        this->raiseTokenMismatchedError(kind);
        return this->curToken;
    }
    this->trace();
    Token token = this->curToken;
    if(fetchNext) {
        this->fetchNext();
    }
    return token;
}

template<typename T, typename LexerImpl, typename Tracker>
T AbstractParser<T, LexerImpl, Tracker>::consume() {
    T kind = this->curKind;
    this->trace();
    this->fetchNext();
    return kind;
}

template<typename T, typename LexerImpl, typename Tracker>
void AbstractParser<T, LexerImpl, Tracker>::raiseTokenMismatchedError(T expected) {
    if(LexerImpl::isInvalidToken(this->curKind)) {
        T alter[1] = { expected };
        this->raiseInvalidTokenError(1, alter);
    } else {
        std::string message("mismatched token: ");
        message += toString(this->curKind);
        message += ", expected: ";
        message += toString(expected);

        std::vector<T> expectedTokens(1);
        expectedTokens[0] = expected;
        this->createError(this->curKind, this->curToken, "TokenMismatched",
                          std::move(expectedTokens), std::move(message));
    }
}

template<typename T, typename LexerImpl, typename Tracker>
void AbstractParser<T, LexerImpl, Tracker>::raiseNoViableAlterError(unsigned int size, const T *const alters) {
    if(LexerImpl::isInvalidToken(this->curKind)) {
        this->raiseInvalidTokenError(size, alters);
    } else {
        std::string message = "no viable alternative: ";
        message += toString(this->curKind);
        if(size > 0 && alters != nullptr) {
            message += ", expected: ";
            for(unsigned int i = 0; i < size; i++) {
                if(i > 0) {
                    message += ", ";
                }
                message += toString(alters[i]);
            }
        }

        std::vector<T> expectedTokens(alters, alters + size);
        this->createError(this->curKind, this->curToken, "NoViableAlter",
                          std::move(expectedTokens), std::move(message));
    }
}

template<typename T, typename LexerImpl, typename Tracker>
void AbstractParser<T, LexerImpl, Tracker>::raiseInvalidTokenError(unsigned int size, const T *const alters) {
    std::string message = "invalid token, expected: ";
    if(size > 0 && alters != nullptr) {
        for(unsigned int i = 0; i < size; i++) {
            if(i > 0) {
                message += ", ";
            }
            message += toString(alters[i]);
        }
    }

    std::vector<T> expectedTokens(alters, alters + size);
    this->createError(this->curKind, this->curToken, "InvalidToken",
                      std::move(expectedTokens), std::move(message));
}


} //namespace parser_base
} //namespace ydsh


#endif //YDSH_MISC_PARSER_BASE_HPP
