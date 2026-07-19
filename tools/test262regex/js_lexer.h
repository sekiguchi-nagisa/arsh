/*
 * Copyright (C) 2026 Nagisa Sekiguchi
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

#ifndef ARSH_TOOLS_TEST262_REGEX_JS_LEXER_H
#define ARSH_TOOLS_TEST262_REGEX_JS_LEXER_H

#include <optional>

#include <misc/lexer_base.hpp>

namespace arsh::re262 {

#define EACH_JS_TOKEN_KIND(OP)                                                                     \
  OP(INVALID, "<invalid>")                                                                         \
  OP(EOS, "<EOS>")                                                                                 \
  OP(NUMBER, "<Number>")                                                                           \
  OP(STRING, "<String>")                                                                           \
  OP(REGEX, "<RegExp>")                                                                            \
  OP(KEYWORD, "<Keyword>")       /* for reserved keyword (not implemented) */                      \
  OP(IDENTIFIER, "<Identifier>") /* ascii only. `[A-Za-z_$][0-9A-Za-z_$]*` */                      \
  OP(CONST, "const")                                                                               \
  OP(VAR, "var")                                                                                   \
  OP(LET, "let")                                                                                   \
  OP(TRUE, "true")                                                                                 \
  OP(FALSE, "false")                                                                               \
  OP(NIL, "null")                                                                                  \
  OP(NEW, "new")                                                                                   \
  OP(FUNCTION, "function")                                                                         \
  OP(RETURN, "return")                                                                             \
  OP(VOID, "void")                                                                                 \
  OP(TYPEOF, "typeof")                                                                             \
  OP(INSTANCEOF, "instanceof")                                                                     \
  OP(TRY, "try")                                                                                   \
  OP(CATCH, "catch")                                                                               \
  OP(FINALLY, "finally")                                                                           \
  OP(THROW, "throw")                                                                               \
  OP(ASSIGN, "=")                                                                                  \
  OP(ADD, "+")                                                                                     \
  OP(SUB, "-")                                                                                     \
  OP(NOT, "!")                                                                                     \
  OP(LP, "(")                                                                                      \
  OP(RP, ")")                                                                                      \
  OP(LBC, "{")                                                                                     \
  OP(RBC, "}")                                                                                     \
  OP(LB, "[")                                                                                      \
  OP(RB, "]")                                                                                      \
  OP(COLON, ":")                                                                                   \
  OP(LINE_END, ";")                                                                                \
  OP(COMMA, ",")                                                                                   \
  OP(DOT, ".")

enum class JSTokenKind : unsigned char {
#define GEN_TOKEN(T, S) T,
  EACH_JS_TOKEN_KIND(GEN_TOKEN)
#undef GEN_TOKEN
};

inline bool isInvalidToken(JSTokenKind kind) { return kind == JSTokenKind::INVALID; }

inline bool isEOSToken(JSTokenKind kind) { return kind == JSTokenKind::EOS; }

const char *toString(JSTokenKind kind);

class JSLexer : public LexerBase {
private:
  bool prevNewLine{false};
  bool verbose{false};

public:
  JSLexer(const char *sourceName, StringRef src) : LexerBase(sourceName, src.data(), src.size()) {
    this->limit--;
  }

  bool hasPrevNewLine() const { return this->prevNewLine; }

  void setVerbose(bool set) { this->verbose = set; }

  JSTokenKind nextToken(Token &token);

  static std::optional<std::u16string> unquoteString(StringRef ref, std::string *err);

  std::optional<std::u16string> toString(Token token, std::string *err) const {
    return unquoteString(this->toStrRef(token), err);
  }
};

} // namespace arsh::re262

#endif // ARSH_TOOLS_TEST262_REGEX_JS_LEXER_H
