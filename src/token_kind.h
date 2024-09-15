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

#ifndef ARSH_TOKEN_KIND_H
#define ARSH_TOKEN_KIND_H

#include "misc/flag_util.hpp"
#include "misc/string_ref.hpp"

#define EACH_TOKEN(TOKEN)                                                                          \
  TOKEN(INVALID, "<Invalid>")                                                                      \
  TOKEN(EOS, "<EOS>")                                                                              \
  TOKEN(COMPLETION, "<completion>") /* for code completion */                                      \
  /* token kind definition. */                                                                     \
  /* reserved key word. */                                                                         \
  TOKEN(ALIAS, "alias")                                                                            \
  TOKEN(ASSERT, "assert")                                                                          \
  TOKEN(BREAK, "break")                                                                            \
  TOKEN(CASE, "case")                                                                              \
  TOKEN(CATCH, "catch")                                                                            \
  TOKEN(CONTINUE, "continue")                                                                      \
  TOKEN(COPROC, "coproc")                                                                          \
  TOKEN(DEFER, "defer")                                                                            \
  TOKEN(DO, "do")                                                                                  \
  TOKEN(ELIF, "elif")                                                                              \
  TOKEN(ELSE, "else")                                                                              \
  TOKEN(EXPORT_ENV, "exportenv")                                                                   \
  TOKEN(FINALLY, "finally")                                                                        \
  TOKEN(FOR, "for")                                                                                \
  TOKEN(FUNCTION, "function")                                                                      \
  TOKEN(IF, "if")                                                                                  \
  TOKEN(IMPORT_ENV, "importenv")                                                                   \
  TOKEN(INTERFACE, "interface")                                                                    \
  TOKEN(LET, "let")                                                                                \
  TOKEN(NEW, "new")                                                                                \
  TOKEN(RETURN, "return")                                                                          \
  TOKEN(SOURCE, "source")                                                                          \
  TOKEN(SOURCE_OPT, "source?")                                                                     \
  TOKEN(TRY, "try")                                                                                \
  TOKEN(THROW, "throw")                                                                            \
  TOKEN(TIME, "time")                                                                              \
  TOKEN(TYPEDEF, "typedef")                                                                        \
  TOKEN(VAR, "var")                                                                                \
  TOKEN(WHILE, "while")                                                                            \
  /* unary op */                                                                                   \
  TOKEN(PLUS, "+")                                                                                 \
  TOKEN(MINUS, "-")                                                                                \
  TOKEN(NOT, "!")                                                                                  \
  /* literal */                                                                                    \
  TOKEN(INT_LITERAL, "<Int Literal>")                                                              \
  TOKEN(FLOAT_LITERAL, "<Float Literal>")                                                          \
  TOKEN(STRING_LITERAL, "<String Literal>")                                                        \
  TOKEN(UNCLOSED_STRING_LITERAL, "string literal must be closed with '")                           \
  TOKEN(REGEX_LITERAL, "<Regex Literal>")                                                          \
  TOKEN(UNCLOSED_REGEX_LITERAL, "regex literal must be closed with /")                             \
  TOKEN(BACKQUOTE_LITERAL, "<Backquote Literal>")                                                  \
  TOKEN(UNCLOSED_BACKQUOTE_LITERAL, "backquote literal must be closed with `")                     \
  TOKEN(OPEN_DQUOTE, "\"")                                                                         \
  TOKEN(START_SUB_CMD, "$(")                                                                       \
  TOKEN(START_IN_SUB, ">(")                                                                        \
  TOKEN(START_OUT_SUB, "<(")                                                                       \
  TOKEN(AT_PAREN, "@(")                                                                            \
  /* applied name */                                                                               \
  TOKEN(APPLIED_NAME, "<$ Name>")                                                                  \
  TOKEN(SPECIAL_NAME, "<$ Char>")                                                                  \
  TOKEN(PARAM_NAME, "<Parameter>")                                                                 \
  /* bracket */                                                                                    \
  TOKEN(LP, "(")  /* ( */                                                                          \
  TOKEN(RP, ")")  /* ) */                                                                          \
  TOKEN(LB, "[")  /* [ */                                                                          \
  TOKEN(RB, "]")  /* ] */                                                                          \
  TOKEN(LBC, "{") /* { */                                                                          \
  TOKEN(RBC, "}") /* } */                                                                          \
  /* command */                                                                                    \
  TOKEN(COMMAND, "<Command>")                                                                      \
  TOKEN(ENV_ASSIGN, "<EnvName=>")                                                                  \
  /* separator */                                                                                  \
  TOKEN(COLON, ":")                                                                                \
  TOKEN(COMMA, ",")                                                                                \
  /* binary op */                                                                                  \
  TOKEN(ADD, "+")                                                                                  \
  TOKEN(SUB, "-")                                                                                  \
  TOKEN(MUL, "*")                                                                                  \
  TOKEN(DIV, "/")                                                                                  \
  TOKEN(MOD, "%")                                                                                  \
  TOKEN(LT, "<")                                                                                   \
  TOKEN(GT, ">")                                                                                   \
  TOKEN(LE, "<=")                                                                                  \
  TOKEN(GE, ">=")                                                                                  \
  TOKEN(EQ, "==")                                                                                  \
  TOKEN(NE, "!=")                                                                                  \
  TOKEN(AND, "and")                                                                                \
  TOKEN(OR, "or")                                                                                  \
  TOKEN(XOR, "xor")                                                                                \
  TOKEN(LSHIFT, "<<")                                                                              \
  TOKEN(RSHIFT, ">>")                                                                              \
  TOKEN(URSHIFT, ">>>")                                                                            \
  TOKEN(COND_AND, "&&")                                                                            \
  TOKEN(COND_OR, "||")                                                                             \
  TOKEN(NULL_COALE, "??")                                                                          \
  TOKEN(MATCH, "=~")                                                                               \
  TOKEN(UNMATCH, "!~")                                                                             \
  /* ternary op */                                                                                 \
  TOKEN(TERNARY, "?")                                                                              \
  /* suffix op */                                                                                  \
  TOKEN(INC, "++")                                                                                 \
  TOKEN(DEC, "--")                                                                                 \
  TOKEN(UNWRAP, "!")                                                                               \
  /* assign op */                                                                                  \
  TOKEN(ASSIGN, "=")                                                                               \
  TOKEN(ADD_ASSIGN, "+=")                                                                          \
  TOKEN(SUB_ASSIGN, "-=")                                                                          \
  TOKEN(MUL_ASSIGN, "*=")                                                                          \
  TOKEN(DIV_ASSIGN, "/=")                                                                          \
  TOKEN(MOD_ASSIGN, "%=")                                                                          \
  TOKEN(NULL_ASSIGN, "?"                                                                           \
                     "?=") /* suppress -Wtrigraphs */                                              \
  /* for case expression */                                                                        \
  TOKEN(CASE_ARM, "=>")                                                                            \
  /* context dependent key word */                                                                 \
  TOKEN(AS, "as")                                                                                  \
  TOKEN(AS_OPT, "as?")                                                                             \
  TOKEN(FUNC, "Func")                                                                              \
  TOKEN(IN, "in")                                                                                  \
  TOKEN(IS, "is")                                                                                  \
  TOKEN(TYPEOF, "typeof")                                                                          \
  TOKEN(WITH, "with")                                                                              \
  TOKEN(INLINED, "inlined") /* dummy for completion */                                             \
  /* identifier. */                                                                                \
  TOKEN(IDENTIFIER, "<Identifier>")                                                                \
  /* accessor */                                                                                   \
  TOKEN(ACCESSOR, ".")                                                                             \
  /* line end */                                                                                   \
  TOKEN(LINE_END, ";") /* for new line error */                                                    \
  TOKEN(NEW_LINE, "<NewLine>")                                                                     \
  /* double-quoted string */                                                                       \
  TOKEN(CLOSE_DQUOTE, "\"")                                                                        \
  TOKEN(STR_ELEMENT, "<String Element>")                                                           \
  TOKEN(START_INTERP, "${")                                                                        \
  /* command argument */                                                                           \
  TOKEN(CMD_ARG_PART, "<Argument Part>")                                                           \
  TOKEN(APPLIED_NAME_WITH_BRACKET, "<$ Name[>")                                                    \
  TOKEN(SPECIAL_NAME_WITH_BRACKET, "<$ Char[>")                                                    \
  TOKEN(APPLIED_NAME_WITH_PAREN, "<$ Name(>")                                                      \
  TOKEN(APPLIED_NAME_WITH_FIELD, "<$ Name.field>")                                                 \
  TOKEN(GLOB_ANY, "<glob ?>")                                                                      \
  TOKEN(GLOB_ZERO_OR_MORE, "<glob *>")                                                             \
  TOKEN(GLOB_BRACKET_OPEN, "<glob [>")                                                             \
  TOKEN(GLOB_BRACKET_CLOSE, "<glob ]>")                                                            \
  TOKEN(META_ASSIGN, "=")                                                                          \
  TOKEN(META_COLON, ":")                                                                           \
  TOKEN(TILDE, "~")                                                                                \
  TOKEN(BRACE_OPEN, "{")                                                                           \
  TOKEN(BRACE_CLOSE, "}")                                                                          \
  TOKEN(BRACE_SEP, ",")                                                                            \
  TOKEN(BRACE_CHAR_SEQ, "<Brace Char Seq>")                                                        \
  TOKEN(BRACE_INT_SEQ, "<Brace Int Seq>")                                                          \
  /* redir op */                                                                                   \
  TOKEN(REDIR_IN, "<")                                                                             \
  TOKEN(REDIR_OUT, ">")                                                                            \
  TOKEN(REDIR_OUT_CLOBBER, ">|")                                                                   \
  TOKEN(REDIR_APPEND, ">>")                                                                        \
  TOKEN(REDIR_OUT_ERR, "&>")                                                                       \
  TOKEN(REDIR_OUT_ERR_CLOBBER, "&>|")                                                              \
  TOKEN(REDIR_APPEND_OUT_ERR, "&>>")                                                               \
  TOKEN(REDIR_IN_OUT, "<>")                                                                        \
  TOKEN(REDIR_DUP_IN, "<&")                                                                        \
  TOKEN(REDIR_DUP_OUT, ">&")                                                                       \
  TOKEN(REDIR_HERE_DOC, "<<")                                                                      \
  TOKEN(REDIR_HERE_DOC_DASH, "<<-")                                                                \
  TOKEN(REDIR_HERE_STR, "<<<")                                                                     \
  TOKEN(HERE_START, "<Here Doc Start>")                                                            \
  TOKEN(HERE_END, "<Here Doc End>")                                                                \
  TOKEN(PIPE, "|")                                                                                 \
  TOKEN(BACKGROUND, "&")                                                                           \
  TOKEN(DISOWN_BG, "&!")                                                                           \
  /* type  */                                                                                      \
  TOKEN(TYPE_NAME, "<TypeName>")                                                                   \
  TOKEN(TYPE_OPEN, "<")   /* < */                                                                  \
  TOKEN(TYPE_CLOSE, ">")  /* > */                                                                  \
  TOKEN(TYPE_SEP, ",")    /* , */                                                                  \
  TOKEN(TYPE_DOT, ".")    /* . */                                                                  \
  TOKEN(ATYPE_OPEN, "[")  /* [ */                                                                  \
  TOKEN(ATYPE_CLOSE, "]") /* ] */                                                                  \
  TOKEN(PTYPE_OPEN, "(")  /* ( */                                                                  \
  TOKEN(PTYPE_CLOSE, ")") /* ) */                                                                  \
  TOKEN(TYPE_MSEP, ":")   /* : */                                                                  \
  TOKEN(TYPE_OPT, "?")    /* ? */                                                                  \
  TOKEN(TYPE_ARROW, "->") /* -> */                                                                 \
  /* attribute */                                                                                  \
  TOKEN(ATTR_OPEN, "[<")                                                                           \
  TOKEN(ATTR_CLOSE, ">]")                                                                          \
  TOKEN(ATTR_NAME, "<Attribute>")                                                                  \
  TOKEN(ATTR_ASSIGN, ":")

