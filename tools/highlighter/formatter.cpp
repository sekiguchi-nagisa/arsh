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

void ANSIFormatter::emit(HighlightTokenClass tokenClass, Token token) {
  assert(this->curSrcPos <= token.pos);
  this->write(this->source.slice(this->curSrcPos, token.pos));

  auto *styleRule = this->style.find(tokenClass);
  if (styleRule) {
    std::string v;
    if (styleRule->text) {
      v += this->format(styleRule->text, false);
    }
    if (styleRule->background) {
      v += this->format(styleRule->text, true);
    }
    if (styleRule->bold) {
      v += "\033[1m";
    }
    if (styleRule->italic) {
      v += "\033[3m";
    }
    if (styleRule->underline) {
      v += "\033[4m";
    }
    this->output << v;
  }

  this->write(this->source.substr(token.pos, token.size));
  this->curSrcPos = token.endPos();
  if (styleRule) {
    this->output << "\033[0m";
  }
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