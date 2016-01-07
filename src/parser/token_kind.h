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

#ifndef YDSH_PARSER_TOKEN_KIND_H
#define YDSH_PARSER_TOKEN_KIND_H

#include <ostream>

#define EACH_TOKEN(TOKEN) \
    TOKEN(DUMMY                              , "<Dummy>") /* for sentinel value. not use it as token kind */\
    TOKEN(INVALID                            , "<Invalid>") \
    TOKEN(EOS                                , "<EOS>") \
    /* token kind definition. */\
    /* reserved key word. */\
    TOKEN(ASSERT                             , "assert") \
    TOKEN(BREAK                              , "break") \
    TOKEN(CATCH                              , "catch") \
    TOKEN(CLASS                              , "class") \
    TOKEN(CONTINUE                           , "continue") \
    TOKEN(DO                                 , "do") \
    TOKEN(ELIF                               , "elif") \
    TOKEN(ELSE                               , "else") \
    TOKEN(EXPORT_ENV                         , "export-env") \
    TOKEN(FINALLY                            , "finally") \
    TOKEN(FOR                                , "for") \
    TOKEN(FUNCTION                           , "function") \
    TOKEN(IF                                 , "if") \
    TOKEN(IMPORT_ENV                         , "import-env") \
    TOKEN(INTERFACE                          , "interface") \
    TOKEN(LET                                , "let") \
    TOKEN(NEW                                , "new") \
    TOKEN(NOT                                , "not") \
    TOKEN(RETURN                             , "return") \
    TOKEN(TRY                                , "try") \
    TOKEN(THROW                              , "throw") \
    TOKEN(TYPE_ALIAS                         , "type-alias") \
    TOKEN(VAR                                , "var") \
    TOKEN(WHILE                              , "while") \
    /* unary op */\
    TOKEN(PLUS                               , "+") \
    TOKEN(MINUS                              , "-") \
    /* literal */\
    TOKEN(BYTE_LITERAL                       , "<Byte Literal>") \
    TOKEN(INT16_LITERAL                      , "<Int16 Literal>") \
    TOKEN(UINT16_LITERAL                     , "<Uint16 Literal>") \
    TOKEN(INT32_LITERAL                      , "<Int32 Literal>") \
    TOKEN(UINT32_LITERAL                     , "<Uint32 Literal>") \
    TOKEN(INT64_LITERAL                      , "<Int64 Literal>") \
    TOKEN(UINT64_LITERAL                     , "<Uint64 Literal>") \
    TOKEN(FLOAT_LITERAL                      , "<Float Literal>") \
    TOKEN(STRING_LITERAL                     , "<String Literal>") \
    TOKEN(PATH_LITERAL                       , "<Path Literal>") \
    TOKEN(OPEN_DQUOTE                        , "\"") \
    TOKEN(START_SUB_CMD                      , "$(") \
    /* applied name */\
    TOKEN(APPLIED_NAME                       , "<$ Name>") \
    TOKEN(SPECIAL_NAME                       , "<$ Char>") \
    TOKEN(VAR_NAME                           , "<Variable Name>") \
    /* bracket */\
    TOKEN(LP                                 , "(") /* ( */\
    TOKEN(RP                                 , ")") /* ) */\
    TOKEN(LB                                 , "[") /* [ */\
    TOKEN(RB                                 , "]") /* ] */\
    TOKEN(LBC                                , "{") /* { */\
    TOKEN(RBC                                , "}") /* } */\
    TOKEN(LA                                 , "<") /* < */\
    TOKEN(RA                                 , ">") /* > */\
    /* command */\
    TOKEN(COMMAND                            , "<Command>") \
    /* separator */\
    TOKEN(COLON                              , ":") \
    TOKEN(COMMA                              , ",") \
    /* binary op */\
    TOKEN(MUL                                , "*") \
    TOKEN(DIV                                , "/") \
    TOKEN(MOD                                , "%") \
    TOKEN(LE                                 , "<=") \
    TOKEN(GE                                 , ">=") \
    TOKEN(EQ                                 , "==") \
    TOKEN(NE                                 , "!=") \
    TOKEN(AND                                , "-and") \
    TOKEN(OR                                 , "-or") \
    TOKEN(XOR                                , "-xor") \
    TOKEN(COND_AND                           , "&&") \
    TOKEN(COND_OR                            , "||") \
    TOKEN(MATCH                              , "=~") \
    TOKEN(UNMATCH                            , "!~") \
    /* suffix op */\
    TOKEN(INC                                , "++") \
    TOKEN(DEC                                , "--") \
    /* assign op */\
    TOKEN(ASSIGN                             , "=") \
    TOKEN(ADD_ASSIGN                         , "+=") \
    TOKEN(SUB_ASSIGN                         , "-=") \
    TOKEN(MUL_ASSIGN                         , "*=") \
    TOKEN(DIV_ASSIGN                         , "/=") \
    TOKEN(MOD_ASSIGN                         , "%=") \
    /* context dependent key word */\
    TOKEN(AS                                 , "as") \
    TOKEN(FUNC                               , "Func") \
    TOKEN(IN                                 , "in") \
    TOKEN(IS                                 , "is") \
    TOKEN(TYPEOF                             , "typeof") \
    /* identifier. */\
    TOKEN(IDENTIFIER                         , "<Identifier>") \
    /* accessor */\
    TOKEN(ACCESSOR                           , ".") \
    /* line end */\
    TOKEN(LINE_END                           , ";") /* for new line error */\
    TOKEN(NEW_LINE                           , "<NewLine>") \
    /* double quoted string */\
    TOKEN(CLOSE_DQUOTE                       , "\"") \
    TOKEN(STR_ELEMENT                        , "<String Element>") \
    TOKEN(START_INTERP                       , "${") \
    /* command argument */\
    TOKEN(CMD_ARG_PART                       , "<Argument Part>") \
    TOKEN(APPLIED_NAME_WITH_BRACKET          , "<$ Name[>") \
    TOKEN(SPECIAL_NAME_WITH_BRACKET          , "<$ Char[>") \
    /* redir op */\
    TOKEN(REDIR_IN_2_FILE                    , "<") \
    TOKEN(REDIR_OUT_2_FILE                   , "1>") \
    TOKEN(REDIR_OUT_2_FILE_APPEND            , "1>>") \
    TOKEN(REDIR_ERR_2_FILE                   , "2>") \
    TOKEN(REDIR_ERR_2_FILE_APPEND            , "2>>") \
    TOKEN(REDIR_MERGE_ERR_2_OUT_2_FILE       , "&>") \
    TOKEN(REDIR_MERGE_ERR_2_OUT_2_FILE_APPEND, "&>>") \
    TOKEN(REDIR_MERGE_ERR_2_OUT              , "2>&1") \
    TOKEN(REDIR_MERGE_OUT_2_ERR              , "1>&2") \
    TOKEN(PIPE                               , "|") \
    TOKEN(BACKGROUND                         , "&") \
    /* type  */\
    TOKEN(TYPE_OPEN                          , "<") /* < */\
    TOKEN(TYPE_CLOSE                         , ">") /* > */\
    TOKEN(TYPE_SEP                           , ",") /* , */\
    TOKEN(PTYPE_OPEN                         , "[") /* [ */\
    TOKEN(PTYPE_CLOSE                        , "]") /* ] */\
    TOKEN(TYPE_PATH                          , ".") /* . */\
    TOKEN(TYPE_OTHER                         , "<Type Other>")

namespace ydsh {
namespace parser {

enum TokenKind : unsigned int {
#define GEN_ENUM(ENUM, STR) ENUM,
    EACH_TOKEN(GEN_ENUM)
#undef GEN_ENUM
};

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

#endif //YDSH_PARSER_TOKEN_KIND_H
