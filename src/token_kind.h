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

#ifndef YDSH_TOKEN_KIND_H
#define YDSH_TOKEN_KIND_H

#define EACH_TOKEN(TOKEN) \
    TOKEN(INVALID                            , "<Invalid>") \
    TOKEN(EOS                                , "<EOS>") \
    /* token kind definition. */\
    /* reserved key word. */\
    TOKEN(ALIAS                              , "alias") \
    TOKEN(ASSERT                             , "assert") \
    TOKEN(BREAK                              , "break") \
    TOKEN(CASE                               , "case") \
    TOKEN(CATCH                              , "catch") \
    TOKEN(CLASS                              , "class") \
    TOKEN(CONTINUE                           , "continue") \
    TOKEN(COPROC                             , "coproc") \
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
    TOKEN(RETURN                             , "return") \
    TOKEN(SOURCE                             , "source") \
    TOKEN(TRY                                , "try") \
    TOKEN(THROW                              , "throw") \
    TOKEN(VAR                                , "var") \
    TOKEN(WHILE                              , "while") \
    /* unary op */\
    TOKEN(PLUS                               , "+") \
    TOKEN(MINUS                              , "-") \
    TOKEN(NOT                                , "!") \
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
    TOKEN(REGEX_LITERAL                      , "<Regex Literal>") \
    TOKEN(SIGNAL_LITERAL                     , "<Signal Literal>") \
    TOKEN(OPEN_DQUOTE                        , "\"") \
    TOKEN(START_SUB_CMD                      , "$(") \
    TOKEN(START_IN_SUB                       , ">(") \
    TOKEN(START_OUT_SUB                      , "<(") \
    /* applied name */\
    TOKEN(APPLIED_NAME                       , "<$ Name>") \
    TOKEN(SPECIAL_NAME                       , "<$ Char>") \
    /* bracket */\
    TOKEN(LP                                 , "(") /* ( */\
    TOKEN(RP                                 , ")") /* ) */\
    TOKEN(LB                                 , "[") /* [ */\
    TOKEN(RB                                 , "]") /* ] */\
    TOKEN(LBC                                , "{") /* { */\
    TOKEN(RBC                                , "}") /* } */\
    /* command */\
    TOKEN(COMMAND                            , "<Command>") \
    /* separator */\
    TOKEN(COLON                              , ":") \
    TOKEN(COMMA                              , ",") \
    /* binary op */\
    TOKEN(ADD                                , "+") \
    TOKEN(SUB                                , "-") \
    TOKEN(MUL                                , "*") \
    TOKEN(DIV                                , "/") \
    TOKEN(MOD                                , "%") \
    TOKEN(LT                                 , "<") \
    TOKEN(GT                                 , ">") \
    TOKEN(LE                                 , "<=") \
    TOKEN(GE                                 , ">=") \
    TOKEN(EQ                                 , "==") \
    TOKEN(NE                                 , "!=") \
    TOKEN(AND                                , "and") \
    TOKEN(OR                                 , "or") \
    TOKEN(XOR                                , "xor") \
    TOKEN(COND_AND                           , "&&") \
    TOKEN(COND_OR                            , "||") \
    TOKEN(NULL_COALE                         , "??") \
    TOKEN(MATCH                              , "=~") \
    TOKEN(UNMATCH                            , "!~") \
    /* ternary op */\
    TOKEN(TERNARY                            , "?") \
    /* suffix op */\
    TOKEN(INC                                , "++") \
    TOKEN(DEC                                , "--") \
    TOKEN(UNWRAP                             , "!") \
    /* assign op */\
    TOKEN(ASSIGN                             , "=") \
    TOKEN(ADD_ASSIGN                         , "+=") \
    TOKEN(SUB_ASSIGN                         , "-=") \
    TOKEN(MUL_ASSIGN                         , "*=") \
    TOKEN(DIV_ASSIGN                         , "/=") \
    TOKEN(MOD_ASSIGN                         , "%=") \
    /* for case expression */\
    TOKEN(CASE_ARM                           , "=>") \
    /* context dependent key word */\
    TOKEN(AS                                 , "as") \
    TOKEN(FUNC                               , "Func") \
    TOKEN(IN                                 , "in") \
    TOKEN(IS                                 , "is") \
    TOKEN(TYPEOF                             , "typeof") \
    TOKEN(WITH                               , "with") \
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
    TOKEN(REDIR_HERE_STR                     , "<<<") \
    TOKEN(PIPE                               , "|") \
    TOKEN(BACKGROUND                         , "&") \
    TOKEN(DISOWN_BG                          , "&!") \
    /* type  */\
    TOKEN(TYPE_OPEN                          , "<") /* < */\
    TOKEN(TYPE_CLOSE                         , ">") /* > */\
    TOKEN(TYPE_SEP                           , ",") /* , */\
    TOKEN(ATYPE_OPEN                         , "[") /* [ */\
    TOKEN(ATYPE_CLOSE                        , "]") /* ] */\
    TOKEN(PTYPE_OPEN                         , "(") /* ( */\
    TOKEN(PTYPE_CLOSE                        , ")") /* ) */\
    TOKEN(TYPE_MSEP                          , ":") /* : */\
    TOKEN(TYPE_OPT                           , "!") /* ! */

namespace ydsh {

enum TokenKind : unsigned int {
#define GEN_ENUM(ENUM, STR) ENUM,
    EACH_TOKEN(GEN_ENUM)
#undef GEN_ENUM
};

inline bool isInvalidToken(TokenKind kind) {
    return kind == INVALID;
}

const char *toString(TokenKind kind);

#define TO_NAME(kind) toString(kind)


// binary op alias
//#define LT LA
//#define GT RA


/**
 * get binary operator precedence.
 */
unsigned int getPrecedence(TokenKind kind);

bool isAssignOp(TokenKind kind);

} // namespace ydsh

#endif //YDSH_TOKEN_KIND_H
