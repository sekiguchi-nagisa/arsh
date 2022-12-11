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

#include "highlighter.h"

namespace ydsh {

// ##############################
// ##     ANSIEscapeSeqMap     ##
// ##############################

ANSIEscapeSeqMap ANSIEscapeSeqMap::fromString(StringRef setting) {
  ANSIEscapeSeqMap seqMap;

  // comment=\033... keyword=...
  for (StringRef::size_type pos = 0; pos != StringRef::npos;) {
    // extract entry `comment=...`
    auto retPos = setting.find(" ", pos);
    auto entry = setting.slice(pos, retPos);
    pos = retPos != StringRef::npos ? retPos + 1 : retPos;

    retPos = entry.find('=');
    if (retPos == StringRef::npos) {
      continue; // skip invalid entry
    }
    auto element = entry.slice(0, retPos);
    auto escapeSeq = entry.substr(retPos + 1); // FIXME: check escape sequence format?

    for (auto &e : getHighlightTokenEntries()) {
      if (element == e.second) {
        seqMap.values[e.first] = escapeSeq.toString();
        break;
      }
    }
  }

  return seqMap;
}

// ################################
// ##     BuiltinHighlighter     ##
// ################################

void BuiltinHighlighter::emit(HighlightTokenClass tokenClass, Token token) {
  this->tokens.emplace_back(tokenClass, token);
}

void BuiltinHighlighter::writeTrivia(StringRef ref) {
  for (StringRef::size_type pos = 0; pos != StringRef::npos;) {
    auto r = ref.find('\\', pos);
    auto line = ref.slice(pos, r);
    pos = r != StringRef::npos ? r + 1 : r;

    this->write(line, HighlightTokenClass::NONE);
    if (r != StringRef::npos) {
      auto tokenClass = HighlightTokenClass::NONE;
      this->write(StringRef("\\"), tokenClass);
    }
  }
}

void BuiltinHighlighter::write(StringRef ref, HighlightTokenClass tokenClass) {
  const std::string *seq = nullptr;
  if (tokenClass != HighlightTokenClass::NONE) {
    auto iter = this->escapeSeqMap.getValues().find(tokenClass);
    if (iter != this->escapeSeqMap.getValues().end()) {
      seq = &(iter->second);
    }
  }
  this->writeWithEscapeSeq(ref, seq ? *seq : "");
}

bool BuiltinHighlighter::doHighlight() {
  auto error = this->tokenizeAndEmit();

  // write each token
  unsigned int curPos = 0;
  for (auto &e : this->tokens) {
    Token token = e.second;
    assert(curPos <= token.pos);
    this->writeTrivia(this->source.slice(curPos, token.pos));
    curPos = token.endPos();
    this->write(this->source.substr(token.pos, token.size), e.first);
  }
  // write remain
  if (curPos < this->source.size()) {
    auto remain = this->source.substr(curPos);
    this->writeTrivia(remain);
  }

  // check line continuation
  if (error) {
    if (error->getTokenKind() == TokenKind::EOS) {
      return false;
    } else {
      auto kind = error->getTokenKind();
      if (isUnclosedToken(kind) && kind != TokenKind::UNCLOSED_REGEX_LITERAL) {
        return false;
      }
    }
  } else if (!this->tokens.empty() && this->tokens.back().first == HighlightTokenClass::NONE) {
    auto token = this->tokens.back().second;
    auto last = this->source.substr(token.pos, token.size);
    if (last.size() == 2 && last == "\\\n") {
      return false;
    }
  }
  return true;
}

} // namespace ydsh