#define EACH_ASSIGN_OPERATOR(OP)                                                                   \
  OP(ASSIGN, 1, INFIX | RASSOC)                                                                    \
  OP(ADD_ASSIGN, 1, INFIX | RASSOC)                                                                \
  OP(SUB_ASSIGN, 1, INFIX | RASSOC)                                                                \
  OP(MUL_ASSIGN, 1, INFIX | RASSOC)                                                                \
  OP(DIV_ASSIGN, 1, INFIX | RASSOC)                                                                \
  OP(MOD_ASSIGN, 1, INFIX | RASSOC)                                                                \
  OP(NULL_ASSIGN, 1, INFIX | RASSOC)

#define EACH_OPERATOR(OP)                                                                          \
  OP(IS, 18, INFIX)                                                                                \
  OP(AS, 18, INFIX)                                                                                \
  OP(AS_OPT, 18, INFIX)                                                                            \
  OP(MUL, 17, INFIX)                                                                               \
  OP(DIV, 17, INFIX)                                                                               \
  OP(MOD, 17, INFIX)                                                                               \
  OP(ADD, 16, INFIX)                                                                               \
  OP(SUB, 16, INFIX)                                                                               \
  OP(LSHIFT, 15, INFIX)                                                                            \
  OP(RSHIFT, 15, INFIX)                                                                            \
  OP(URSHIFT, 15, INFIX)                                                                           \
  OP(AND, 14, INFIX)                                                                               \
  OP(XOR, 13, INFIX)                                                                               \
  OP(OR, 12, INFIX)                                                                                \
  OP(NULL_COALE, 11, INFIX | RASSOC)                                                               \
  OP(LT, 10, INFIX)                                                                                \
  OP(GT, 10, INFIX)                                                                                \
  OP(LE, 10, INFIX)                                                                                \
  OP(GE, 10, INFIX)                                                                                \
  OP(EQ, 10, INFIX)                                                                                \
  OP(NE, 10, INFIX)                                                                                \
  OP(MATCH, 10, INFIX)                                                                             \
  OP(UNMATCH, 10, INFIX)                                                                           \
  OP(WITH, 9, INFIX)                                                                               \
  OP(ENV_ASSIGN, 8, PREFIX)                                                                        \
  OP(PIPE, 7, INFIX)                                                                               \
  OP(COPROC, 6, PREFIX | RASSOC)                                                                   \
  OP(TIME, 6, PREFIX | RASSOC)                                                                     \
  OP(COND_AND, 5, INFIX)                                                                           \
  OP(COND_OR, 4, INFIX)                                                                            \
  OP(TERNARY, 3, INFIX)                                                                            \
  OP(BACKGROUND, 2, INFIX)                                                                         \
  OP(DISOWN_BG, 2, INFIX)                                                                          \
  EACH_ASSIGN_OPERATOR(OP)

