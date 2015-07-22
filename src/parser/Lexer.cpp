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

#include <cmath>

#include "../misc/debug.h"
#include "../misc/num.h"
#include "Lexer.h"

namespace ydsh {
namespace parser {

using namespace ydsh::misc;

// ###################
// ##     Lexer     ##
// ###################

const char *Lexer::lexerModeNames[] = {
#define GEN_NAME(ENUM) #ENUM,
        EACH_LEXER_MODE(GEN_NAME)
#undef GEN_NAME
#undef EACH_LEXER_MODE
};

void Lexer::setPos(unsigned int pos) {
    if(this->buf + pos > this->limit) {
        fatal("too large position: %u\n", pos);
    }
    this->cursor = this->buf + pos;
}

Token Lexer::getLineToken(const Token &token, bool skipEOS) const {
    if(skipEOS && token.size == 0) {
        unsigned int startIndex = token.startPos;
        for(; startIndex > 0; startIndex--) {
            char ch = this->buf[startIndex];
            if(ch == ' ' || ch == '\t' || ch == '\n' || ch == '\000') {
                continue;
            }
            if(ch == '\\' && startIndex - 1 > 0 && this->buf[startIndex - 1] == '\n') {
                continue;
            }
            break;
        }
        Token skippedToken;
        skippedToken.startPos = startIndex;
        skippedToken.size = 0;
        return this->getLineTokenImpl(skippedToken);
    }
    return this->getLineTokenImpl(token);
}

Token Lexer::getLineTokenImpl(const Token &token) const {
    assert(this->withinRange(token));

    // find start index of line.
    unsigned int startIndex;
    for(startIndex = token.startPos; startIndex > 0; startIndex--) {
        if(this->buf[startIndex] == '\n') {
            startIndex += (startIndex == token.startPos) ? 0 : 1;
            break;
        }
    }

    // find stop index of line
    unsigned int stopIndex;
    unsigned int usedSize = this->getUsedSize();
    for(stopIndex = token.startPos + token.size; stopIndex < usedSize; stopIndex++) {
        if(this->buf[stopIndex] == '\n') {
            break;
        }
    }
    Token lineToken;
    lineToken.startPos = startIndex;
    lineToken.size = stopIndex - startIndex;
    return lineToken;
}

std::string Lexer::toString(const Token &token, bool isSingleQuote) const {
    assert(this->withinRange(token));

    std::string str;
    str.reserve(token.size);

    unsigned int offset = isSingleQuote ? 1 : 0;
    unsigned int size = token.size - offset;
    for(unsigned int i = offset; i < size; i++) {
        char ch = this->buf[token.startPos + i];
        if(ch == '\\') {    // handle escape sequence
            char nextCh = this->buf[token.startPos + ++i];
            switch(nextCh) {
            case 'b' :
                ch = '\b';
                break;
            case 'f' :
                ch = '\f';
                break;
            case 'n' :
                ch = '\n';
                break;
            case 'r' :
                ch = '\r';
                break;
            case 't' :
                ch = '\t';
                break;
            case '\'':
                ch = '\'';
                break;
            case '"' :
                ch = '"';
                break;
            case '\\':
                ch = '\\';
                break;
            case '`' :
                ch = '`';
                break;
            case '$' :
                ch = '$';
                break;
            default:
                fatal("unexpected escape sequence: %c\n", nextCh);
                break;
            }
        }
        str += ch;
    }
    return str;
}

std::string Lexer::toCmdArg(const Token &token) const {
    assert(this->withinRange(token));

    std::string str;
    str.reserve(token.size);

    for(unsigned int i = 0; i < token.size; i++) {
        char ch = this->buf[token.startPos + i];
        if(ch == '\\') {
            char nextCh = this->buf[token.startPos + ++i];
            switch(nextCh) {
            case '\n':
            case '\r':
                continue;
            default:
                ch = nextCh;
                break;
            }
        }
        str += ch;
    }
    return str;
}

std::string Lexer::toName(const Token &token) const {
    assert(this->withinRange(token));

    std::string name;
    name.reserve(token.size);
    for(unsigned int i = 0; i < token.size; i++) {
        char ch = this->buf[token.startPos + i];
        switch(ch) {
        case '$':
        case '{':
        case '}':
            continue;
        default:
            name += ch;
            break;
        }
    }
    return name;
}

bool Lexer::startswith(const Token &token, char ch) const {
    assert(this->withinRange(token));
    return this->buf[token.startPos] == ch;
}

char Lexer::toInt8(const Token &token, int &status) const {
    long value = this->toInt64(token, status);
    if(value > INT8_MAX || value < INT8_MIN) {
        status = 1;
        return 0;
    }
    return (char) value;
}

unsigned char Lexer::toUint8(const Token &token, int &status) const {
    long value = this->toInt64(token, status);
    if(value > UINT8_MAX || value < 0) {
        status = 1;
        return 0;
    }
    return (unsigned char) value;
}

short Lexer::toInt16(const Token &token, int &status) const {
    long value = this->toInt64(token, status);
    if(value > INT16_MAX || value < INT16_MIN) {
        status = 1;
        return 0;
    }
    return (short) value;
}

unsigned short Lexer::toUint16(const Token &token, int &status) const {
    long value = this->toInt64(token, status);
    if(value > UINT16_MAX || value < 0) {
        status = 1;
        return 0;
    }
    return (unsigned short) value;
}

int Lexer::toInt(const Token &token, int &status) const {
    return this->toInt32(token, status);
}

int Lexer::toInt32(const Token &token, int &status) const {
    long value = this->toInt64(token, status);
    if(value > INT32_MAX || value < INT32_MIN) {
        status = 1;
        return 0;
    }
    return (int) value;
}

unsigned int Lexer::toUint32(const Token &token, int &status) const {
    long value = this->toInt64(token, status);
    if(value > UINT32_MAX || value < 0) {
        status = 1;
        return 0;
    }
    return (unsigned int) value;
}

long Lexer::toInt64(const Token &token, int &status) const {
    assert(this->withinRange(token));

    char str[token.size + 1];
    for(unsigned int i = 0; i < token.size; i++) {
        str[i] = this->buf[token.startPos + i];
    }
    str[token.size] = '\0';

    long value = convertToInt64(str, status, true);
    if(status == -1) {
        fatal("cannot covert to int: %s\n", str);
    } else if(status == -2) {
        fatal("found illegal character in num: %s\n", str);
    }
    return value;
}

unsigned long Lexer::toUint64(const Token &token, int &status) const {
    assert(this->withinRange(token));

    char str[token.size + 1];
    for(unsigned int i = 0; i < token.size; i++) {
        str[i] = this->buf[token.startPos + i];
    }
    str[token.size] = '\0';

    unsigned long value = convertToUint64(str, status, true);
    if(status == -1) {
        fatal("cannot covert to int: %s\n", str);
    } else if(status == -2) {
        fatal("found illegal character in num: %s\n", str);
    }
    return value;
}

double Lexer::toDouble(const Token &token, int &status) const {
    assert(this->withinRange(token));

    char str[token.size + 1];
    for(unsigned int i = 0; i < token.size; i++) {
        str[i] = this->buf[token.startPos + i];
    }
    str[token.size] = '\0';

    double value = convertToDouble(str, status, false);
    if(status == -1) {
        fatal("cannot convert to double: %s\n", str);
    } else if(status == -2) {
        fatal("found illegal character in num: %s\n", str);
    }
    return value;
}

} // namespace parser
} // namespace ydsh