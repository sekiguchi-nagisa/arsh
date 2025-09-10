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

static Tokenizer::TokenList tokenize(const LineBuffer &buf, TokenizerResult *cache) {
  TokenizerResult ret;
  if (!cache) {
    Tokenizer tokenizer(buf.get());
    ret = tokenizer.tokenize();
    cache = &ret;
  }

  if (cache->error) {
    if (buf.getCursor() > cache->error->getErrorToken().endPos()) {
      cache->tokens.clear();
    } else if (StringRef(cache->error->getErrorKind()) == INVALID_TOKEN) {
      cache->tokens.emplace_back(cache->error->getTokenKind(), cache->error->getErrorToken());
    }
  }
  Tokenizer::TokenList tmp;
  std::swap(tmp, cache->tokens); // explicitly clear tokens
  return tmp;
}

static const char *reverseSearchPathSep(const char *begin, const char *end) {
  if (*end == '/') {
    --end;
  }
  for (; begin != end; --end) {
    if (*end == '/') {
      return end;
    }
  }
  return nullptr;
}

static const char *searchPathSep(const char *begin, const char *end) {
  if (*begin == '/') {
    ++begin;
  }
  for (; begin != end; ++begin) {
    if (*begin == '/') {
      return begin;
    }
  }
  return nullptr;
}

static Optional<unsigned int> resolveEditAfterPos(const LineBuffer &buf,
                                                  const MoveOrDeleteTokenParam param,
                                                  TokenizerResult *cache) {
  auto tokens = tokenize(buf, cache);
  if (tokens.empty()) {
    return {};
  }
  const unsigned int cursor = buf.getCursor();
  unsigned int index = lookupToken(tokens, cursor);
  if (param.left) {
    index = index < tokens.size() ? index : index - 1;
    Token token = tokens[index].second;
    if (cursor <= token.pos) {
      if (index) {
        index--;
        token = tokens[index].second;
      } else {
        return 0;
      }
    }
    if (const auto kind = tokens[index].first; cursor > token.pos) {
      if (kind == TokenKind::COMMENT) {
        return {}; // within comment
      }
      if (kind == TokenKind::COMMAND || kind == TokenKind::CMD_ARG_PART) {
        const char *ptr = buf.get().data();
        const char *end = ptr + cursor;
        if (*(end - 1) == '/') {
          return cursor - 1;
        }
        if (const auto *ret = reverseSearchPathSep(ptr + token.pos - 1, end)) {
          return ret - ptr + 1;
        }
      }
    }
    return token.pos;
  }
  if (index == tokens.size()) {
    return buf.getUsedSize();
  }
  Token token = tokens[index].second;
  if (cursor < token.pos) {
    if (index > 0 && cursor < tokens[index - 1].second.endPos()) {
      index--;
      token = tokens[index].second;
    } else if (tokens[index].first == TokenKind::COMMENT) {
      return token.pos;
    }
  }
  if (token.endPos() <= buf.getUsedSize()) {
    const auto kind = tokens[index].first;
    if (kind == TokenKind::COMMENT && cursor >= token.pos) {
      return {}; // within comment
    }
    if (kind == TokenKind::COMMAND || kind == TokenKind::CMD_ARG_PART) {
      const char *ptr = buf.get().data();
      if (const char *ret = searchPathSep(ptr + cursor, ptr + token.endPos())) {
        return ret - ptr;
      }
    }
    return token.endPos();
  }
  return buf.getUsedSize(); // if EOS
}

Optional<bool> moveCursorOrDeleteToken(LineBuffer &buf, const MoveOrDeleteTokenParam param,
                                       std::string *capture, TokenizerResult *cache) {
  if (auto ret = resolveEditAfterPos(buf, param, cache); ret.hasValue()) {
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

static bool replaceBytes(LineBuffer &buf, Token token, StringRef replacement) {
  return buf.intoAtomicEdit([&token, &replacement](LineBuffer &b) {
    const unsigned int suffixOffset = b.getCursor() - token.endPos();
    b.setCursor(token.endPos());
    if (b.deleteToCursor(token.size) && b.insertToCursor(replacement)) {
      b.setCursor(b.getCursor() + suffixOffset);
      return true;
    }
    return false;
  });
}

/**
 * cursor position in the following cases
 *
 * ls
 *   ^
 *
 * @param buf
 * @param ret
 * @return
 */
static bool isCursorInDummyNewline(const LineBuffer &buf, const TokenizerResult &ret) {
  const unsigned int cursor = buf.getCursor();
  return cursor == buf.getUsedSize() && ret.tokens.size() > 1 &&
         ret.tokens.back().first == TokenKind::NEW_LINE && ret.tokens.back().second.pos == cursor &&
         ret.tokens[ret.tokens.size() - 2].second.endPos() == cursor;
}

bool tryToExpandAbbreviation(LineBuffer &buf, const AbbrMap &abbrMap,
                             const TokenizerResult &cache) {
  if (abbrMap.empty() || cache.tokens.empty() || buf.getCursor() == 0) {
    return false;
  }
  const unsigned int cursor = buf.getCursor() - (isCursorInDummyNewline(buf, cache) ? 0 : 1);
  const unsigned int index = lookupToken(cache.tokens, cursor);
  if (index == 0 || index == cache.tokens.size() || cursor > cache.tokens[index].second.pos) {
    return false;
  }
  auto &[kind, token] = cache.tokens[index - 1];
  if (kind == TokenKind::COMMAND && cursor == token.endPos() &&
      !isUDCDeclTokenAt(cache.tokens, index - 1)) {
    std::string cmd = buf.get().substr(token.pos, token.size).toString();
    if (auto iter = abbrMap.find(cmd); iter != abbrMap.end()) {
      return replaceBytes(buf, token, iter->second);
    }
  }
  return false;
}

} // namespace arsh