#define EACH_INFIX_OPERATOR_KW(OP)                                                                 \
  OP(AS)                                                                                           \
  OP(AS_OPT)                                                                                       \
  OP(IS)                                                                                           \
  OP(AND)                                                                                          \
  OP(OR)                                                                                           \
  OP(XOR)                                                                                          \
  OP(WITH)

// for lookahead
#define EACH_LA_interpolation(OP)                                                                  \
  OP(APPLIED_NAME)                                                                                 \
  OP(SPECIAL_NAME)                                                                                 \
  OP(APPLIED_NAME_WITH_FIELD)                                                                      \
  OP(START_INTERP)

#define EACH_LA_paramExpansion(OP)                                                                 \
  OP(APPLIED_NAME_WITH_BRACKET)                                                                    \
  OP(SPECIAL_NAME_WITH_BRACKET)                                                                    \
  OP(APPLIED_NAME_WITH_PAREN)                                                                      \
  EACH_LA_interpolation(OP)

#define EACH_LA_primary(OP)                                                                        \
  OP(COMMAND)                                                                                      \
  OP(ENV_ASSIGN)                                                                                   \
  OP(NEW)                                                                                          \
  OP(INT_LITERAL)                                                                                  \
  OP(FLOAT_LITERAL)                                                                                \
  OP(STRING_LITERAL)                                                                               \
  OP(REGEX_LITERAL)                                                                                \
  OP(BACKQUOTE_LITERAL)                                                                            \
  OP(OPEN_DQUOTE)                                                                                  \
  OP(START_SUB_CMD)                                                                                \
  OP(APPLIED_NAME)                                                                                 \
  OP(SPECIAL_NAME)                                                                                 \
  OP(START_IN_SUB)                                                                                 \
  OP(START_OUT_SUB)                                                                                \
  OP(AT_PAREN)                                                                                     \
  OP(LP)                                                                                           \
  OP(LB)                                                                                           \
  OP(LBC)                                                                                          \
  OP(DO)                                                                                           \
  OP(FOR)                                                                                          \
  OP(IF)                                                                                           \
  OP(CASE)                                                                                         \
  OP(TRY)                                                                                          \
  OP(WHILE)                                                                                        \
  OP(FUNCTION)                                                                                     \
  OP(BREAK)                                                                                        \
  OP(CONTINUE)                                                                                     \
  OP(RETURN)                                                                                       \
  OP(COMPLETION)

