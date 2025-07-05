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

#include "token_edit.h"
#include "highlighter_base.h"
#include "line_buffer.h"

namespace arsh {

struct TokenListComp {
  bool operator()(const std::pair<TokenKind, Token> &x, unsigned int y) const {
    return x.second.pos < y;
  }

  bool operator()(unsigned int x, const std::pair<TokenKind, Token> &y) const {
    return x < y.second.pos;
  }
};

static unsigned int lookupToken(const Tokenizer::TokenList &tokens, unsigned int pos) {
  if (const auto iter = std::lower_bound(tokens.begin(), tokens.end(), pos, TokenListComp());
      iter != tokens.end()) {
    return iter - tokens.begin();
  }
  return tokens.size();
}

static Tokenizer::TokenList tokenize(const LineBuffer &buf) {
  Tokenizer tokenizer(buf.get());
  auto error = tokenizer.tokenizeAndEmit();
  auto tokens = std::move(tokenizer).take();
  if (error) {
    if (buf.getCursor() > error->getErrorToken().endPos()) {
      tokens.clear();
    } else if (StringRef(error->getErrorKind()) == INVALID_TOKEN) {
      tokens.emplace_back(error->getTokenKind(), error->getErrorToken());
    }
  }
  return tokens;
}

static Optional<unsigned int> resolveEditAfterPos(const LineBuffer &buf,
                                                  const MoveOrDeleteTokenParam param) {
  auto tokens = tokenize(buf);
  if (tokens.empty()) {
    return {};
  }
  unsigned int cursor = buf.getCursor();
  unsigned int index = lookupToken(tokens, cursor);
  if (param.left) {
    index = index < tokens.size() ? index : index - 1;
    Token token = tokens[index].second;
    if (cursor <= token.pos) {
      if (index) {
        token = tokens[index - 1].second;
      } else {
        return 0;
      }
    }
    return token.pos;
  }
  if (index == tokens.size()) {
    return buf.getUsedSize();
  }
  Token token = tokens[index].second;
  if (cursor < token.pos && index > 0 && cursor < tokens[index - 1].second.endPos()) {
    index--;
    token = tokens[index].second;
  }
  return token.endPos() <= buf.getUsedSize() ? token.endPos() : buf.getUsedSize();
}

Optional<bool> moveCursorOrDeleteToken(LineBuffer &buf, const MoveOrDeleteTokenParam param,
                                       std::string *capture) {
  if (auto ret = resolveEditAfterPos(buf, param); ret.hasValue()) {
    const unsigned int afterPos = ret.unwrap();
    if (param.move) {
      const bool moved = buf.getCursor() != afterPos;
      buf.setCursor(afterPos);
      return moved;
    }
    if (param.left) { // remove left token
      assert(afterPos <= buf.getCursor());
      return buf.deleteToCursor(buf.getCursor() - afterPos, capture);
    }
    assert(afterPos >= buf.getCursor());
    return buf.deleteFromCursor(afterPos - buf.getCursor(), capture);
  }
  return {}; // fallback
}

} // namespace arsh