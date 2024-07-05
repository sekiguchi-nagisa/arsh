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

#include <misc/format.hpp>

#include "formatter.h"

namespace arsh::highlighter {

// #######################
// ##     Formatter     ##
// #######################

void Formatter::emit(HighlightTokenClass tokenClass, Token token) {
  assert(this->curSrcPos <= token.pos);
  this->drawTrivia(this->source.slice(this->curSrcPos, token.pos));
  this->curSrcPos = token.endPos();
  this->draw(this->source.substr(token.pos, token.size), &tokenClass);
}

void Formatter::initialize(StringRef newSource) {
  this->source = newSource;
  this->curSrcPos = 0;
}

std::string Formatter::dump() { return ""; }

void Formatter::drawTrivia(StringRef ref) {
  for (StringRef::size_type pos = 0; pos != StringRef::npos;) {
    auto r = ref.find('\\', pos);
    auto line = ref.slice(pos, r);
    pos = r != StringRef::npos ? r + 1 : r;

    this->draw(line, nullptr);
    if (r != StringRef::npos) {
      auto tokenClass = HighlightTokenClass::FOREGROUND_;
      this->draw(StringRef("\\"), &tokenClass);
    }
  }
}

// ###########################
// ##     NullFormatter     ##
// ###########################

void NullFormatter::draw(StringRef ref, const HighlightTokenClass *) { this->write(ref); }

void NullFormatter::finalize() {
  if (this->curSrcPos < this->source.size()) {
    auto remain = this->source.substr(this->curSrcPos);
    this->drawTrivia(remain);
    this->curSrcPos = this->source.size();
  }
  this->output.flush();
}

// ####################################
// ##     IndexedColorPalette256     ##
// ####################################

/**
 * see. (https://en.wikipedia.org/wiki/ANSI_escape_code#8-bit)
 */
IndexedColorPalette256::IndexedColorPalette256() {
  this->values.resize(256);

  // Standard colors
  this->values[0] = Color::parse("#000000");
  this->values[1] = Color::parse("#800000");
  this->values[2] = Color::parse("#008000");
  this->values[3] = Color::parse("#808000");
  this->values[4] = Color::parse("#000080");
  this->values[5] = Color::parse("#800080");
  this->values[6] = Color::parse("#008080");
  this->values[7] = Color::parse("#c0c0c0");

  // High-intensity colors
  this->values[8] = Color::parse("#808080");
  this->values[9] = Color::parse("#ff0000");
  this->values[10] = Color::parse("#00ff00");
  this->values[11] = Color::parse("#ffff00");
  this->values[12] = Color::parse("#0000ff");
  this->values[13] = Color::parse("#ff00ff");
  this->values[14] = Color::parse("#00ffff");
  this->values[15] = Color::parse("#ffffff");

  // 216 colors
  unsigned char cube[] = {0x00, 0x5f, 0x87, 0xaf, 0xd7, 0xff};

  for (unsigned int r = 0; r < 6; r++) {
    for (unsigned int g = 0; g < 6; g++) {
      for (unsigned int b = 0; b < 6; b++) {
        unsigned int index = 16 + 36 * r + 6 * g + b;
        this->values[index] =
            Color{.red = cube[r], .green = cube[g], .blue = cube[b], .initialized = true};
      }
    }
  }

  // Grayscale colors
  for (unsigned int i = 232; i < 256; i++) {
    unsigned char c = i - 232 * 10 + 8;
    this->values[i] = Color{.red = c, .green = c, .blue = c, .initialized = true};
  }
}

unsigned char IndexedColorPalette256::findClosest(Color color) const {
  assert(color);
  unsigned int closest = 0;
  auto distance = std::numeric_limits<double>::max();
  for (unsigned int i = 0; i < this->values.size(); i++) {
    double v = this->values[i].distance(color);
    if (v < distance) {
      distance = v;
      closest = i;
    }
  }
  return closest;
}

// ###########################
// ##     ANSIFormatter     ##
// ###########################

std::string ANSIFormatter::format(Color c, bool background) {
  std::string value;
  switch (this->colorCap) {
  case TermColorCap::TRUE_COLOR: {
    char buf[32];
    snprintf(buf, std::size(buf), "\033[%d;2;%d;%d;%dm", background ? 48 : 38, c.red, c.green,
             c.blue);
    value += buf;
    break;
  }
  case TermColorCap::INDEXED_256: {
    char buf[16];
    unsigned int index = this->colorPalette256.findClosest(c);
    snprintf(buf, std::size(buf), "\033[%d;5;%dm", background ? 48 : 38, index);
    value += buf;
    break;
  }
  }
  return value;
}

const std::string &ANSIFormatter::toEscapeSeq(HighlightTokenClass tokenClass) {
  if (auto iter = this->escapeSeqCache.find(tokenClass); iter != this->escapeSeqCache.end()) {
    return iter->second;
  }

  // compute
  auto &styleRule = this->style.findOrDefault(tokenClass);
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

  auto pair = this->escapeSeqCache.emplace(tokenClass, std::move(escapeSeq));
  assert(pair.second);
  return pair.first->second;
}

void ANSIFormatter::draw(StringRef ref, const HighlightTokenClass *tokenClass) {
  const auto &escapeSeq = tokenClass ? this->toEscapeSeq(*tokenClass) : "";

  // split by newline
  for (StringRef::size_type pos = 0; pos != StringRef::npos;) {
    auto r = ref.find('\n', pos);
    auto line = ref.slice(pos, r);
    pos = r != StringRef::npos ? r + 1 : r;

    if (!line.empty()) {
      this->write(escapeSeq);
      this->write(line);
      if (!escapeSeq.empty()) {
        this->write("\033[0m");
      }
    }
    if (r != StringRef::npos) {
      this->write("\n");
    }
  }
}

void ANSIFormatter::finalize() {
  if (this->curSrcPos < this->source.size()) {
    auto remain = this->source.substr(this->curSrcPos);
    this->drawTrivia(remain);
    this->curSrcPos = this->source.size();
  }
  this->output.flush();
}

std::string ANSIFormatter::dump() {
  // fill escape sequence cache
  for (auto &e : getHighlightTokenEntries()) {
    this->toEscapeSeq(e.first);
  }
  std::string value;
  for (auto &e : this->escapeSeqCache) {
    auto index = static_cast<unsigned int>(e.first);
    assert(index < getHighlightTokenEntries().size());
    const StringRef name = getHighlightTokenEntries()[index].second;
    if (!value.empty()) {
      value += " ";
    }
    value += name;
    value += "=";
    value += e.second;
  }
  return value;
}

// ###########################
// ##     HTMLFormatter     ##
// ###########################

static std::string toCSSImpl(const StyleRule &styleRule) {
  std::vector<std::string> values;
  if (styleRule.text) {
    std::string value = "color:";
    value += styleRule.text.toString();
    values.push_back(std::move(value));
  }
  if (styleRule.background) {
    std::string value = "background-color:";
    value += styleRule.background.toString();
    values.push_back(std::move(value));
  }
  //  if (styleRule.border) { //FIXME: are there any styles that use border?
  //    std::string value = "border:1px solid ";
  //    value += styleRule.border.toString();
  //    values.push_back(std::move(value));
  //  }
  if (styleRule.bold) {
    values.emplace_back("font-weight:bold");
  }
  if (styleRule.italic) {
    values.emplace_back("font-style:italic");
  }
  if (styleRule.underline) {
    values.emplace_back("text-decoration:underline");
  }

  std::string value;
  for (auto &e : values) {
    if (!value.empty()) {
      value += ";";
    }
    value += e;
  }
  return value;
}

void HTMLFormatter::emitLineNum(unsigned int lineNum) {
  std::string value = padLeft(lineNum, this->maxLineNumDigits, ' ');
  auto &css = this->toCSS(HighlightTokenClass::LINENO_);
  this->output << "<span";
  if (!css.empty()) {
    this->output << " style=\"" << css << "\"";
  }
  this->output << ">" << value << "</span>   ";
}

const std::string &HTMLFormatter::toCSS(HighlightTokenClass tokenClass) {
  if (auto iter = this->cssCache.find(tokenClass); iter != this->cssCache.end()) {
    return iter->second;
  }
  auto &styleRule = this->style.findOrDefault(tokenClass);
  auto pair = this->cssCache.emplace(tokenClass, toCSSImpl(styleRule));
  assert(pair.second);
  return pair.first->second;
}

static std::string escape(StringRef ref) {
  std::string value;
  for (auto ch : ref) {
    switch (ch) {
    case '<':
      value += "&lt;";
      break;
    case '>':
      value += "&gt;";
      break;
    case '&':
      value += "&amp;";
      break;
    case '"':
      value += "&quot;";
      break;
    case '\'':
      value += "&#39;";
      break;
    default:
      value += ch;
      break;
    }
  }
  return value;
}

void HTMLFormatter::draw(StringRef ref, const HighlightTokenClass *tokenClass) {
  const auto *css = tokenClass ? &this->toCSS(*tokenClass) : nullptr;

  // split by newline
  for (StringRef::size_type pos = 0; pos != StringRef::npos;) {
    auto r = ref.find('\n', pos);
    auto line = ref.slice(pos, r);
    pos = r != StringRef::npos ? r + 1 : r;

    if (!line.empty()) {
      if (css) {
        if (!css->empty()) {
          this->output << "<span style=\"" << *css << "\">";
        }
        this->output << escape(line);
        if (!css->empty()) {
          this->output << "</span>";
        }
      } else {
        this->write(line);
      }
    }
    if (r != StringRef::npos) {
      this->output << '\n';
      this->newlineCount++;
      if (hasFlag(this->formatOp, HTMLFormatOp::LINENO) &&
          !hasFlag(this->formatOp, HTMLFormatOp::TABLE)) {
        uint64_t lineNum = this->lineNumOffset + this->newlineCount;
        this->emitLineNum(lineNum);
      }
    }
  }
}

void HTMLFormatter::initialize(StringRef newSource) {
  Formatter::initialize(newSource);

  this->newlineCount = 0;
  if (hasFlag(this->formatOp, HTMLFormatOp::FULL)) {
    this->output << "<html>\n<body";
    auto css = toCSSImpl(this->style.background);
    if (!css.empty()) {
      this->output << " style=\"" << css << "\"";
    }
    this->output << ">\n";
  }

  if (hasFlag(this->formatOp, HTMLFormatOp::LINENO)) {
    // pre-compute max line number
    uint64_t maxLineNum = this->lineNumOffset;
    for (StringRef::size_type pos = 0; (pos = this->source.find('\n', pos)) != StringRef::npos;
         pos++) {
      maxLineNum++;
    }
    this->maxLineNumDigits = countDigits(maxLineNum);

    if (hasFlag(this->formatOp, HTMLFormatOp::TABLE)) {
      this->output << R"(<table class="highlight_table"><tr><td class="linenos">
<div class="lineno_div" style=")"
                   << this->toCSS(HighlightTokenClass::LINENO_) << "\"><pre>";
      for (uint64_t count = this->lineNumOffset; count < maxLineNum; count++) {
        std::string value = padLeft(count, this->maxLineNumDigits, ' ');
        this->output << value << "\n";
      }
      this->output << "</pre></div></td><td class=\"code\">\n";
    }
  }
  this->output << "<div class=\"highlight\"><pre style=\"tab-size:4\">\n<code>";
  if (hasFlag(this->formatOp, HTMLFormatOp::LINENO) &&
      !hasFlag(this->formatOp, HTMLFormatOp::TABLE)) {
    this->emitLineNum(this->lineNumOffset);
  }
}

void HTMLFormatter::finalize() {
  if (this->curSrcPos < this->source.size()) {
    auto remain = this->source.substr(this->curSrcPos);
    this->drawTrivia(remain);
    this->curSrcPos = this->source.size();
  }
  this->output << "</code></pre></div>";
  if (hasFlag(this->formatOp, HTMLFormatOp::TABLE)) {
    this->output << "\n</td></tr></table>";
  }
  if (hasFlag(this->formatOp, HTMLFormatOp::FULL)) {
    this->output << "\n</body>\n</html>" << '\n';
  }
  this->output.flush();
}

} // namespace arsh::highlighter
