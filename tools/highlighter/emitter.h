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

#ifndef YDSH_TOOLS_HIGHLIGHTER_EMITTER_H
#define YDSH_TOOLS_HIGHLIGHTER_EMITTER_H

#include "parser.h"

namespace ydsh::highlighter {

enum class HighlightTokenClass : unsigned int {
  NONE,
  COMMENT,
  KEYWORD,
  OPERATOR,
  NUMBER,
  REGEX,
  STRING,
  COMMAND,
  COMMAND_ARG,
  REDIRECT,
  VARIABLE,
  TYPE,
  MEMBER,
  FOREGROUND_, // pseudo token class for foreground (text) color
  BACKGROUND_, // pseudo token class for background color
  LINENO_,     // pseudo token class for line number
};

HighlightTokenClass toTokenClass(TokenKind kind);

class TokenEmitter : public CommentStore, public TokenTracker {
protected:
  /**
   * must be end with newline
   */
  const StringRef source;

public:
  explicit TokenEmitter(StringRef source) : source(source) {}

  ~TokenEmitter() override = default;

  [[nodiscard]] StringRef getSource() const { return this->source; }

  void operator()(TokenKind kind, Token token) override;

  /**
   * for comment
   * @param token
   * must be represent comment
   */
  void operator()(Token token) override;

private:
  /**
   * actual token emit function
   * @param tokenClass
   * @param token
   */
  virtual void emit(HighlightTokenClass tokenClass, Token token) = 0;
};

/**
 * colorize source content
 * @param emitter
 */
void tokenizeAndEmit(TokenEmitter &emitter);

} // namespace ydsh::highlighter

#endif // YDSH_TOOLS_HIGHLIGHTER_EMITTER_H
