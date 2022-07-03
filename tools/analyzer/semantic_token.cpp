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

#include "semantic_token.h"
#include "source.h"

namespace ydsh::lsp {

SemanticTokenEncoder::SemanticTokenEncoder(const SemanticTokensLegend &legend) {
  unsigned int index = 0;
  for (auto &e : legend.tokenTypes) {
    this->tokenTypeToIds.emplace(e, index++);
  }

  index = 0;
  for (auto &e : legend.tokenModifiers) {
    unsigned int v = 1 << index++;
    this->tokenModifierToIds.emplace(e, v);
  }
}

static Optional<SemanticTokenTypes> toTokenType(HighlightTokenClass tokenClass) {
  switch (tokenClass) {
  case HighlightTokenClass::KEYWORD:
    return SemanticTokenTypes::keyword_;
  case HighlightTokenClass::NONE:
    break;
  case HighlightTokenClass::COMMENT:
    return SemanticTokenTypes::comment_;
  case HighlightTokenClass::OPERATOR:
    return SemanticTokenTypes::operator_;
  case HighlightTokenClass::NUMBER:
    return SemanticTokenTypes::number_;
  case HighlightTokenClass::REGEX:
    return SemanticTokenTypes::regexp_;
  case HighlightTokenClass::STRING:
    return SemanticTokenTypes::string_;
  case HighlightTokenClass::COMMAND:
    return SemanticTokenTypes::function_;
  case HighlightTokenClass::COMMAND_ARG:
    return SemanticTokenTypes::parameter_;
  case HighlightTokenClass::REDIRECT:
    return SemanticTokenTypes::operator_;
  case HighlightTokenClass::VARIABLE:
    return SemanticTokenTypes::variable_;
  case HighlightTokenClass::TYPE:
    return SemanticTokenTypes::type_;
  case HighlightTokenClass::MEMBER:
    return SemanticTokenTypes::property_;
  case HighlightTokenClass::FOREGROUND_:
  case HighlightTokenClass::BACKGROUND_:
  case HighlightTokenClass::LINENO_:
    break;
  }
  return {};
}

Optional<std::pair<unsigned int, unsigned int>>
SemanticTokenEncoder::encode(HighlightTokenClass tokenClass) const {
  auto ret = toTokenType(tokenClass);
  if (ret.hasValue()) {
    return std::make_pair(this->tokenTypeToIds.find(ret.unwrap())->second, 0);
  }
  return {};
}

unsigned int splitTokenByNewline(StringRef source, Token token,
                                 const std::function<void(Token)> &callback) {
  unsigned int count = 0;
  auto ref = source.substr(token.pos, token.size);
  for (StringRef::size_type pos = 0; pos != StringRef::npos;) {
    const auto nlPos = ref.find('\n', pos);
    const auto subRef = ref.slice(pos, nlPos);
    pos = nlPos != StringRef::npos ? nlPos + 1 : nlPos;

    Token subToken = {
        .pos = static_cast<unsigned int>(subRef.data() - source.data()),
        .size = static_cast<unsigned int>(subRef.size()),
    };
    count++;
    if (callback) {
      callback(subToken);
    }
  }
  return count;
}

TokenDelta getTokenDelta(const Range &prev, const Range &cur) {
  assert(prev.start.line == prev.end.line);
  assert(cur.start.line == cur.end.line);
  assert(prev.start.line <= cur.start.line);

  TokenDelta tokenDelta;
  tokenDelta.deltaLine = cur.start.line - prev.start.line;
  tokenDelta.deltaStartPos =
      tokenDelta.deltaLine == 0 ? cur.start.character - prev.start.character : cur.start.character;
  tokenDelta.len = cur.end.character - cur.start.character;
  return tokenDelta;
}

void SemanticTokenEmitter::emit(HighlightTokenClass tokenClass, Token token) {
  auto typePair = this->encoder.encode(tokenClass);
  if (!typePair.hasValue()) {
    return;
  }
  splitTokenByNewline(this->source, token, [&](Token subToken) {
    auto ret = toRange(this->src, subToken);
    assert(ret.hasValue());
    auto &range = ret.unwrap();
    auto delta = getTokenDelta(this->prev, range);
    this->prev = range;

    this->tokens.data.push_back(delta.deltaLine);
    this->tokens.data.push_back(delta.deltaStartPos);
    this->tokens.data.push_back(delta.len);
    this->tokens.data.push_back(typePair.unwrap().first);
    this->tokens.data.push_back(typePair.unwrap().second);
  });
}

} // namespace ydsh::lsp
