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

template<typename T, typename LexerImpl, typename EListener>
class ParserBase {
protected:
    LexerImpl *lexer;
    T curKind;
    Token curToken;

public:
    ParserBase() = default;

protected:
    ~ParserBase() = default;

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

template<typename T, typename LexerImpl, typename EListener>
Token ParserBase<T, LexerImpl, EListener>::expect(T kind, bool fetchNext) {
    if(this->curKind != kind) {
        if(LexerImpl::isInvalidToken(this->curKind)) {
            EListener::reportInvalidTokenError(this->curKind, this->curToken);
        }
        EListener::reportTokenMismatchedError(this->curKind, this->curToken, kind);
    }
    Token token = this->curToken;
    if(fetchNext) {
        this->fetchNext();
    }
    return token;
}

template<typename T, typename LexerImpl, typename EListener>
T ParserBase<T, LexerImpl, EListener>::consume() {
    T kind = this->curKind;
    this->fetchNext();
    return kind;
}

template<typename T, typename LexerImpl, typename EListener>
void ParserBase<T, LexerImpl, EListener>::alternativeError(std::vector<T> &&alters) {
    if(LexerImpl::isInvalidToken(this->curKind)) {
        EListener::reportInvalidTokenError(this->curKind, this->curToken);
    }
    EListener::reportNoViableAlterError(this->curKind, this->curToken, std::move(alters));
}

} //namespace parser_base
} //namespace ydsh


#endif //YDSH_PARSERBASE_HPP
