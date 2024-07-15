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

#ifndef ARSH_TOOLS_ANALYZER_SEMANTIC_TOKEN_H
#define ARSH_TOOLS_ANALYZER_SEMANTIC_TOKEN_H

#include "source.h"
#include <highlighter_base.h>

namespace arsh::lsp {

constexpr size_t numberOfExtendSemanticTokenTypes() {
  constexpr SemanticTokenTypes table[] = {
#define GEN_TABLE(E, V, F) SemanticTokenTypes::E,
      EACH_SEMANTIC_TOKEN_TYPES_EXTEND(GEN_TABLE)
#undef GEN_TABLE
  };
  return std::size(table);
}

struct ExtendSemanticTokenTypeEntry {
  SemanticTokenTypes extend;
  SemanticTokenTypes fallback;
};

using ExtendSemanticTokenTypeList =
    std::array<ExtendSemanticTokenTypeEntry, numberOfExtendSemanticTokenTypes()>;

const ExtendSemanticTokenTypeList &getExtendSemanticTokenTypes();

void fitLegendToClient(SemanticTokensLegend &legend,
                       const std::vector<std::string> &clientTokenTypes);

class SemanticTokenEncoder {
private:
  SemanticTokensLegend legend;
  std::unordered_map<SemanticTokenTypes, unsigned int> tokenTypeToIds;
  std::unordered_map<SemanticTokenModifiers, unsigned int> tokenModifierToIds;

public:
  SemanticTokenEncoder() = default;

  explicit SemanticTokenEncoder(SemanticTokensLegend &&legend);

  const auto &getLegend() const { return this->legend; }

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

} // namespace arsh::lsp

#endif // ARSH_TOOLS_ANALYZER_SEMANTIC_TOKEN_H