#define EACH_LA_expression(OP)                                                                     \
  OP(NOT)                                                                                          \
  OP(PLUS)                                                                                         \
  OP(MINUS)                                                                                        \
  OP(THROW)                                                                                        \
  OP(COPROC)                                                                                       \
  OP(TIME)                                                                                         \
  EACH_LA_primary(OP)

#define EACH_LA_varDecl(OP)                                                                        \
  OP(VAR)                                                                                          \
  OP(LET)

#define EACH_LA_statement(OP)                                                                      \
  OP(ASSERT)                                                                                       \
  OP(DEFER)                                                                                        \
  OP(EXPORT_ENV)                                                                                   \
  OP(IMPORT_ENV)                                                                                   \
  OP(SOURCE)                                                                                       \
  OP(SOURCE_OPT)                                                                                   \
  OP(TYPEDEF)                                                                                      \
  OP(ATTR_OPEN)                                                                                    \
  OP(LINE_END)                                                                                     \
  EACH_LA_varDecl(OP) EACH_LA_expression(OP)

#define EACH_LA_stringExpression(OP)                                                               \
  OP(STR_ELEMENT)                                                                                  \
  EACH_LA_interpolation(OP) OP(START_SUB_CMD) OP(BACKQUOTE_LITERAL) OP(CLOSE_DQUOTE)

