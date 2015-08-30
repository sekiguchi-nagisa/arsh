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

#include <cassert>

#include <misc/num.h>
#include <misc/fatal.h>
#include "directive_lexer.h"

namespace ydsh {
namespace directive {

using namespace ydsh::misc;

static const char *TOKEN_KIND_STR[] = {
#define GEN_STR(ENUM) "<" #ENUM ">",
    EACH_TOKEN(GEN_STR)
#undef GEN_STR
};

const char *toString(TokenKind kind) {
    return TOKEN_KIND_STR[kind];
}

std::ostream &operator<<(std::ostream &stream, TokenKind kind) {
    stream << toString(kind);
    return stream;
}

// ##################
// ##    Lexer     ##
// ##################

int Lexer::toInt(const Token &token, int &status) const {
    long value = this->toInt64(token, status);
    if(value > INT32_MAX || value < INT32_MIN) {
        status = 1;
        return 0;
    }
    return (int) value;
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

std::string Lexer::toString(const Token &token) const {
    assert(this->withinRange(token));

    std::string str;
    str.reserve(token.size);
    unsigned int size = token.size - 1;
    for(unsigned int i = 1; i < size; i++) {
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
            default:
                fatal("unexpected escape sequence: %c\n", nextCh);
                break;
            }
        }
        str += ch;
    }
    return str;
}


} // namespace directive
} // namespace ydsh