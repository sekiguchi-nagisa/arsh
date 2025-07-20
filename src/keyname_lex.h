/*
 * Copyright (C) 2025 Nagisa Sekiguchi
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

#ifndef ARSH_KEYNAME_LEX_H
#define ARSH_KEYNAME_LEX_H

#include "misc/lexer_base.hpp"

namespace arsh {

#define EACH_KEYNAME_TOKEN(OP)                                                                     \
  OP(INVALID, "<invalid>")                                                                         \
  OP(EOS, "<EOS>")                                                                                 \
  OP(ASCII_CHAR, "<AsciiChar>")                                                                    \
  OP(PLUS, "+")                                                                                    \
  OP(MINUS, "-")                                                                                   \
  OP(IDENTIFIER, "<Identifier>")

enum class KeyNameTokenKind : unsigned char {
#define GEN_ENUM(E, S) E,
  EACH_KEYNAME_TOKEN(GEN_ENUM)
#undef GEN_ENUM
};

// const char *toString(KeyNameTokenKind kind);

class KeyNameLexer : public LexerBase {
public:
  explicit KeyNameLexer(StringRef ref) : LexerBase("(string)", ref.data(), ref.size()) {}

  KeyNameTokenKind nextToken(Token &token);
};

} // namespace arsh

#endif // ARSH_KEYNAME_LEX_H
