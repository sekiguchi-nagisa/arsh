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

#include <parser/Token.h>

namespace ydsh {
namespace parser {

static const char *TOKEN_KIND_STRING[] = {
#define GEN_NAME(ENUM) "<" #ENUM ">",
        EACH_TOKEN(GEN_NAME)
#undef GEN_NAME
};

const char *getTokenName(TokenKind kind) {
    return TOKEN_KIND_STRING[kind];
}

// ###################
// ##     Token     ##
// ###################

bool Token::operator==(const Token &token) {
    return this->startPos == token.startPos &&
           this->size == token.size;
}

unsigned int getPrecedence(TokenKind kind) {
    switch(kind) {
    case MUL:
    case DIV:
    case MOD:
        return 300;
    case PLUS:
    case MINUS:
        return 280;
    case LA:
    case RA:
    case LE:
    case GE:
        return 260;
    case IS:
    case AS:
        return 240;
    case EQ:
    case NE:
    case RE_MATCH:
    case RE_UNMATCH:
        return 220;
    case AND:
        return 200;
    case XOR:
        return 180;
    case OR:
        return 160;
    case COND_AND:
        return 140;
    case COND_OR:
        return 120;
    case ASSIGN:
    case ADD_ASSIGN:
    case SUB_ASSIGN:
    case MUL_ASSIGN:
    case DIV_ASSIGN:
    case MOD_ASSIGN:
        return 100;
    default:
        return 0;
    }
}

} // namespace parser
} // namespace ydsh
