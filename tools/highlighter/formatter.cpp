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

#include "formatter.h"

namespace ydsh::highlighter {

// ###########################
// ##     NullFormatter     ##
// ###########################

void NullFormatter::emit(HighlightTokenClass, Token token) {
  assert(this->curSrcPos <= token.pos);
  this->write(this->source.slice(this->curSrcPos, token.pos));
  this->write(this->source.substr(token.pos, token.size));
  this->curSrcPos = token.endPos();
}

void NullFormatter::finalize() {
  if (this->curSrcPos < this->source.size()) {
    auto remain = this->source.substr(this->curSrcPos);
    this->write(remain);
    this->curSrcPos = this->source.size();
  }
  this->output.flush();
}

// ###########################
// ##     ANSIFormatter     ##
// ###########################

std::string ANSIFormatter::format(Color c, bool background) { // FIXME: 8bit color
  std::string value;
  assert(this->colorCap == TermColorCap::TRUE_COLOR);
  value += background ? "\033[48;2;" : "\033[38;2;";
  value += std::to_string(static_cast<unsigned int>(c.red));
  value += ";";
  value += std::to_string(static_cast<unsigned int>(c.green));
  value += ";";
  value += std::to_string(static_cast<unsigned int>(c.blue));
  value += "m";
  return value;
}

void ANSIFormatter::draw(StringRef ref, const StyleRule &styleRule) {
  std::string escapeSeq;
  if (styleRule.text) {
    escapeSeq += this->format(styleRule.text, false);
  }
  if (styleRule.background) {
    escapeSeq += this->format(styleRule.background, true);
  }
  if (styleRule.bold) {
    escapeSeq += "\033[1m";
  }
  if (styleRule.italic) {
    escapeSeq += "\033[3m";
  }
  if (styleRule.underline) {
    escapeSeq += "\033[4m";
  }

  // split by newline
  for (StringRef::size_type pos = 0; pos != StringRef::npos;) {
    auto r = ref.find('\n', pos);
    auto line = ref.slice(pos, r);
    pos = r != StringRef::npos ? r + 1 : r;

    if (!line.empty()) {
      this->output << escapeSeq;
      this->write(line);
      if (!escapeSeq.empty()) {
        this->output << "\033[0m";
      }
    }
    if (r != StringRef::npos) {
      this->output << '\n';
    }
  }
}

void ANSIFormatter::emit(HighlightTokenClass tokenClass, Token token) {
  assert(this->curSrcPos <= token.pos);
  auto remain = this->source.slice(this->curSrcPos, token.pos);
  this->write(remain);
  this->curSrcPos = token.endPos();

  auto ref = this->source.substr(token.pos, token.size);
  this->draw(ref, this->style.findOrDefault(tokenClass));
}

void ANSIFormatter::finalize() {
  if (this->curSrcPos < this->source.size()) {
    auto remain = this->source.substr(this->curSrcPos);
    this->write(remain);
    this->curSrcPos = this->source.size();
  }
  this->output.flush();
}

} // namespace ydsh::highlighter