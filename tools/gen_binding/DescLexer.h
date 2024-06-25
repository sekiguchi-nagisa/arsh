/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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

#ifndef ARSH_TOOLS_GEN_BINDING_DESCLEXER_H
#define ARSH_TOOLS_GEN_BINDING_DESCLEXER_H

#include <ostream>

#include <misc/lexer_base.hpp>

#define EACH_DESC_TOKEN(OP)                                                                        \
  OP(INVALID)                                                                                      \
  OP(EOS)                                                                                          \
  OP(DESC_PREFIX)      /* //!bind: */                                                              \
  OP(FUNC)             /* function */                                                              \
  OP(ARSH_METHOD)      /* ARSH_METHOD */                                                           \
  OP(ARSH_METHOD_DECL) /* ARSH_METHOD_DECL */                                                      \
  OP(RCTX)             /* RuntimeContext */                                                        \
  OP(WHERE)            /* where */                                                                 \
  OP(AND)              /* & */                                                                     \
  OP(IDENTIFIER)                                                                                   \
  OP(TYPE_OPEN)  /* < */                                                                           \
  OP(TYPE_CLOSE) /* > */                                                                           \
  OP(PTYPE_OPEN) /* [ */                                                                           \
  OP(PTYPE_CLOSE /* ] */)                                                                          \
  OP(VAR_NAME)   /* $ [_a-zA-Z][_a-zA-Z0-9]* */                                                    \
  OP(LP)         /* ( */                                                                           \
  OP(RP)         /* ) */                                                                           \
  OP(COMMA)      /* , */                                                                           \
  OP(COLON)      /* : */                                                                           \
  OP(SEMI_COLON) /* ; */                                                                           \
  OP(LBC)        /* { */

enum class DescTokenKind : unsigned char {
#define GEN_ENUM(TOK) TOK,
  EACH_DESC_TOKEN(GEN_ENUM)
#undef GEN_ENUM
};

using Token = arsh::Token;

class DescLexer : public arsh::LexerBase {
public:
  explicit DescLexer(const char *line) : LexerBase("", line) {}
  ~DescLexer() = default;

  DescTokenKind nextToken(Token &token);
};

inline bool isInvalidToken(DescTokenKind kind) { return kind == DescTokenKind::INVALID; }

inline bool isEOSToken(DescTokenKind kind) { return kind == DescTokenKind::EOS; }

const char *toString(DescTokenKind kind);

#endif // ARSH_TOOLS_GEN_BINDING_DESCLEXER_H
