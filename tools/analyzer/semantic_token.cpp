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

static Optional<SemanticTokenTypes> toTokenType(HightlightTokenClass tokenClass) {
  switch (tokenClass) {
  case HightlightTokenClass::KEYWORD:
    return SemanticTokenTypes::keyword_;
  case HightlightTokenClass::NONE:
    break;
  case HightlightTokenClass::COMMENT:
    return SemanticTokenTypes::comment_;
  case HightlightTokenClass::OPERATOR:
    return SemanticTokenTypes::operator_;
  case HightlightTokenClass::NUMBER:
    return SemanticTokenTypes::number_;
  case HightlightTokenClass::REGEX:
    return SemanticTokenTypes::regexp_;
  case HightlightTokenClass::STRING:
    return SemanticTokenTypes::string_;
  case HightlightTokenClass::SIGNAL:
    return SemanticTokenTypes::event_;
  case HightlightTokenClass::COMMAND:
    return SemanticTokenTypes::function_;
  case HightlightTokenClass::COMMAND_ARG:
    return SemanticTokenTypes::parameter_;
  case HightlightTokenClass::REDIRECT:
    return SemanticTokenTypes::operator_;
  case HightlightTokenClass::VARIABLE:
    return SemanticTokenTypes::variable_;
  case HightlightTokenClass::TYPE:
    return SemanticTokenTypes::type_;
  case HightlightTokenClass::MEMBER:
    return SemanticTokenTypes::property_;
  }
  return {};
}

Optional<std::pair<unsigned int, unsigned int>>
SemanticTokenEncoder::encode(HightlightTokenClass tokenClass) const {
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

void SemanticTokenEmitter::emit(HightlightTokenClass tokenClass, Token token) {
  auto typePair = this->encoder.encode(tokenClass);
  if (!typePair.hasValue()) {
    return;
  }
  splitTokenByNewline(this->source, token, [&](Token subToken) {
    auto ret = toRange(this->source, subToken);
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
