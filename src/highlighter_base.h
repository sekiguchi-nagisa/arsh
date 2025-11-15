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

#include "misc/array_ref.hpp"
#include "parser.h"

namespace arsh {

#define EACH_HIGHLIGHT_TOKEN_CLASS(OP)                                                             \
  OP(NONE_, "none") /* pseudo token class for null color */                                        \
  OP(COMMENT, "comment")                                                                           \
  OP(KEYWORD, "keyword")                                                                           \
  OP(OPERATOR, "operator")                                                                         \
  OP(NUMBER, "number")                                                                             \
  OP(REGEX, "regex")                                                                               \
  OP(STRING, "string")                                                                             \
  OP(COMMAND, "command")                                                                           \
  OP(COMMAND_ARG, "command_arg")                                                                   \
  OP(META, "meta")                                                                                 \
  OP(REDIRECT, "redirect")                                                                         \
  OP(VARIABLE, "variable")                                                                         \
  OP(TYPE, "type")                                                                                 \
  OP(MEMBER, "member")                                                                             \
  OP(ATTRIBUTE, "attribute")                                                                       \
  OP(ERROR_, "error")           /* pseudo token class for error */                                 \
  OP(FOREGROUND_, "foreground") /* pseudo token class for foreground (text) color */               \
  OP(BACKGROUND_, "background") /* pseudo token class for background color */                      \
  OP(LINENO_, "lineno")         /* pseudo token class for line number */

enum class HighlightTokenClass : unsigned char {
#define GEN_ENUM(E, S) E,
  EACH_HIGHLIGHT_TOKEN_CLASS(GEN_ENUM)
#undef GEN_ENUM
};

HighlightTokenClass toTokenClass(TokenKind kind);

using HighlightTokenRange = ArrayRef<std::pair<HighlightTokenClass, StringRef>>;

HighlightTokenRange getHighlightTokenRange();

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
   * must represent comment
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
   * @param kind
   * @param token
   */
  virtual void emit(TokenKind kind, Token token) = 0;
};

struct TokenizerResult {
  std::vector<std::pair<TokenKind, Token>> tokens;
  std::unique_ptr<ParseError> error;
};

class Tokenizer : public TokenEmitter {
public:
  using TokenList = std::vector<std::pair<TokenKind, Token>>;

private:
  TokenList tokens;

public:
  explicit Tokenizer(StringRef source) : TokenEmitter(source) {}

  const auto &get() const { return this->tokens; }

  TokenizerResult tokenize() {
    auto error = this->tokenizeAndEmit();
    return {
        .tokens = std::move(this->tokens),
        .error = std::move(error),
    };
  }

private:
  void emit(TokenKind kind, Token token) override;
};

/**
 * distinguish command and user-defined command decl
 * ex. `AAA` and `BBB() {}`
 * @param tokens
 * @param index
 * @return
 */
inline bool isUDCDeclTokenAt(const std::vector<std::pair<TokenKind, Token>> &tokens,
                             unsigned int index) {
  if (index < tokens.size() && tokens[index].first == TokenKind::COMMAND) {
    // skip trivia (escaped newlines)
    for (index++; index < tokens.size() && (tokens[index].first == TokenKind::ESCAPED_NL ||
                                            tokens[index].first == TokenKind::COMMENT);
         index++)
      ;
    return index < tokens.size() && tokens[index].first == TokenKind::LP;
  }
  return false;
}

} // namespace arsh

#endif // ARSH_HIGHLIGHTER_BASE_H
