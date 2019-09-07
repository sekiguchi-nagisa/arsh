/*
 * Copyright (C) 2015-2018 Nagisa Sekiguchi
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

#include "misc/fatal.h"
#include "misc/num_util.hpp"
#include "misc/hash.hpp"
#include "misc/flag_util.hpp"
#include "lexer.h"

namespace ydsh {

const char *toString(TokenKind kind) {
    const char *table[] = {
#define GEN_NAME(ENUM, STR) STR,
            EACH_TOKEN(GEN_NAME)
#undef GEN_NAME
    };
    return table[kind];
}

OperatorInfo getOpInfo(TokenKind kind) {
#define INFIX OperatorAttr::INFIX
#define PREFIX OperatorAttr::PREFIX
#define RASSOC OperatorAttr::RASSOC

    switch(kind) {
#define GEN_CASE(T, P, A) case T: return {P, A};
    EACH_OPERATOR(GEN_CASE)
#undef GEN_CASE
    default:
        return {};
    }

#undef INFIX
#undef PREFIX
#undef RASSOC
}

bool isAssignOp(TokenKind kind) {
    switch(kind) {
#define GEN_OP(K, P, A) case K: return true;
    EACH_ASSIGN_OPERATOR(GEN_OP)
#undef GEN_OP
    default:
        return false;
    }
}

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
    return nullptr; // normally unreachable, but gcc requires this return statement.
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
                    int v = hexToNum(this->buf[++i]);
                    if(i + 1 < stopPos && isHex(this->buf[i + 1])) {
                        v *= 16;
                        v += hexToNum(this->buf[++i]);
                        ch = static_cast<char>(v);
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
                            ch = static_cast<char>(v);
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

int Lexer::toInt32(Token token, int &status) const {
    auto range = this->getRange(token);
    auto ret = convertToNum<uint32_t>(range.first, range.second);
    status = ret.second ? 0 : 1;
    if(!this->startsWith(token, '0')) { // decimal integer is less than INT32_MAX
        if(ret.second) {
            status = ret.first <= std::numeric_limits<int32_t>::max() ? 0 : 1;
        }
    }
    return static_cast<int>(ret.first);
}

long Lexer::toInt64(Token token, int &status) const {
    // skip suffix [lL]
    token.size--;

    auto range = this->getRange(token);
    auto ret = convertToNum<uint64_t>(range.first, range.second);
    status = ret.second ? 0 : 1;
    if(!this->startsWith(token, '0')) { // decimal integer is less than INT32_MAX
        if(ret.second) {
            status = ret.first <= std::numeric_limits<int64_t>::max() ? 0 : 1;
        }
    }
    return static_cast<long>(ret.first);
}

double Lexer::toDouble(Token token, int &status) const {
    assert(this->withinRange(token));
    double value = convertToDouble(this->toTokenText(token).c_str(), status);
    assert(status > -1);
    return value;
}

} // namespace ydsh