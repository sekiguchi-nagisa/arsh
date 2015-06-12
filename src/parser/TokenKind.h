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

#ifndef PARSER_TOKENKIND_H_
#define PARSER_TOKENKIND_H_

#include <ostream>

#include "ParserBase.hpp"

#define EACH_TOKEN(TOKEN) \
    TOKEN(DUMMY) /* for sentinel value. not use it as token kind */\
    TOKEN(INVALID) \
    TOKEN(EOS) \
    /* token kind definition. */\
    /* reserved key word. */\
    TOKEN(ASSERT) \
    TOKEN(BREAK) \
    TOKEN(CATCH) \
    TOKEN(CLASS) \
    TOKEN(CONTINUE) \
    TOKEN(DO) \
    TOKEN(ELIF) \
    TOKEN(ELSE) \
    TOKEN(EXTENDS) \
    TOKEN(EXPORT_ENV) \
    TOKEN(FINALLY) \
    TOKEN(FOR) \
    TOKEN(FUNCTION) \
    TOKEN(IF) \
    TOKEN(IMPORT_ENV) \
    TOKEN(INTERFACE) \
    TOKEN(LET) \
    TOKEN(NEW) \
    TOKEN(NOT) \
    TOKEN(RETURN) \
    TOKEN(TRY) \
    TOKEN(THROW) \
    TOKEN(TYPE_ALIAS) \
    TOKEN(VAR) \
    TOKEN(WHILE) \
    /* unary op */\
    TOKEN(PLUS) \
    TOKEN(MINUS) \
    /* literal */\
    TOKEN(INT_LITERAL) /* equivalent to int32 */\
    TOKEN(BYTE_LITERAL) \
    TOKEN(INT16_LITERAL) \
    TOKEN(UINT16_LITERAL) \
    TOKEN(INT32_LITERAL) \
    TOKEN(UINT32_LITERAL) \
    TOKEN(INT64_LITERAL) \
    TOKEN(UINT64_LITERAL) \
    TOKEN(FLOAT_LITERAL) \
    TOKEN(STRING_LITERAL) \
    TOKEN(PATH_LITERAL) \
    TOKEN(OPEN_DQUOTE) \
    TOKEN(BQUOTE_LITERAL) \
    TOKEN(START_SUB_CMD) \
    /* applied name */\
    TOKEN(APPLIED_NAME) \
    TOKEN(SPECIAL_NAME) \
    TOKEN(VAR_NAME) \
    /* bracket */\
    TOKEN(LP) /* ( */\
    TOKEN(RP) /* ) */\
    TOKEN(LB) /* [ */\
    TOKEN(RB) /* ] */\
    TOKEN(LBC) /* { */\
    TOKEN(RBC) /* } */\
    TOKEN(LA) /* < */\
    TOKEN(RA) /* > */\
    /* command */\
    TOKEN(COMMAND) \
    /* separator */\
    TOKEN(COLON) \
    TOKEN(COMMA) \
    /* binary op */\
    TOKEN(MUL) \
    TOKEN(DIV) \
    TOKEN(MOD) \
    TOKEN(LE) \
    TOKEN(GE) \
    TOKEN(EQ) \
    TOKEN(NE) \
    TOKEN(AND) \
    TOKEN(OR) \
    TOKEN(XOR) \
    TOKEN(COND_AND) \
    TOKEN(COND_OR) \
    TOKEN(RE_MATCH) \
    TOKEN(RE_UNMATCH) \
    /* suffix op */\
    TOKEN(INC) \
    TOKEN(DEC) \
    /* assign op */\
    TOKEN(ASSIGN) \
    TOKEN(ADD_ASSIGN) \
    TOKEN(SUB_ASSIGN) \
    TOKEN(MUL_ASSIGN) \
    TOKEN(DIV_ASSIGN) \
    TOKEN(MOD_ASSIGN) \
    /* context dependent key word */\
    TOKEN(AS) \
    TOKEN(FUNC) \
    TOKEN(IN) \
    TOKEN(IS) \
    /* identifier. */\
    TOKEN(IDENTIFIER) \
    /* accessor */\
    TOKEN(ACCESSOR) \
    /* line end */\
    TOKEN(LINE_END) /* for new line error */\
    TOKEN(NEW_LINE) \
    /* double quoted string */\
    TOKEN(CLOSE_DQUOTE) \
    TOKEN(STR_ELEMENT) \
    TOKEN(START_INTERP) \
    /* command argument */\
    TOKEN(CMD_ARG_PART) \
    TOKEN(CMD_SEP) \
    /* redir op */\
    TOKEN(REDIR_IN_2_FILE) \
    TOKEN(REDIR_OUT_2_FILE) \
    TOKEN(REDIR_OUT_2_FILE_APPEND) \
    TOKEN(REDIR_ERR_2_FILE) \
    TOKEN(REDIR_ERR_2_FILE_APPEND) \
    TOKEN(REDIR_MERGE_ERR_2_OUT_2_FILE) \
    TOKEN(REDIR_MERGE_ERR_2_OUT_2_FILE_APPEND) \
    TOKEN(REDIR_MERGE_ERR_2_OUT) \
    TOKEN(PIPE) \
    TOKEN(BACKGROUND) \
    TOKEN(OR_LIST) \
    TOKEN(AND_LIST) \
    /* type  */\
    TOKEN(TYPE_OPEN) /* < */\
    TOKEN(TYPE_CLOSE) /* > */\
    TOKEN(TYPE_SEP) /* , */\
    TOKEN(PTYPE_OPEN) /* [ */\
    TOKEN(PTYPE_CLOSE) /* ] */\
    TOKEN(TYPE_PATH) /* . */\
    TOKEN(TYPE_OTHER)

namespace ydsh {
namespace parser {

typedef enum {
#define GEN_ENUM(ENUM) ENUM,
    EACH_TOKEN(GEN_ENUM)
#undef GEN_ENUM
} TokenKind;

const char *toString(TokenKind kind);

std::ostream &operator<<(std::ostream &stream, TokenKind kind);

#define TO_NAME(kind) toString(kind)


// binary op alias
//#define ADD PLUS
//#define SUB MINUS
//#define LT LA
//#define GT RA


/**
 * get binary operator precedence.
 */
unsigned int getPrecedence(TokenKind kind);

} // namespace parser
} // namespace ydsh

#endif /* PARSER_TOKENKIND_H_ */
