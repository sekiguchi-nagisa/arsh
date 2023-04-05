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
#include "object.h"

namespace ydsh {

const CharWidthPropertyList &getCharWidthPropertyList() {
  static CharWidthPropertyList table = {{
#define GEN_ENUM(E, S) {CharWidthProperty::E, S},
      EACH_CHAR_WIDTH_PROPERTY(GEN_ENUM)
#undef GEN_ENUM
  }};
  return table;
}

unsigned int getGraphemeWidth(const CharWidthProperties &ps, const GraphemeScanner::Result &ret) {
  unsigned int width = 0;
  unsigned int flagSeqCount = 0;
  for (unsigned int i = 0; i < ret.codePointCount; i++) {
    auto codePoint = ret.codePoints[i];
    if (ps.replaceInvalid && codePoint < 0) {
      codePoint = UnicodeUtil::REPLACEMENT_CHAR_CODE;
    } else if (ret.breakProperties[i] == GraphemeBoundary::BreakProperty::Regional_Indicator) {
      flagSeqCount++;
    }
    int w = UnicodeUtil::width(codePoint, ps.eaw);
    if (w > 0) {
      width += w;
    }
  }
  if (flagSeqCount == 2) {
    return ps.flagSeqWidth;
  }
  if (width > 2 && ps.zwjSeqFallback) {
    return width;
  }
  return width < 2 ? width : 2;
}

static bool isControlChar(const GraphemeScanner::Result &grapheme) {
  if ((grapheme.codePointCount == 1 && isControlChar(grapheme.codePoints[0])) ||
      (grapheme.codePointCount == 2 && grapheme.codePoints[0] == '\r' &&
       grapheme.codePoints[1] == '\n')) {
    return true;
  }
  return false;
}

ColumnLen ColumnCounter::getCharLen(StringRef ref, ColumnLenOp op) {
  const auto limit = op == ColumnLenOp::NEXT ? 1 : static_cast<size_t>(-1);
  unsigned int lastByteSize = 0;
  unsigned int lastColsLen = 0;
  iterateGraphemeUntil(ref, limit, [&](const GraphemeScanner::Result &grapheme) {
    lastByteSize = static_cast<unsigned int>(grapheme.ref.size());
    if (isControlChar(grapheme)) {
      auto codePoint = grapheme.codePoints[0];
      if (codePoint == '\t') { // max tab len is 4
        lastColsLen = 4 - this->totalColLen % 4;
      } else if (codePoint == '\n') {
        lastColsLen = 0; // ignore newline
      } else {
        lastColsLen = 2; // caret notation, such as ^@
      }
    } else {
      lastColsLen = getGraphemeWidth(this->ps, grapheme);
    }
    this->totalColLen += lastColsLen;
  });
  return ColumnLen{
      .byteSize = lastByteSize,
      .colSize = lastColsLen,
  };
}

// ##############################
// ##     ANSIEscapeSeqMap     ##
// ##############################

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
    auto escapeSeq = entry.substr(retPos + 1); // FIXME: check escape sequence format?

