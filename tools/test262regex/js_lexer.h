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

#include <misc/enum_util.hpp>
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
  OP(IF, "if")                                                                                     \
  OP(ELSE, "else")                                                                                 \
  OP(ASSIGN, "=")                                                                                  \
  OP(ADD, "+")                                                                                     \
  OP(SUB, "-")                                                                                     \
  OP(NOT, "!")                                                                                     \
  OP(INC, "++")                                                                                    \
  OP(DEC, "--")                                                                                    \
  OP(EQ2, "===")                                                                                   \
  OP(NE2, "!==")                                                                                   \
  OP(LT, "<")                                                                                      \
  OP(LE, "<=")                                                                                     \
  OP(GT, ">")                                                                                      \
  OP(GE, ">=")                                                                                     \
  OP(COND_AND, "&&")                                                                               \
  OP(COND_OR, "||")                                                                                \
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

#define EACH_JS_ASSIGN_OP(OP) OP(ASSIGN, 2, INFIX | RASSOC)

#define EACH_JS_OPERATOR(OP)                                                                       \
  OP(ADD, 11, INFIX)                                                                               \
  OP(SUB, 11, INFIX)                                                                               \
  OP(LT, 9, INFIX)                                                                                 \
  OP(LE, 9, INFIX)                                                                                 \
  OP(GT, 9, INFIX)                                                                                 \
  OP(GE, 9, INFIX)                                                                                 \
  OP(INSTANCEOF, 9, INFIX)                                                                         \
  OP(EQ2, 8, INFIX)                                                                                \
  OP(NE2, 8, INFIX)                                                                                \
  OP(COND_AND, 4, INFIX)                                                                           \
  OP(COND_OR, 3, INFIX)                                                                            \
  EACH_JS_ASSIGN_OP(OP)

enum class JSTokenKind : unsigned char {
#define GEN_TOKEN(T, S) T,
  EACH_JS_TOKEN_KIND(GEN_TOKEN)
#undef GEN_TOKEN
};

inline bool isInvalidToken(JSTokenKind kind) { return kind == JSTokenKind::INVALID; }

inline bool isEOSToken(JSTokenKind kind) { return kind == JSTokenKind::EOS; }

const char *toString(JSTokenKind kind);

enum class JSOperatorAttr : unsigned char {
  NONE = 0u,
  INFIX = 1u << 0u,
  RASSOC = 1u << 1u,
};

enum class JSOperatorPrecedence : unsigned char {};

inline JSOperatorPrecedence advance(JSOperatorPrecedence precedence) {
  return static_cast<JSOperatorPrecedence>(toUnderlying(precedence) + 1);
}

struct JSOperatorInfo {
  JSOperatorPrecedence precedence;
  JSOperatorAttr attr;

  constexpr JSOperatorInfo(unsigned char precedence, JSOperatorAttr attr)
      : precedence(JSOperatorPrecedence{precedence}), attr(attr) {}

  constexpr JSOperatorInfo() : JSOperatorInfo(0, JSOperatorAttr()) {}

  constexpr explicit operator bool() const { return this->precedence != JSOperatorPrecedence{0}; }
};

JSOperatorInfo getOperatorInfo(JSTokenKind kind);

inline bool isOperator(JSTokenKind kind) { return static_cast<bool>(getOperatorInfo(kind)); }

bool isAssignOp(JSTokenKind kind);

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

namespace arsh {

template <>
struct allow_enum_bitop<re262::JSOperatorAttr> : std::true_type {};

} // namespace arsh

#endif // ARSH_TOOLS_TEST262_REGEX_JS_LEXER_H
