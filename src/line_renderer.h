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

#ifndef YDSH_LINE_RENDERER_H
#define YDSH_LINE_RENDERER_H

#include "highlighter_base.h"
#include "misc/grapheme.hpp"

namespace arsh {

// high level api for unicode-aware character op

#define EACH_CHAR_WIDTH_PROPERTY(OP)                                                               \
  OP(EAW, "○")                                                                                     \
  OP(EMOJI_FLAG_SEQ, "🇯🇵")                                                                         \
  OP(EMOJI_ZWJ_SEQ, "👩🏼‍🏭")

enum class CharWidthProperty {
#define GEN_ENUM(E, S) E,
  EACH_CHAR_WIDTH_PROPERTY(GEN_ENUM)
#undef GEN_ENUM
};

constexpr unsigned int getCharWidthPropertyLen() {
  constexpr const CharWidthProperty table[] = {
#define GEN_ENUM(E, S) CharWidthProperty::E,
      EACH_CHAR_WIDTH_PROPERTY(GEN_ENUM)
#undef GEN_ENUM
  };
  return std::size(table);
}

using CharWidthPropertyList =
    std::array<std::pair<CharWidthProperty, const char *>, getCharWidthPropertyLen()>;

const CharWidthPropertyList &getCharWidthPropertyList();

struct CharWidthProperties {
  AmbiguousCharWidth eaw{AmbiguousCharWidth::HALF};
  unsigned char flagSeqWidth{4};
  bool zwjSeqFallback{false};
  bool replaceInvalid{false};

  void setProperty(CharWidthProperty p, std::size_t len) {
    switch (p) {
    case CharWidthProperty::EAW:
      this->eaw = len == 2 ? AmbiguousCharWidth::FULL : AmbiguousCharWidth::HALF;
      break;
    case CharWidthProperty::EMOJI_FLAG_SEQ:
      this->flagSeqWidth = len;
      break;
    case CharWidthProperty::EMOJI_ZWJ_SEQ:
      this->zwjSeqFallback = len > 2;
      break;
    }
  }
};

/**
 * get width of a grapheme cluster
 * @param ps
 * @param ret
 * @return
 */
unsigned int getGraphemeWidth(const CharWidthProperties &ps, const GraphemeCluster &ret);

class ANSIEscapeSeqMap {
private:
  std::unordered_map<HighlightTokenClass, std::string> values;

public:
  ANSIEscapeSeqMap() = default;

  explicit ANSIEscapeSeqMap(std::unordered_map<HighlightTokenClass, std::string> &&values)
      : values(std::move(values)) {} // for testing

  static ANSIEscapeSeqMap fromString(StringRef setting);

  static bool checkSGRSeq(StringRef seq);

  const auto &getValues() const { return this->values; }
};

/**
 * generate output string for terminal
 */
class LineRenderer {
public:
  enum class LineBreakOp {
    SOFT_WRAP,
    TRUNCATE,
  };

  static constexpr unsigned int TAB_WIDTH = 4;

private:
  const CharWidthProperties &ps;

  /**
   * may be null
   */
  const ObserverPtr<const ANSIEscapeSeqMap> escapeSeqMap;

  const size_t initCols;

  size_t totalCols{0};

  size_t totalRows{0};

  size_t lineNum{0};

  size_t maxCols{static_cast<size_t>(-1)};

  /**
   * if 0, ignore newline characters and not increment totalRows/lineNum
   */
  size_t lineNumLimit{static_cast<size_t>(-1)};

  LineBreakOp breakOp{LineBreakOp::SOFT_WRAP};

  /**
   * append to existing content
   */
  ObserverPtr<std::string> output;

public:
  LineRenderer(const CharWidthProperties &ps, size_t initCols, ObserverPtr<std::string> output,
               ObserverPtr<const ANSIEscapeSeqMap> escapeSeqMap)
      : ps(ps), escapeSeqMap(escapeSeqMap), initCols(initCols), totalCols(initCols),
        output(output) {}

