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

#include "../misc/fatal.h"
#include "../misc/num.h"
#include "Lexer.h"

namespace ydsh {
namespace parser {

using namespace ydsh::misc;

const char *toModeName(LexerMode mode) {
    static const char *lexerModeNames[] = {
#define GEN_NAME(ENUM) #ENUM,
            EACH_LEXER_MODE(GEN_NAME)
#undef GEN_NAME
#undef EACH_LEXER_MODE
    };

    return lexerModeNames[mode];
}

// ###################
// ##     Lexer     ##
// ###################

void Lexer::setPos(unsigned int pos) {
    if(this->buf + pos > this->limit) {
        fatal("too large position: %u\n", pos);
    }
    this->cursor = this->buf + pos;
}

Token Lexer::getLineToken(Token token, bool skipEOS) const {
    if(skipEOS && token.size == 0) {
        unsigned int startIndex = token.pos;
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
        skippedToken.pos = startIndex;
        skippedToken.size = 0;
        return this->getLineTokenImpl(skippedToken);
    }
    return this->getLineTokenImpl(token);
}

Token Lexer::getLineTokenImpl(Token token) const {
    assert(this->withinRange(token));

    // find start index of line.
    unsigned int startIndex;
    for(startIndex = token.pos; startIndex > 0; startIndex--) {
        if(this->buf[startIndex] == '\n') {
            startIndex += (startIndex == token.pos) ? 0 : 1;
            break;
        }
    }

    // find stop index of line
    unsigned int stopIndex;
    unsigned int usedSize = this->getUsedSize();
    for(stopIndex = token.pos + token.size; stopIndex < usedSize; stopIndex++) {
        if(this->buf[stopIndex] == '\n') {
            break;
        }
    }
    Token lineToken;
    lineToken.pos = startIndex;
    lineToken.size = stopIndex - startIndex;
    return lineToken;
}

std::string Lexer::singleToString(Token token) const {
    if(this->startsWith(token, '$')) {
        return this->escapedSingleToString(token);
    }

    Token trimed = token;
    trimed.pos++;
    trimed.size -= 2;

    return this->toTokenText(trimed);
}

std::string Lexer::escapedSingleToString(Token token) const {
    assert(this->withinRange(token));

    std::string str;
    str.reserve(token.size - 3);

    const unsigned int stopPos = token.pos + token.size - 1; // ignore suffix "'"
    for(unsigned int i = token.pos + 2; i < stopPos; i++) {  // ignore prefix "$'"
        char ch = this->buf[i];
        if(ch == '\\' && i + 1 < stopPos) {
            switch(this->buf[++i]) {
            case 'n':
                ch = '\n';
                break;
            case 'r':
                ch = '\r';
                break;
            case 't':
                ch = '\t';
                break;
            case '\'':
                ch = '\'';
                break;
            case '\\':
                ch = '\\';
                break;
            default:
                --i;
                break;
            }
        }
        str += ch;
    }
    return str;
}

std::string Lexer::doubleElementToString(Token token) const {
    assert(this->withinRange(token));

    std::string str;
    str.reserve(token.size);

    const unsigned int stopPos = token.pos + token.size;
    for(unsigned int i = token.pos; i < stopPos; i++) {
        char ch = this->buf[i];
        if(ch == '\\' && i + 1 < stopPos) {
            char next = this->buf[++i];
            switch(next) {
            case '"':
            case '$':
            case '\\':
                ch = next;
                break;
            default:
                --i;
                break;
            }
        }
        str += ch;
    }
    return str;
}

std::string Lexer::toCmdArg(Token token) const {
    assert(this->withinRange(token));

    std::string str;
    str.reserve(token.size);

    for(unsigned int i = 0; i < token.size; i++) {
        char ch = this->buf[token.pos + i];
        if(ch == '\\') {
            char nextCh = this->buf[token.pos + ++i];
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

std::string Lexer::toName(Token token) const {
    assert(this->withinRange(token));

    std::string name;
    name.reserve(token.size);
    for(unsigned int i = 0; i < token.size; i++) {
        char ch = this->buf[token.pos + i];
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

unsigned char Lexer::toUint8(Token token, int &status) const {
    long value = this->toInt64(token, status);
    if(value > UINT8_MAX || value < 0) {
        status = 1;
        return 0;
    }
    return (unsigned char) value;
}

short Lexer::toInt16(Token token, int &status) const {
    long value = this->toInt64(token, status);
    if(value > INT16_MAX || value < INT16_MIN) {
        status = 1;
        return 0;
    }
    return (short) value;
}

unsigned short Lexer::toUint16(Token token, int &status) const {
    long value = this->toInt64(token, status);
    if(value > UINT16_MAX || value < 0) {
        status = 1;
        return 0;
    }
    return (unsigned short) value;
}

int Lexer::toInt(Token token, int &status) const {
    return this->toInt32(token, status);
}

int Lexer::toInt32(Token token, int &status) const {
    long value = this->toInt64(token, status);
    if(value > INT32_MAX || value < INT32_MIN) {
        status = 1;
        return 0;
    }
    return (int) value;
}

unsigned int Lexer::toUint32(Token token, int &status) const {
    long value = this->toInt64(token, status);
    if(value > UINT32_MAX || value < 0) {
        status = 1;
        return 0;
    }
    return (unsigned int) value;
}

long Lexer::toInt64(Token token, int &status) const {
    assert(this->withinRange(token));

    char str[token.size + 1];
    for(unsigned int i = 0; i < token.size; i++) {
        str[i] = this->buf[token.pos + i];
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

unsigned long Lexer::toUint64(Token token, int &status) const {
    assert(this->withinRange(token));

    char str[token.size + 1];
    for(unsigned int i = 0; i < token.size; i++) {
        str[i] = this->buf[token.pos + i];
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

double Lexer::toDouble(Token token, int &status) const {
    assert(this->withinRange(token));

    char str[token.size + 1];
    for(unsigned int i = 0; i < token.size; i++) {
        str[i] = this->buf[token.pos + i];
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