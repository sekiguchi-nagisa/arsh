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

typedef enum {
    INVALID,
    EOS,

    // token kind definition.
    // reserved key word.
    ASSERT,
    BREAK,
    CATCH,
    CLASS,
    CONTINUE,
    DO,
    ELSE,
    EXTENDS,
    EXPORT_ENV,
    FINALLY,
    FOR,
    FUNCTION,
    IF,
    IMPORT_ENV,
    LET,
    NEW,
    NOT,
    RETURN,
    TRY,
    THROW,
    VAR,
    WHILE,

    // unary op
    PLUS,
    MINUS,

    // literal
    INT_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,
    OPEN_DQUOTE,
    BQUOTE_LITERAL,
    START_SUB_CMD,

    // applied name
    APPLIED_NAME,
    SPECIAL_NAME,
    VAR_NAME,

    // bracket
    LP, // (
    RP, // )
    LB, // [
    RB, // ]
    LBC, // {
    RBC, // }
    LA, // <
    RA, // >

    // command
    COMMAND,

    // separator
    COLON,
    COMMA,

    // binary op
    MUL,
    DIV,
    MOD,
    LE,
    GE,
    EQ,
    NE,
    AND,
    OR,
    XOR,
    COND_AND,
    COND_OR,
    RE_MATCH,
    RE_UNMATCH,

    // suffix op
    INC,
    DEC,

    // assign op
    ASSIGN,
    ADD_ASSIGN,
    SUB_ASSIGN,
    MUL_ASSIGN,
    DIV_ASSIGN,
    MOD_ASSIGN,

    // context dependent key word
    AS,
    FUNC,
    IN,
    IS,

    // identifier.
    IDENTIFIER,

    // accessor
    ACCESSOR,

    // line end
    LINE_END,
    NEW_LINE,

    // double quoted string
    CLOSE_DQUOTE,
    STR_ELEMENT,
    START_INTERP,

    // command argument
    CMD_ARG_PART,
    CMD_SEP,
    REDIR_OP,
    REDIR_OP_NO_ARG,
    PIPE,
    BACKGROUND,
    OR_LIST,
    AND_LIST,

} TokenKind;


// binary op alias
//#define ADD PLUS
//#define SUB MINUS
//#define LT LA
//#define GT RA


#endif /* PARSER_TOKENKIND_H_ */
