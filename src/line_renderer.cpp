/*
 * Copyright (C) 2023 Nagisa Sekiguchi
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

#include "line_renderer.h"
#include "keycode.h"
#include "misc/num_util.hpp"

namespace arsh {

const CharWidthPropertyList &getCharWidthPropertyList() {
  static CharWidthPropertyList table = {{
#define GEN_ENUM(E, S) {CharWidthProperty::E, S},
      EACH_CHAR_WIDTH_PROPERTY(GEN_ENUM)
#undef GEN_ENUM
  }};
  return table;
}

static bool isRegionalIndicator(int codePoint) {
  return codePoint >= 0x1F1E6 && codePoint <= 0x1F1E6 + ('z' - 'a');
}

unsigned int getGraphemeWidth(const CharWidthProperties &ps, const GraphemeCluster &ret) {
  unsigned int width = 0;
  unsigned int flagSeqCount = 0;
  Utf8Stream stream(ret.getRef().begin(), ret.getRef().end());
  while (stream) {
    int codePoint = stream.nextCodePoint();
    if (ps.replaceInvalid && codePoint < 0) {
      codePoint = UnicodeUtil::REPLACEMENT_CHAR_CODE;
    } else if (isRegionalIndicator(codePoint)) {
      flagSeqCount++;
    }
    if (const int w = UnicodeUtil::width(codePoint, ps.eaw); w > 0) {
      width += w;
    }
  }
  if (flagSeqCount == 2) {
    return ps.flagSeqWidth;
  }
  if (ret.isEmojiSeq()) {
    return ps.zwjSeqFallback ? width : 2;
  }
  return width;
}

// ##############################
// ##     ANSIEscapeSeqMap     ##
// ##############################

static bool consumeDigits(const char *&iter, const char *end) {
  if (iter == end || !isDecimal(*iter)) {
    return false;
  }
  for (++iter; iter != end && isDecimal(*iter); ++iter)
    ;
  return true;
}

bool ANSIEscapeSeqMap::checkSGRSeq(StringRef seq) {
  const auto end = seq.end();
  for (auto iter = seq.begin(); iter != end; ++iter) {
    if (*iter != '\x1b' || iter + 1 == end || *(iter + 1) != '[' || iter + 2 == end) {
      return false;
    }
    iter += 2;          // skip '\x1b['
    if (*iter != 'm') { // consume [0-9][0-9]* (; [0-9][0-9]*)*
      if (!consumeDigits(iter, end)) {
        return false;
      }
      while (iter != end && *iter == ';') {
        ++iter;
        if (!consumeDigits(iter, end)) {
          return false;
        }
      }
    }
    if (iter == end || *iter != 'm') {
      return false;
    }
  }
  return true;
}

ANSIEscapeSeqMap ANSIEscapeSeqMap::fromString(StringRef setting) {
  std::unordered_map<HighlightTokenClass, std::string> values;

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
    auto escapeSeq = entry.substr(retPos + 1);
    if (!checkSGRSeq(escapeSeq) || escapeSeq.empty()) {
      continue; // skip invalid color sequence
    }

    for (auto &[cl, name] : getHighlightTokenRange()) {
      if (element == name && cl != HighlightTokenClass::NONE_) {
        values[cl] = escapeSeq.toString();
        break;
      }
    }
  }

  ANSIEscapeSeqMap seqMap(std::move(values));
  return seqMap;
}

// ##########################
// ##     LineRenderer     ##
// ##########################

static StringRef::size_type startsWithAnsiEscape(StringRef ref) {
  if (ref.size() > 2 && ref[0] == '\x1b' && ref[1] == '[') {
    for (StringRef::size_type i = 2; i < ref.size(); i++) {
      switch (ref[i]) {
      case 'A':
      case 'B':
      case 'C':
      case 'D':
      case 'E':
      case 'F':
      case 'G':
      case 'H':
      case 'J':
      case 'K':
      case 'S':
      case 'T':
      case 'f':
      case 'm':
        return i + 1;
      default:
        break;
      }
    }
  }
  return 0;
}

class TokenEmitterImpl : public TokenEmitter {
private:
  std::vector<std::pair<HighlightTokenClass, Token>> tokens;

public:
  explicit TokenEmitterImpl(StringRef source) : TokenEmitter(source) {}

  std::vector<std::pair<HighlightTokenClass, Token>> take() && { return std::move(this->tokens); }

private:
  void emit(TokenKind kind, Token token) override {
    this->tokens.emplace_back(toTokenClass(kind), token);
  }
};

void LineRenderer::renderWithANSI(StringRef prompt) {
  for (StringRef::size_type pos = 0; pos != StringRef::npos;) {
    auto r = prompt.find('\x1b', pos);
    auto sub = prompt.slice(pos, r);
    if (!this->render(sub, HighlightTokenClass::NONE_)) {
      return;
    }
    if (r != StringRef::npos) {
      auto remain = prompt.substr(r);
      if (auto len = startsWithAnsiEscape(remain)) {
        if (this->output) {
          *this->output += remain.substr(0, len);
        }
        pos = r + len;
      } else {
        if (!this->renderControlChar('\x1b', nullptr)) {
          return;
        }
        pos = r + 1;
      }
    } else {
      pos = r;
    }
  }
}

static bool nextIsLP(const StringRef source,
                     const std::vector<std::pair<HighlightTokenClass, Token>> &tokens,
                     unsigned int curIndex) {
  if (curIndex + 1 < tokens.size()) {
    const auto next = tokens[curIndex + 1].second;
    return source.substr(next.pos, next.size) == "(";
  }
  return false;
}

bool LineRenderer::renderScript(const StringRef source,
                                const std::function<bool(StringRef)> &errorCmdChecker) {
  // for syntax highlight
  TokenEmitterImpl tokenEmitter(source);
  auto error = tokenEmitter.tokenizeAndEmit();
  auto lex = tokenEmitter.getLexerPtr();
  const auto tokens = std::move(tokenEmitter).take();

  // render lines with highlight
  bool next = true;
  unsigned int curPos = 0;
  const bool supportErrorHighlight =
      errorCmdChecker && this->findColorCode(HighlightTokenClass::ERROR_);
  for (unsigned int i = 0; i < tokens.size(); i++) {
    Token token = tokens[i].second;
    assert(curPos <= token.pos);
    if (!this->render(source.slice(curPos, token.pos), HighlightTokenClass::NONE_)) {
      next = false;
      break;
    }
    curPos = token.endPos();
    const StringRef ref = source.substr(token.pos, token.size);
    HighlightTokenClass tokenClass = tokens[i].first;
    if (supportErrorHighlight && tokenClass == HighlightTokenClass::COMMAND &&
        !nextIsLP(source, tokens, i)) {
      if (!errorCmdChecker(ref)) {
        tokenClass = HighlightTokenClass::ERROR_;
      }
    }
    if (!this->render(ref, tokenClass)) {
      next = false;
      break;
    }
  }
  // render remain lines
  if (next && curPos < source.size()) {
    auto remain = source.substr(curPos);
    this->render(remain, HighlightTokenClass::NONE_);
  }

  // line continuation checking
  if (error) {
    if (error->getTokenKind() == TokenKind::EOS) {
      return false;
    } else {
      auto kind = error->getTokenKind();
      if (isUnclosedToken(kind) && kind != TokenKind::UNCLOSED_REGEX_LITERAL) {
        return false;
      }
    }
  } else if (!tokens.empty()) {
    auto token = tokens.back().second;
    auto last = lex->toStrRef(token);
    switch (tokens.back().first) {
    case HighlightTokenClass::NONE_:
      if (last.size() == 2 && last == "\\\n") {
        return false;
      }
      break;
    case HighlightTokenClass::COMMAND:
    case HighlightTokenClass::COMMAND_ARG:
      if (last.endsWith("\\\n")) {
        return false;
      }
      break;
    default:
      break;
    }
  }
  return true;
}

const std::string *LineRenderer::findColorCode(HighlightTokenClass tokenClass) const {
  if (this->escapeSeqMap && tokenClass != HighlightTokenClass::NONE_) {
    auto iter = this->escapeSeqMap->getValues().find(tokenClass);
    if (iter != this->escapeSeqMap->getValues().end()) {
      return &(iter->second);
    }
  }
  return nullptr;
}

static bool isControlChar(const GraphemeCluster &grapheme) {
  if ((grapheme.getRef().size() == 1 && isControlChar(grapheme.getRef()[0])) ||
      (grapheme.getRef().size() == 2 && grapheme.getRef() == "\r\n")) {
    return true;
  }
  return false;
}

static size_t getNewlineOffset(const GraphemeCluster &grapheme) {
  if (grapheme.getRef().size() == 1 && grapheme.getRef()[0] == '\n') {
    return 1;
  }
  if (grapheme.getRef().size() == 2 && grapheme.getRef() == "\r\n") {
    return 2;
  }
  return 0;
}

bool LineRenderer::render(StringRef ref, HighlightTokenClass tokenClass) {
  if (ref.empty()) {
    return true; // skip rendering
  }
  auto *colorCode = this->findColorCode(tokenClass);
  if (colorCode && this->output) {
    *this->output += *colorCode;
  }
  bool status = true;
  iterateGrapheme(ref, [&](const GraphemeCluster &grapheme) {
    if (auto offset = getNewlineOffset(grapheme)) {
      if (offset == 2) { // \r\n
        bool r = this->renderControlChar('\r', colorCode);
        (void)r; // ignore return value
      }
      if (colorCode && this->output) {
        *this->output += "\x1b[0m";
      }
      if (this->emitNewline) {
        if (this->output) {
          *this->output += "\r\n";
        }
        this->totalRows++;
        if (this->output) {
          this->output->append(this->initCols, ' ');
        }
        this->maxTotalCols = std::max(this->maxTotalCols, this->totalCols);
        this->totalCols = this->initCols;
      }
      if (colorCode && this->output) {
        *this->output += *colorCode;
      }
    } else if (isControlChar(grapheme)) {
      return this->renderControlChar(grapheme.getRef()[0], colorCode);
    } else {
      unsigned int width = getGraphemeWidth(this->ps, grapheme);
      if (this->totalCols + width > this->colLimit) { // line break
        switch (this->breakOp) {
        case LineBreakOp::SOFT_WRAP:
          this->handleSoftWrap(colorCode);
          break;
        case LineBreakOp::TRUNCATE:
          this->handleTruncate('.');
          return false;
        }
      }
      if (this->output) {
        if (grapheme.hasInvalid()) {
          *this->output += UnicodeUtil::REPLACEMENT_CHAR_UTF8;
        } else {
          *this->output += grapheme.getRef();
        }
      }
      this->totalCols += width;
      if (this->totalCols == this->colLimit && this->breakOp == LineBreakOp::SOFT_WRAP) {
        this->handleSoftWrap(colorCode);
      }
    }
    return true;
  });
  if (colorCode && status && this->output) {
    *this->output += "\x1b[0m";
  }
  return status;
}

bool LineRenderer::renderControlChar(int codePoint, const std::string *color) {
  assert(isControlChar(codePoint));
  if (codePoint == '\t') {
    unsigned int colLen = TAB_WIDTH - (this->totalCols % TAB_WIDTH);
    if (this->totalCols + colLen > this->colLimit) { // line break
      switch (this->breakOp) {
      case LineBreakOp::SOFT_WRAP:
        this->handleSoftWrap(color);
        colLen = TAB_WIDTH - this->totalCols % TAB_WIDTH; // re-compute tab stop
        break;
      case LineBreakOp::TRUNCATE:
        this->handleTruncate(' ');
        return false;
      }
    }
    if (this->output) {
      this->output->append(colLen, ' ');
    }
    this->totalCols += colLen;
    if (this->totalCols == this->colLimit && this->breakOp == LineBreakOp::SOFT_WRAP) {
      this->handleSoftWrap(color);
    }
  } else if (codePoint != '\n') {
    if (this->totalCols + 2 > this->colLimit) { // line break
      switch (this->breakOp) {
      case LineBreakOp::SOFT_WRAP:
        this->handleSoftWrap(color);
        break;
      case LineBreakOp::TRUNCATE:
        this->handleTruncate('.');
        return false;
      }
    }
    if (this->output) {
      auto v = static_cast<unsigned int>(codePoint);
      v ^= 64;
      assert(isCaretTarget(static_cast<int>(v)));
      *this->output += "^";
      *this->output += static_cast<char>(static_cast<int>(v));
    }
    this->totalCols += 2;
    if (this->totalCols == this->colLimit && this->breakOp == LineBreakOp::SOFT_WRAP) {
      this->handleSoftWrap(color);
    }
  }
  return true;
}

} // namespace arsh