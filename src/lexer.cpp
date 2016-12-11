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

#include <cmath>

#include "misc/fatal.h"
#include "misc/num.h"
#include "lexer.h"

namespace ydsh {

const char *toModeName(LexerMode mode) {
    switch(mode) {
    case yycSTMT:
        return "STMT";
    case yycEXPR:
        return "EXPR";
    case yycNAME:
        return "NAME";
    case yycTYPE:
        return "TYPE";
    case yycCMD:
        return "CMD";
    case yycDSTRING:
        return "DSTRING";
    }
}

// ########################
// ##     SourceInfo     ##
// ########################

void SourceInfo::addNewlinePos(unsigned int pos) {
    if(this->lineNumTable.empty()) {
        this->lineNumTable.push_back(pos);
    } else if(pos > this->lineNumTable.back()) {
        this->lineNumTable.push_back(pos);
    }
}

unsigned int SourceInfo::getLineNum(unsigned int pos) const {
    auto iter = std::lower_bound(this->lineNumTable.begin(), this->lineNumTable.end(), pos);
    if(this->lineNumTable.end() == iter) {
        return this->lineNumTable.size() + this->lineNumOffset;
    }
    return iter - this->lineNumTable.begin() + this->lineNumOffset;
}


// ###################
// ##     Lexer     ##
// ###################

bool Lexer::singleToString(Token token, std::string &out) const {
    if(this->startsWith(token, '$')) {
        return this->escapedSingleToString(token, out);
    }

    Token trimed = token;
    trimed.pos++;
    trimed.size -= 2;

    out = this->toTokenText(trimed);
    return true;
}

bool Lexer::escapedSingleToString(Token token, std::string &out) const {
    assert(this->withinRange(token));

    out.clear();
    out.reserve(token.size - 3);

    const unsigned int stopPos = token.pos + token.size - 1; // ignore suffix "'"
    for(unsigned int i = token.pos + 2; i < stopPos; i++) {  // ignore prefix "$'"
        char ch = this->buf[i];
        if(ch == '\\' && i + 1 < stopPos) {
            switch(this->buf[++i]) {
            case '\\':
                ch = '\\';
                break;
            case 'a':
                ch = '\a';
                break;
            case 'b':
                ch = '\b';
                break;
            case 'e':
            case 'E':
                ch = '\033';
                break;
            case 'f':
                ch = '\f';
                break;
            case 'n':
                ch = '\n';
                break;
            case 'r':
                ch = '\r';
                break;
            case 't':
                ch = '\t';
                break;
            case 'v':
                ch = '\v';
                break;
            case '\'':
                ch = '\'';
                break;
            case 'x':
                if(i + 1 < stopPos && isHex(this->buf[i + 1])) {
                    int v = toHex(this->buf[++i]);
                    if(i + 1 < stopPos && isHex(this->buf[i + 1])) {
                        v *= 16;
                        v += toHex(this->buf[++i]);
                        ch = (char) v;
                        break;
                    }
                }
                return false;
            default:
                if(isOctal(this->buf[i])) {
                    int v = this->buf[i] - '0';
                    if(i + 1 < stopPos && isOctal(this->buf[i + 1])) {
                        v *= 8;
                        v += this->buf[++i] - '0';
                        if(i + 1 < stopPos && isOctal(this->buf[i + 1])) {
                            v *= 8;
                            v += this->buf[++i] - '0';
                            ch = (char) v;
                            break;
                        }
                    }
                    return false;
                }
                --i;
                break;
            }
        }
        out += ch;
    }
    return true;
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
            case '\n':
                continue;
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
    for(unsigned int i = this->buf[token.pos] == '$' ? 1 : 0; i < token.size; i++) {
        char ch = this->buf[token.pos + i];
        switch(ch) {
        /**
         * ex. $true, ${true}, $@[
         */
        case '{':
        case '}':
        case '[':
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
    return static_cast<unsigned char>(value);
}

short Lexer::toInt16(Token token, int &status) const {
    if(this->isDecimal(token)) {
        long value = this->toInt64(token, status);
        if(value > INT16_MAX || value < INT16_MIN) {
            status = 1;
            return 0;
        }
        return static_cast<short>(value);
    }
    return static_cast<short>(this->toUint16(token, status));
}

unsigned short Lexer::toUint16(Token token, int &status) const {
    long value = this->toInt64(token, status);
    if(value > UINT16_MAX || value < 0) {
        status = 1;
        return 0;
    }
    return static_cast<unsigned short>(value);
}

int Lexer::toInt32(Token token, int &status) const {
    if(this->isDecimal(token)) {
        long value = this->toInt64(token, status);
        if(value > INT32_MAX || value < INT32_MIN) {
            status = 1;
            return 0;
        }
        return static_cast<int>(value);
    }
    return static_cast<int>(this->toUint32(token, status));
}

unsigned int Lexer::toUint32(Token token, int &status) const {
    long value = this->toInt64(token, status);
    if(value > UINT32_MAX || value < 0) {
        status = 1;
        return 0;
    }
    return static_cast<unsigned int>(value);
}

long Lexer::toInt64(Token token, int &status) const {
    if(this->isDecimal(token)) {
        assert(this->withinRange(token));

        char str[token.size + 1];
        for(unsigned int i = 0; i < token.size; i++) {
            str[i] = this->buf[token.pos + i];
        }
        str[token.size] = '\0';

        long value = convertToInt64(str, status, true);
        assert(status > -1);
        return value;
    }
    return static_cast<long>(this->toUint64(token, status));
}

unsigned long Lexer::toUint64(Token token, int &status) const {
    assert(this->withinRange(token));

    char str[token.size + 1];
    for(unsigned int i = 0; i < token.size; i++) {
        str[i] = this->buf[token.pos + i];
    }
    str[token.size] = '\0';

    unsigned long value = convertToUint64(str, status, true);
    assert(status > -1);
    return value;
}

double Lexer::toDouble(Token token, int &status) const {
    assert(this->withinRange(token));

    char str[token.size + 1];
    for(unsigned int i = 0; i < token.size; i++) {
        str[i] = this->buf[token.pos + i];
    }
    str[token.size] = '\0';

    double value = convertToDouble(str, status);
    assert(status > -1);
    return value;
}

bool Lexer::isDecimal(Token token) const {
    assert(this->withinRange(token));
    if(token.size > 2) {    // '0x' or '0o'
        const char *str = reinterpret_cast<char *>(this->buf + token.pos);
        if(str[0] == '0' && (str[1] == 'x' || str[1] == 'o')) {
            return false;
        }
    }
    return true;
}

} // namespace ydsh