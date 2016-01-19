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

#include "token_kind.h"

namespace ydsh {
namespace parser {

const char *toString(TokenKind kind) {
    static const char *table[] = {
#define GEN_NAME(ENUM, STR) STR,
            EACH_TOKEN(GEN_NAME)
#undef GEN_NAME
    };
    return table[kind];
}

unsigned int getPrecedence(TokenKind kind) {
    switch(kind) {
    case IS:
    case AS:
        return 300;
    case MUL:
    case DIV:
    case MOD:
        return 280;
    case PLUS:
    case MINUS:
        return 260;
    case AND:
        return 220;
    case XOR:
        return 200;
    case OR:
        return 180;
    case LA:
    case RA:
    case LE:
    case GE:
    case EQ:
    case NE:
    case MATCH:
    case UNMATCH:
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
