/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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

constexpr const char *TOKEN_MISMATCHED = "TokenMismatched";
constexpr const char *NO_VIABLE_ALTER  = "NoViableAlter";
constexpr const char *INVALID_TOKEN    = "InvalidToken";
constexpr const char *TOKEN_FORMAT     = "TokenFormat";
constexpr const char *DEEP_NESTING     = "DeepNesting";

template <typename T>
class ParseErrorBase {
private:
    T kind;
    Token errorToken;
    const char *errorKind;
    std::vector<T> expectedTokens;
    std::string message;

public:
    ParseErrorBase(T kind, Token errorToken, const char *errorKind, std::string &&message) :
            kind(kind), errorToken(errorToken), errorKind(errorKind),
            expectedTokens(), message(std::move(message)) { }

    ParseErrorBase(T kind, Token errorToken, const char *errorKind,
               std::vector<T> &&expectedTokens, std::string &&message) :
            kind(kind), errorToken(errorToken), errorKind(errorKind),
            expectedTokens(std::move(expectedTokens)), message(std::move(message)) { }

    ~ParseErrorBase() = default;

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
class ParserBase {
protected:
    /**
     * need 'T nextToken(Token)'
     */
    LexerImpl *lexer{nullptr};

    T curKind{};
    Token curToken{};

    /**
     * kind of latest consumed token
     */
    T consumedKind{};

    /**
     * need 'void operator()(T, Token)'
     */
    Tracker *tracker{nullptr};

    unsigned int callCount{0};

private:
    std::unique_ptr<ParseErrorBase<T>> error;

public:
    ParserBase() = default;

    void setTracker(Tracker *tracker) {
        this->tracker = tracker;
    }

    Tracker *getTracker() {
        return this->tracker;
    }

    const LexerImpl *getLexer() const {
        return this->lexer;
    }

    bool hasError() const {
        return static_cast<bool>(this->error);
    }

    const ParseErrorBase<T> &getError() const {
        return *this->error;
    }

    void clear() {
        this->error.reset();
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
        this->consumedKind = this->curKind;
        if(this->tracker != nullptr) {
            (*this->tracker)(this->curKind, this->curToken);
        }
    }

    Token expect(T kind, bool fetchNext = true);

    void consume();

    T scan() {
        T kind = this->curKind;
        this->consume();
        return kind;
    }

    template <std::size_t N>
    void raiseNoViableAlterError(const T (&alters)[N]) {
        this->raiseNoViableAlterError(N, alters);
    }

    void raiseTokenMismatchedError(T expected);

    void raiseNoViableAlterError(unsigned int size, const T *alters);

    void raiseInvalidTokenError(unsigned int size, const T *alters);

    void raiseTokenFormatError(T kind, Token token, const char *msg);

    void raiseDeepNestingError();

    template <typename ...Arg>
    void createError(Arg && ...arg) {
        this->error.reset(new ParseErrorBase<T>(std::forward<Arg>(arg)...));
    }
};

// ########################
// ##     ParserBase     ##
// ########################

template<typename T, typename LexerImpl, typename Tracker>
Token ParserBase<T, LexerImpl, Tracker>::expect(T kind, bool fetchNext) {
    auto token = this->curToken;
    if(this->curKind != kind) {
        this->raiseTokenMismatchedError(kind);
        return token;
    }
    this->trace();
    if(fetchNext) {
        this->fetchNext();
    }
    return token;
}

template<typename T, typename LexerImpl, typename Tracker>
void ParserBase<T, LexerImpl, Tracker>::consume() {
    this->trace();
    this->fetchNext();
}

template<typename T, typename LexerImpl, typename Tracker>
void ParserBase<T, LexerImpl, Tracker>::raiseTokenMismatchedError(T expected) {
    if(isInvalidToken(this->curKind)) {
        T alter[1] = { expected };
        this->raiseInvalidTokenError(1, alter);
    } else {
        std::string message("mismatched token: ");
        message += toString(this->curKind);
        message += ", expected: ";
        message += toString(expected);

        std::vector<T> expectedTokens(1);
        expectedTokens[0] = expected;
        this->createError(this->curKind, this->curToken, TOKEN_MISMATCHED,
                          std::move(expectedTokens), std::move(message));
    }
}

template<typename T, typename LexerImpl, typename Tracker>
void ParserBase<T, LexerImpl, Tracker>::raiseNoViableAlterError(unsigned int size, const T *alters) {
    if(isInvalidToken(this->curKind)) {
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
        this->createError(this->curKind, this->curToken, NO_VIABLE_ALTER,
                          std::move(expectedTokens), std::move(message));
    }
}

template<typename T, typename LexerImpl, typename Tracker>
void ParserBase<T, LexerImpl, Tracker>::raiseInvalidTokenError(unsigned int size, const T *alters) {
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
    this->createError(this->curKind, this->curToken, INVALID_TOKEN,
                      std::move(expectedTokens), std::move(message));
}

template<typename T, typename LexerImpl, typename Tracker>
void ParserBase<T, LexerImpl, Tracker>::raiseTokenFormatError(T kind, Token token, const char *msg) {
    std::string message(msg);
    message += ": ";
    message += toString(kind);

    this->createError(kind, token, TOKEN_FORMAT, std::move(message));
}

template<typename T, typename LexerImpl, typename Tracker>
void ParserBase<T, LexerImpl, Tracker>::raiseDeepNestingError() {
    std::string message = "parser recursion depth exceeded";
    this->createError(this->curKind, this->curToken, DEEP_NESTING, std::move(message));
}

} //namespace ydsh


#endif //YDSH_MISC_PARSER_BASE_HPP
