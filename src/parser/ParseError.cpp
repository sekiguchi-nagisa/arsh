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

#include "ParseError.h"

namespace ydsh {
namespace parser {

// ########################
// ##     ParseError     ##
// ########################

ParseError::ParseError(unsigned int lineNum, TokenKind kind, Token errorToken) :
        lineNum(lineNum), kind(kind), errorToken(errorToken) {
}

unsigned int ParseError::getLineNum() const {
    return this->lineNum;
}

TokenKind ParseError::getTokenKind() const {
    return this->kind;
}

Token ParseError::getErrorToken() const {
    return this->errorToken;
}

bool ParseError::operator==(const ParseError &e) {
    return this->equalsImpl(e);
}

bool ParseError::baseEquals(const ParseError &e) {
    return this->lineNum == e.lineNum &&
           this->kind == e.kind && this->errorToken == e.errorToken;
}

// ################################
// ##     TokenMismatchError     ##
// ################################

TokenMismatchError::TokenMismatchError(unsigned int lineNum, TokenKind actual,
                                       Token errorToken, TokenKind expected) :
        ParseError(lineNum, actual, errorToken), expected(expected) {
}

TokenKind TokenMismatchError::getExpectedTokenKind() const {
    return this->expected;
}

bool TokenMismatchError::operator==(const TokenMismatchError &e) {
    return this->baseEquals(e) && this->expected == e.expected;
}

void TokenMismatchError::accept(ParseErrorVisitor &visitor) const {
    visitor.visit(*this);
}

bool TokenMismatchError::equalsImpl(const ParseError &e) {
    const TokenMismatchError *ex = dynamic_cast<const TokenMismatchError *>(&e);
    return ex != nullptr && *this == *ex;
}

// ################################
// ##     NoViableAlterError     ##
// ################################

NoViableAlterError::NoViableAlterError(unsigned int lineNum, TokenKind actual,
                                       Token errorToken, TokenKind *alters) :
        ParseError(lineNum, actual, errorToken), alters() {
    for(unsigned int i = 0; alters[i] != DUMMY; i++) {
        this->alters.push_back(alters[i]);
    }
}

const std::vector<TokenKind> &NoViableAlterError::getAlters() const {
    return this->alters;
}

bool NoViableAlterError::operator==(const NoViableAlterError &e) {
    if(!this->baseEquals(e)) {
        return false;
    }

    // check alterSize
    unsigned int size = this->alters.size();
    if(size != e.alters.size()) {
        return false;
    }

    // check each alter
    for(unsigned int i = 0; i < size; i++) {
        if(this->alters[i] != e.alters[i]) {
            return false;
        }
    }
    return true;
}

void NoViableAlterError::accept(ParseErrorVisitor &visitor) const {
    visitor.visit(*this);
}

bool NoViableAlterError::equalsImpl(const ParseError &e) {
    const NoViableAlterError *ex = dynamic_cast<const NoViableAlterError *>(&e);
    return ex != nullptr && *this == *ex;
}

// ###############################
// ##     InvalidTokenError     ##
// ###############################

InvalidTokenError::InvalidTokenError(unsigned int lineNum, Token token) :
        ParseError(lineNum, INVALID, token) {
}

bool InvalidTokenError::operator==(const InvalidTokenError &e) {
    return this->baseEquals(e);
}

void InvalidTokenError::accept(ParseErrorVisitor &visitor) const {
    visitor.visit(*this);
}

bool InvalidTokenError::equalsImpl(const ParseError &e) {
    const InvalidTokenError *ex = dynamic_cast<const InvalidTokenError *>(&e);
    return ex != nullptr && *this == *ex;
}

// ################################
// ##     OutOfRangeNumError     ##
// ################################

OutOfRangeNumError::OutOfRangeNumError(unsigned int lineNum, TokenKind kind, Token token) :
        ParseError(lineNum, kind, token) {
}

bool OutOfRangeNumError::operator==(const OutOfRangeNumError &e) {
    return this->baseEquals(e);
}

void OutOfRangeNumError::accept(ParseErrorVisitor &visitor) const {
    return visitor.visit(*this);
}

bool OutOfRangeNumError::equalsImpl(const ParseError &e) {
    const OutOfRangeNumError *ex = dynamic_cast<const OutOfRangeNumError *>(&e);
    return ex != nullptr && *this == *ex;
}

} // namespace parser
} // namespace ydsh