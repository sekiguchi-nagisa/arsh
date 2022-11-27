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

#ifndef YDSH_TOOLS_ANALYZER_SEMANTIC_TOKEN_H
#define YDSH_TOOLS_ANALYZER_SEMANTIC_TOKEN_H

#include "source.h"
#include <highlighter_base.h>

namespace ydsh::lsp {

class SemanticTokenEncoder {
private:
  std::unordered_map<SemanticTokenTypes, unsigned int> tokenTypeToIds;
  std::unordered_map<SemanticTokenModifiers, unsigned int> tokenModifierToIds;

public:
  explicit SemanticTokenEncoder(const SemanticTokensLegend &legend);

  Optional<std::pair<unsigned int, unsigned int>> encode(HighlightTokenClass tokenClass) const;
};

struct TokenDelta {
  unsigned int deltaLine;

  /**
   * if previous and current tokens are not same line,
   * deltaStartPos is equivalent to current token start pos
   */
  unsigned int deltaStartPos;

  unsigned int len;
};

/**
 *
 * @param prev
 * must be single line token
 * @param cur
 * must be single line token
 * @return
 */
TokenDelta getTokenDelta(const Range &prev, const Range &cur);

/**
 *
 * @param source
 * must be end with newline
 * @param token
 * @param callback
 * @return
 */
unsigned int splitTokenByNewline(StringRef source, Token token,
                                 const std::function<void(Token)> &callback);

class SemanticTokenEmitter : public TokenEmitter {
private:
  const SemanticTokenEncoder &encoder;
  const Source &src;
  Range prev;
  SemanticTokens tokens;

public:
  SemanticTokenEmitter(const SemanticTokenEncoder &encoder, const Source &src)
      : TokenEmitter(src.getContent()), encoder(encoder), src(src) {
    this->tokens.data.reserve(500);
    this->prev = {
        .start = {.line = 0, .character = 0},
        .end = {.line = 0, .character = 0},
    };
  }

  SemanticTokens take() && { return std::move(this->tokens); }

private:
  void emit(HighlightTokenClass tokenClass, Token token) override;
};

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_SEMANTIC_TOKEN_H
