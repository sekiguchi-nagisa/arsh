/*
 * Copyright (C) 2022 Nagisa Sekiguchi
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

#ifndef ARSH_HIGHLIGHTER_BASE_H
#define ARSH_HIGHLIGHTER_BASE_H

#include <array>

#include "parser.h"

namespace arsh {

#define EACH_HIGHLIGHT_TOKEN_CLASS(OP)                                                             \
  OP(NONE, "none")                                                                                 \
  OP(COMMENT, "comment")                                                                           \
  OP(KEYWORD, "keyword")                                                                           \
  OP(OPERATOR, "operator")                                                                         \
  OP(NUMBER, "number")                                                                             \
  OP(REGEX, "regex")                                                                               \
  OP(STRING, "string")                                                                             \
  OP(COMMAND, "command")                                                                           \
  OP(COMMAND_ARG, "command_arg")                                                                   \
  OP(REDIRECT, "redirect")                                                                         \
  OP(VARIABLE, "variable")                                                                         \
  OP(TYPE, "type")                                                                                 \
  OP(MEMBER, "member")                                                                             \
  OP(ATTRIBUTE, "attribute")                                                                       \
  OP(FOREGROUND_, "foreground") /* pseudo token class for foreground (text) color */               \
  OP(BACKGROUND_, "background") /* pseudo token class for background color */                      \
  OP(LINENO_, "lineno")         /* pseudo token class for line number */

enum class HighlightTokenClass : unsigned char {
#define GEN_ENUM(E, S) E,
  EACH_HIGHLIGHT_TOKEN_CLASS(GEN_ENUM)
#undef GEN_ENUM
};

HighlightTokenClass toTokenClass(TokenKind kind);

using HighlightTokenEntries =
    std::array<std::pair<HighlightTokenClass, const char *>,
               static_cast<unsigned int>(HighlightTokenClass::LINENO_) + 1>;

const HighlightTokenEntries &getHighlightTokenEntries();

class TokenEmitter : public TriviaStore, public TokenTracker {
protected:
  StringRef source;
  LexerPtr lexerPtr; // set after call tokenizeAndEmit()

public:
  explicit TokenEmitter(StringRef source) : source(source) {}

  ~TokenEmitter() override = default;

  [[nodiscard]] StringRef getSource() const { return this->source; }

  const LexerPtr &getLexerPtr() const { return this->lexerPtr; }

  void operator()(TokenKind kind, Token token) override;

  /**
   * for comment
   * @param token
   * must be represent comment
   */
  void operator()(Token token) override;

  /**
   * common entry point of colorize source content
   * @return
   */
  std::unique_ptr<ParseError> tokenizeAndEmit();

private:
  /**
   * actual token emit function
   * @param tokenClass
   * @param token
   */
  virtual void emit(HighlightTokenClass tokenClass, Token token) = 0;
};

} // namespace arsh

#endif // ARSH_HIGHLIGHTER_BASE_H
