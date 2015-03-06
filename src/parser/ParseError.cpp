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

#include <parser/ParseError.h>

// ########################
// ##     ParseError     ##
// ########################

ParseError::ParseError(unsigned int lineNum, TokenKind errorToken) :
        lineNum(lineNum), errorToken(errorToken) {
}

ParseError::~ParseError() {
}

unsigned int ParseError::getLineNum() {
    return this->lineNum;
}

TokenKind ParseError::getErrorToken() {
    return this->errorToken;
}

bool ParseError::operator==(const ParseError &e) {
    return this->equalsImpl(e);
}

bool ParseError::baseEquals(const ParseError &e) {
    return this->lineNum == e.lineNum && this->errorToken == e.errorToken;
}

// ################################
// ##     TokenMismatchError     ##
// ################################

TokenMismatchError::TokenMismatchError(unsigned int lineNum, TokenKind actual, TokenKind expected) :
        ParseError(lineNum, actual), expected(expected) {
}

TokenMismatchError::~TokenMismatchError() {
}

TokenKind TokenMismatchError::getExpectedToken() {
    return this->expected;
}

bool TokenMismatchError::operator==(const TokenMismatchError &e) {
    if(!this->baseEquals(e)) {
        return false;
    }
    return this->expected == e.expected;
}

bool TokenMismatchError::equalsImpl(const ParseError &e) {
    const TokenMismatchError *ex = dynamic_cast<const TokenMismatchError*>(&e);
    if(ex != 0) {
        return *this == *ex;
    }
    return false;
}

// ################################
// ##     NoViableAlterError     ##
// ################################

NoViableAlterError::NoViableAlterError(unsigned int lineNum, TokenKind actual, TokenKind *alters) :
        ParseError(lineNum, actual), alters(), alterSize(0) {
    for(unsigned int i = 0; alters[i] != DUMMY; i++) {
        ++this->alterSize;
    }

    this->alters = new TokenKind[this->alterSize];
    for(unsigned int i = 0; i < this->alterSize; i++) {
        this->alters[i] = alters[i];
    }
}

NoViableAlterError::~NoViableAlterError() {
    delete this->alters;
    this->alters = 0;
}

TokenKind *NoViableAlterError::getAlters() {
    return this->alters;
}

unsigned int NoViableAlterError::getAlterSize() {
    return this->alterSize;
}

bool NoViableAlterError::operator==(const NoViableAlterError &e) {
    if(!this->baseEquals(e)) {
        return false;
    }

    // check alterSize
    if(this->alterSize != e.alterSize) {
        return false;
    }

    // check each alter
    for(unsigned int i = 0; i < this->alterSize; i++) {
        if(this->alters[i] != e.alters[i]) {
            return false;
        }
    }
    return true;
}

bool NoViableAlterError::equalsImpl(const ParseError &e) {
    const NoViableAlterError *ex = dynamic_cast<const NoViableAlterError*>(&e);
    if(ex != 0) {
        return *this == *ex;
    }
    return false;
}

// ###############################
// ##     InvalidTokenError     ##
// ###############################

InvalidTokenError::InvalidTokenError(unsigned int lineNum, Token token) :
        ParseError(lineNum, INVALID), token(token) {
}

InvalidTokenError::~InvalidTokenError() {
}

Token InvalidTokenError::getInvalidToken() {
    return this->token;
}

bool InvalidTokenError::operator==(const InvalidTokenError &e) {
    if(!this->baseEquals(e)) {
        return false;
    }
    return this->token == e.token;
}

bool InvalidTokenError::equalsImpl(const ParseError &e) {
    const InvalidTokenError *ex = dynamic_cast<const InvalidTokenError*>(&e);
    if(ex != 0) {
        return *this == *ex;
    }
    return false;
}