  LineRenderer(const CharWidthProperties &ps, size_t initCols, std::string &output,
               ObserverPtr<const ANSIEscapeSeqMap> escapeSeqMap = nullptr)
      : LineRenderer(ps, initCols, makeObserver(output), escapeSeqMap) {}

  LineRenderer(const CharWidthProperties &ps, size_t initCols)
      : LineRenderer(ps, initCols, nullptr, nullptr) {}

  void setMaxCols(size_t limit) { this->maxCols = limit; }

  void setLineNumLimit(size_t limit) { this->lineNumLimit = limit; }

  size_t getTotalCols() const { return this->totalCols; }

  size_t getTotalRows() const { return this->totalRows; }

  void setLineBreakOp(LineBreakOp op) { this->breakOp = op; }

  /**
   * render lines with color code (ansi escape sequence).
   * ansi escape sequences are not quoted
   * @param prompt
   */
  void renderWithANSI(StringRef prompt);

  /**
   * render script (perform syntax highlight, line continuation check)
   * @param source
   * @return
   * reach incomplete input (need more characters), return false
   */
  bool renderScript(StringRef source);

  void renderLines(StringRef source) { this->render(source, HighlightTokenClass::NONE); }

private:
  const std::string *findColorCode(HighlightTokenClass tokenClass) const;

  /**
   *
   * @param ref
   * @param tokenClass
   * @return
   * if reach lineNumLimit or colLenLimit, return false
   */
  bool render(StringRef ref, HighlightTokenClass tokenClass);

  /**
   *
   * @param codePoint
   * @return
   * if reach lineNumLimit or colLenLimit, return false
   */
  bool renderControlChar(int codePoint, const std::string *color);

  void handleSoftWrap(const std::string *color) {
    this->totalCols = 0;
    this->totalRows++;
    if (this->output) {
      if (color) {
        *this->output += "\x1b[0m";
      }
      *this->output += "\r\n";
      if (color) {
        *this->output += *color;
      }
    }
  }

  void handleTruncate(char pad) {
    if (this->output) {
      this->output->append(this->maxCols - this->totalCols, pad);
    }
    this->totalCols = this->maxCols;
  }
};

class ArrayObject;

/**
 * for completion candidates paging
 */
class ArrayPager {
public:
  struct WindowSize {
    unsigned int rows{24};
    unsigned int cols{80};

    bool operator==(WindowSize o) const { return this->rows == o.rows && this->cols == o.cols; }

    bool operator!=(WindowSize o) const { return !(*this == o); }
  };

  struct ItemEntry {
    unsigned int len;  // actual item columns size
    unsigned int tabs; // number of extra tab characters

    unsigned int itemLen() const {
      return this->len + (LineRenderer::TAB_WIDTH - this->len % LineRenderer::TAB_WIDTH) +
             this->tabs * LineRenderer::TAB_WIDTH;
    }
  };

private:
  static constexpr unsigned int ROW_RATIO = 40;
  static constexpr unsigned int MAX_PANE_NUM = 4;

  const CharWidthProperties &ps;
  const ArrayObject &obj; // must be [String]
  WindowSize winSize{0, 0};
  const FlexBuffer<ItemEntry> items; // pre-computed item column size
  const unsigned int maxLenIndex;    // index of item with longest len
  unsigned int paneLen{0};           // pager pane length (paneLen * pages < window col size)
  unsigned int rows{0};              // pager row size (less than window row size)
  unsigned int panes{0};             // number of pager pane
  unsigned int index{0};             // index of currently selected item
  unsigned int curRow{0};            // row of currently selected item (related to rows)
  bool showCursor{true};             // if true, render cursor
  bool showPageNum{false};           // if true, render page number

  ArrayPager(const CharWidthProperties &ps, const ArrayObject &obj, FlexBuffer<ItemEntry> &&items,
             unsigned int maxIndex, WindowSize winSize)
      : ps(ps), obj(obj), items(std::move(items)), maxLenIndex(maxIndex) {
    this->updateWinSize(winSize);
  }

public:
  static ArrayPager create(const ArrayObject &obj, const CharWidthProperties &ps,
                           WindowSize winSize);