    for (auto &e : getHighlightTokenEntries()) {
      if (element == e.second && !escapeSeq.empty()) {
        values[e.first] = escapeSeq.toString();
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

class TokenEmitterImpl : public TokenEmitter {
private:
  std::vector<std::pair<HighlightTokenClass, Token>> tokens;

public:
  explicit TokenEmitterImpl(StringRef source) : TokenEmitter(source) {}

  std::vector<std::pair<HighlightTokenClass, Token>> take() && { return std::move(this->tokens); }

private:
  void emit(HighlightTokenClass tokenClass, Token token) override {
    this->tokens.emplace_back(tokenClass, token);
  }
};

void LineRenderer::renderWithANSI(StringRef prompt) {
  for (StringRef::size_type pos = 0; pos != StringRef::npos;) {
    auto r = prompt.find('\x1b', pos);
    auto sub = prompt.slice(pos, r);
    if (!this->render(sub, HighlightTokenClass::NONE)) {
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
        if (!this->renderControlChar('\x1b')) {
          return;
        }
        pos = r + 1;
      }
    } else {
      pos = r;
    }
  }
}

bool LineRenderer::renderScript(const StringRef source) {
  // for syntax highlight
  TokenEmitterImpl tokenEmitter(source);
  auto error = tokenEmitter.tokenizeAndEmit();
  auto lex = tokenEmitter.getLexerPtr();
  auto tokens = std::move(tokenEmitter).take();

  // render lines with highlight
  bool next = true;
  unsigned int curPos = 0;
  for (auto &e : tokens) {
    Token token = e.second;
    assert(curPos <= token.pos);
    if (!this->render(source.slice(curPos, token.pos), HighlightTokenClass::NONE)) {
      next = false;
      break;
    }
    curPos = token.endPos();
    if (!this->render(source.substr(token.pos, token.size), e.first)) {
      next = false;
      break;
    }
  }
  // render remain lines
  if (next && curPos < source.size()) {
    auto remain = source.substr(curPos);
    this->render(remain, HighlightTokenClass::NONE);
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
    case HighlightTokenClass::NONE:
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
  if (this->escapeSeqMap) {
    auto iter = this->escapeSeqMap->getValues().find(tokenClass);
    if (iter != this->escapeSeqMap->getValues().end()) {
      return &(iter->second);
    }
  }
  return nullptr;
}

static size_t getNewlineOffset(const GraphemeScanner::Result &grapheme) {
  if (grapheme.codePointCount == 1 && grapheme.codePoints[0] == '\n') {
    return 1;
  }
  if (grapheme.codePointCount == 2 && grapheme.codePoints[0] == '\r' &&
      grapheme.codePoints[1] == '\n') {
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
  iterateGrapheme(ref, [&](const GraphemeScanner::Result &grapheme) {
    if (auto offset = getNewlineOffset(grapheme)) {
      if (offset == 2) { // \r\n
        bool r = this->renderControlChar('\r');
        (void)r; // ignore return value
      }
      if (colorCode && this->output) {
        *this->output += "\x1b[0m";
      }
      if (this->lineNumLimit) {
        if (this->output) {
          *this->output += "\r\n";
        }
        this->totalRows++;
        if (++this->lineNum >= this->lineNumLimit) {
          status = false;
          return false;
        }
        if (this->output) {
          this->output->append(this->initCols, ' ');
        }
        this->totalCols = this->initCols;
      }
      if (colorCode && this->output) {
        *this->output += *colorCode;
      }
    } else if (isControlChar(grapheme)) {
      return this->renderControlChar(grapheme.codePoints[0]);
    } else {
      unsigned int width = getGraphemeWidth(this->ps, grapheme);
      if (this->totalCols + width > this->maxCols) { // line break
        switch (this->breakOp) {
        case LineBreakOp::SOFT_WRAP:
          this->handleSoftWrap();
          break;
        case LineBreakOp::TRUNCATE:
          this->handleTruncate('.');
          return false;
        }
      }
      if (this->output) {
        if (grapheme.hasInvalid) {
          assert(grapheme.codePointCount == 1);
          *this->output += UnicodeUtil::REPLACEMENT_CHAR_UTF8;
        } else {
          *this->output += grapheme.ref;
        }
      }
      this->totalCols += width;
      if (this->totalCols == this->maxCols && this->breakOp == LineBreakOp::SOFT_WRAP) {
        this->handleSoftWrap();
      }
    }
    return true;
  });
  if (colorCode && status && this->output) {
    *this->output += "\x1b[0m";
  }
  return status;
}

bool LineRenderer::renderControlChar(int codePoint) {
  assert(isControlChar(codePoint));
  if (codePoint == '\t') {
    unsigned int colLen = 4 - this->totalCols % 4;
    if (this->totalCols + colLen > this->maxCols) { // line break
      switch (this->breakOp) {
      case LineBreakOp::SOFT_WRAP:
        this->handleSoftWrap();
        colLen = 4 - this->totalCols % 4; // re-compute tab stop
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
    if (this->totalCols == this->maxCols && this->breakOp == LineBreakOp::SOFT_WRAP) {
      this->handleSoftWrap();
    }
  } else if (codePoint != '\n') {
    if (this->totalCols + 2 > this->maxCols) { // line break
      switch (this->breakOp) {
      case LineBreakOp::SOFT_WRAP:
        this->handleSoftWrap();
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
    if (this->totalCols == this->maxCols && this->breakOp == LineBreakOp::SOFT_WRAP) {
      this->handleSoftWrap();
    }
  }
  return true;
}

// ########################
// ##     ArrayPager     ##
// ########################

ArrayPager ArrayPager::create(const ArrayObject &obj, const CharWidthProperties &ps,
                              WindowSize winSize) {
  unsigned int maxLen = 0;
  unsigned int maxIndex = 0;
  FlexBuffer<ItemEntry> items;
  items.reserve(obj.size());
  for (auto &e : obj.getValues()) {
    const StringRef ref = e.asStrRef();
    LineRenderer renderer(ps, 0);
    renderer.setLineNumLimit(0); // ignore newline
    renderer.renderLines(ref);
    auto colLen = static_cast<unsigned int>(renderer.getTotalCols());
    items.push_back(ItemEntry{
        .len = colLen,
        .tabs = 0,
    });
    if (colLen > maxLen) {
      maxLen = colLen;
      maxIndex = items.size() - 1;
    }
  }

  // compute extra tabs
  const auto paneSize = items[maxIndex].itemLen();
  assert(paneSize % 4 == 0);
  for (auto &e : items) {
    auto padLen = paneSize - e.itemLen();
    assert(padLen % 4 == 0);
    e.tabs = padLen / 4;
  }
  return {ps, obj, std::move(items), maxIndex, winSize};
}

void ArrayPager::updateWinSize(WindowSize size) {
  if (this->getWinSize() == size) {
    return; // no update
  }
  this->winSize = size;
  this->rows = (this->winSize.rows * ROW_RATIO) / 100;
  if (this->rows == 0) {
    this->rows = 1;
  }
  this->paneLen = this->items[this->maxLenIndex].itemLen();
  this->panes = this->winSize.cols / this->paneLen;
  if (this->panes == 0) {
    this->panes = 1;
  } else if (this->panes > MAX_PANE_NUM) {
    this->panes = MAX_PANE_NUM;
  }
  if (this->curRow > this->rows) {
    this->curRow = this->rows - 1;
  }
  if (this->panes == 1) {
    unsigned int colLimit = (this->winSize.cols / 4) * 4; // truncate to multiple of 4
    if (this->paneLen > colLimit) {
      this->paneLen = colLimit; // larger than window size
    }
  }
}

static void renderItem(LineRenderer &renderer, StringRef ref, const ArrayPager::ItemEntry &e) {
  renderer.renderLines(ref);
  renderer.renderLines("\t");
  for (unsigned int i = 0; i < e.tabs; i++) {
    renderer.renderLines("\t");
  }
}

void ArrayPager::render(std::string &out) const {
  /**
   * resolve start index.
   * example,
   *
   * 0 4 8
   * 1 5 9
   * 2 6 10
   * 3 7
   *
   * panes=3, maxRowWSize=4, index=9, curRow=1,
   * ==>> startIndex=0
   */
  const unsigned int maxRowSize = this->getLogicalRows();
  unsigned int startIndex = this->index % maxRowSize;
  assert(startIndex >= this->curRow);
  startIndex -= this->curRow;
  const unsigned int actualRows = this->getActualRows();

  LineRenderer renderer(this->ps, 0, out);
  if (this->getPanes() == 1) {
    renderer.setMaxCols(this->getPaneLen());
    renderer.setLineBreakOp(LineRenderer::LineBreakOp::TRUNCATE);
  }
  for (unsigned int i = 0; i < actualRows; i++) {
    renderer.setLineNumLimit(0);                     // ignore newlines
    for (unsigned int j = 0; j < this->panes; j++) { // render row
      const unsigned int actualIndex = startIndex + i + j * maxRowSize;
      if (actualIndex >= this->items.size()) {
        break;
      }
      if (actualIndex == this->index && this->showCursor) {
        renderer.renderWithANSI("\x1b[7m");
      }
      auto ref = this->obj.getValues()[actualIndex].asStrRef();
      renderItem(renderer, ref, this->items[actualIndex]);
      if (actualIndex == this->index && this->showCursor) {
        renderer.renderWithANSI("\x1b[0m");
      }
    }
    renderer.setLineNumLimit(static_cast<size_t>(-1)); // re-enable newlines
    renderer.renderLines("\n");
  }
}

} // namespace ydsh