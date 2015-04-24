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

#ifndef TOOLS_DESCLEXER_H
#define TOOLS_DESCLEXER_H

#include <parser/Lexer.h>

#define EACH_DESC_TOKEN(OP) \
    OP(INVALID) \
    OP(EOS) \
    OP(DESC_PREFIX)    /* //!bind: */\
    OP(FUNC)           /* function */\
    OP(INIT)           /* constructor */\
    OP(STATIC)         /* static */\
    OP(INLINE)         /* inline */\
    OP(BOOL)           /* bool */\
    OP(RCTX)           /* RuntimeContext */\
    OP(ARRAY)          /* Array */\
    OP(MAP)            /* Map */\
    OP(TUPLE)          /* Tuple */\
    OP(AND)            /* & */\
    OP(IDENTIFIER) \
    OP(TYPE_OPEN)      /* < */\
    OP(TYPE_CLOSE)     /* > */\
    OP(VAR_NAME)       /* $ [_a-zA-Z][_a-zA-Z0-9]* */\
    OP(LP)             /* ( */\
    OP(RP)             /* ) */\
    OP(COMMA)          /* , */\
    OP(COLON)          /* : */\
    OP(LBC)            /* { */\
    OP(OPT)            /* ? */


enum DescTokenKind {
#define GEN_ENUM(TOK) TOK,
EACH_DESC_TOKEN(GEN_ENUM)
#undef GEN_ENUM
};

struct DescLexer {
    DescTokenKind operator()(ydsh::parser::Lexer<DescLexer, DescTokenKind > *lexer, ydsh::parser::Token &token) const;
};

const char *getTokenKindName(DescTokenKind kind);

#endif //TOOLS_DESCLEXER_H