  /**
   * update windows size.
   * if windows size changed, recompute panes, rows
   * @param size
   */
  void updateWinSize(WindowSize size);

  void setShowCursor(bool set) { this->showCursor = set; }

  WindowSize getWinSize() const { return this->winSize; }

  unsigned int getIndex() const { return this->index; }

  unsigned int getPanes() const { return this->panes; }

  unsigned int getPaneLen() const { return this->paneLen; }

  /**
   * get max row size of pager
   * @return
   */
  unsigned int getRows() const { return this->rows; }

  unsigned int getCurRow() const { return this->curRow; }

  /**
   * get logical row size
   * @return
   */
  unsigned int getLogicalRows() const {
    return static_cast<unsigned int>(this->items.size() / this->panes) +
           (this->items.size() % this->panes == 0 ? 0 : 1);
  }

  /**
   * get row size of actually rendered
   * @return
   */
  unsigned int getActualRows() const { return std::min(this->getLogicalRows(), this->getRows()); }

  unsigned int getRenderedRows() const {
    return this->getActualRows() + (this->showPageNum ? 1 : 0);
  }

  /**
   * actual rendering function
   * @param out
   * append rendering result to it
   */
  void render(std::string &out) const;

  // for pager api
  void moveCursorToForward() {
    if (this->index == 0) {
      this->curRow = this->getActualRows() - 1;
      this->index = this->items.size() - 1;
      if (unsigned int offset = this->index % this->getLogicalRows(); offset < this->curRow) {
        this->curRow = offset;
      }
    } else {
      if (this->curRow == 0) {
        if (this->index % this->getLogicalRows() > 0) {
          this->curRow = 0;
        } else {
          this->curRow = this->getActualRows() - 1;
        }
      } else {
        this->curRow--;
      }
      this->index--;
    }
  }

  void moveCursorToNext() {
    if (this->index == this->items.size() - 1) {
      this->curRow = 0;
      this->index = 0;
    } else {
      if (this->curRow == this->getActualRows() - 1) {
        unsigned int logicalRows = this->getLogicalRows();
        if (this->index % logicalRows == logicalRows - 1) {
          this->curRow = 0;
        } else {
          this->curRow = this->getActualRows() - 1;
        }
      } else {
        this->curRow++;
      }
      this->index++;
    }
  }

  void moveCursorToLeft() {
    const auto logicalRows = this->getLogicalRows();
    const auto curCols = this->index / logicalRows;
    if (curCols > 0) {
      this->index -= logicalRows;
    } else {
      unsigned int nextIndex = this->index - 1 + logicalRows * (this->getPanes() - 1);
      unsigned int curLogicalRows = this->index % logicalRows;
      if (this->curRow > 0) {
        this->curRow--;
      }
      if (curLogicalRows == 0) {
        nextIndex = logicalRows * this->getPanes() - 1;
        this->curRow = this->getActualRows() - 1;
      }
      while (nextIndex >= this->items.size()) {
        nextIndex -= logicalRows;
      }
      this->index = nextIndex;
    }
  }

  void moveCursorToRight() {
    const auto logicalRows = this->getLogicalRows();
    const auto curCols = this->index / logicalRows;
    if (curCols < this->getPanes() - 1 && this->index + logicalRows < this->items.size()) {
      this->index += logicalRows;
    } else {
      unsigned int nextIndex = this->index + 1 - logicalRows * curCols;
      unsigned int curLogicalRows = this->index % logicalRows;
      if (nextIndex < this->items.size() && curLogicalRows < logicalRows - 1) {
        if (this->curRow < this->getActualRows() - 1) {
          this->curRow++;
        }
        this->index = nextIndex;
      } else {
        this->curRow = 0;
        this->index = 0;
      }
    }
  }
};

} // namespace arsh

#endif // YDSH_LINE_RENDERER_H