#define EACH_LA_hereExpand(OP)                                                                     \
  OP(STR_ELEMENT) EACH_LA_interpolation(OP) OP(START_SUB_CMD) OP(BACKQUOTE_LITERAL)

#define EACH_LA_redir(OP)                                                                          \
  OP(REDIR_IN)                                                                                     \
  OP(REDIR_OUT)                                                                                    \
  OP(REDIR_OUT_CLOBBER)                                                                            \
  OP(REDIR_APPEND)                                                                                 \
  OP(REDIR_OUT_ERR)                                                                                \
  OP(REDIR_OUT_ERR_CLOBBER)                                                                        \
  OP(REDIR_APPEND_OUT_ERR)                                                                         \
  OP(REDIR_IN_OUT)                                                                                 \
  OP(REDIR_DUP_IN)                                                                                 \
  OP(REDIR_DUP_OUT)                                                                                \
  OP(REDIR_HERE_DOC)                                                                               \
  OP(REDIR_HERE_DOC_DASH)                                                                          \
  OP(REDIR_HERE_STR)

#define EACH_LA_cmdArg(OP)                                                                         \
  OP(CMD_ARG_PART)                                                                                 \
  OP(META_ASSIGN)                                                                                  \
  OP(META_COLON)                                                                                   \
  OP(TILDE)                                                                                        \
  OP(BRACE_CHAR_SEQ)                                                                               \
  OP(BRACE_INT_SEQ)                                                                                \
  OP(GLOB_ANY)                                                                                     \
  OP(GLOB_ZERO_OR_MORE)                                                                            \
  OP(GLOB_BRACKET_OPEN)                                                                            \
  OP(GLOB_BRACKET_CLOSE)                                                                           \
  OP(BRACE_OPEN)                                                                                   \
  OP(BRACE_CLOSE)                                                                                  \
  OP(BRACE_SEP)                                                                                    \
  OP(STRING_LITERAL)                                                                               \
  OP(OPEN_DQUOTE)                                                                                  \
  OP(START_SUB_CMD)                                                                                \
  OP(START_IN_SUB)                                                                                 \
  OP(START_OUT_SUB)                                                                                \
  OP(BACKQUOTE_LITERAL)                                                                            \
  EACH_LA_paramExpansion(OP) OP(COMPLETION)

#define EACH_LA_cmdArg_LP(OP) EACH_LA_cmdArg(OP) OP(LP)

#define EACH_LA_cmdArgs(E) EACH_LA_cmdArg(E) EACH_LA_redir(E)

#define EACH_LA_typeName(OP)                                                                       \
  OP(TYPE_NAME)                                                                                    \
  OP(PTYPE_OPEN)                                                                                   \
  OP(ATYPE_OPEN)                                                                                   \
  OP(FUNC)                                                                                         \
  OP(TYPEOF)

namespace arsh {

enum class TokenKind : unsigned int {
#define GEN_ENUM(ENUM, STR) ENUM,
  EACH_TOKEN(GEN_ENUM)
#undef GEN_ENUM
};

inline bool isInvalidToken(TokenKind kind) { return kind == TokenKind::INVALID; }

inline bool isEOSToken(TokenKind kind) { return kind == TokenKind::EOS; }

inline bool isUnclosedToken(TokenKind kind) {
  switch (kind) {
  case TokenKind::UNCLOSED_STRING_LITERAL:
  case TokenKind::UNCLOSED_BACKQUOTE_LITERAL:
  case TokenKind::UNCLOSED_REGEX_LITERAL:
    return true;
  default:
    return false;
  }
}

const char *toString(TokenKind kind);

// for operator precedence parsing

enum class OperatorAttr : unsigned short {
  INFIX = 1 << 0,
  PREFIX = 1 << 1,
  RASSOC = 1 << 2,
};

template <>
struct allow_enum_bitop<OperatorAttr> : std::true_type {};

struct OperatorInfo {
  unsigned short prece;
  OperatorAttr attr;

  OperatorInfo(unsigned short prece, OperatorAttr attr) : prece(prece), attr(attr) {}

  OperatorInfo() : OperatorInfo(0, OperatorAttr()) {}
};

OperatorInfo getOpInfo(TokenKind kind);

inline unsigned short getPrecedence(TokenKind kind) { return getOpInfo(kind).prece; }

inline OperatorAttr getOpAttr(TokenKind kind) { return getOpInfo(kind).attr; }

bool isAssignOp(TokenKind kind);

inline bool isInfixOp(TokenKind kind) { return hasFlag(getOpAttr(kind), OperatorAttr::INFIX); }

inline bool isRightAssoc(TokenKind kind) { return hasFlag(getOpAttr(kind), OperatorAttr::RASSOC); }

inline bool isInfixKeyword(TokenKind kind) {
  switch (kind) {
#define GEN_CASE(E) case TokenKind::E:
    // clang-format off
  EACH_INFIX_OPERATOR_KW(GEN_CASE)
    // clang-format on
#undef GEN_CASE
  case TokenKind::IN:
  case TokenKind::ELIF:
  case TokenKind::WHILE:
    return true;
  default:
    return false;
  }
}

const char *resolveUnaryOpName(TokenKind op);

const char *resolveBinaryOpName(TokenKind op);

TokenKind resolveAssignOp(TokenKind op);

enum class RedirOp : unsigned char;

std::pair<std::string, RedirOp> resolveRedirOp(TokenKind kind, StringRef ref);

} // namespace arsh

#endif // ARSH_TOKEN_KIND_H
