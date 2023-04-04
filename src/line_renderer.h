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

namespace ydsh {

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
  UnicodeUtil::AmbiguousCharWidth eaw{UnicodeUtil::HALF_WIDTH};
  unsigned char flagSeqWidth{4};
  bool zwjSeqFallback{false};
  bool replaceInvalid{false};

  void setProperty(CharWidthProperty p, std::size_t len) {
    switch (p) {
    case CharWidthProperty::EAW:
      this->eaw = len == 2 ? UnicodeUtil::FULL_WIDTH : UnicodeUtil::HALF_WIDTH;
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
unsigned int getGraphemeWidth(const CharWidthProperties &ps, const GraphemeScanner::Result &ret);

enum class ColumnLenOp {
  NEXT,
  PREV,
};

struct ColumnLen {
  unsigned int byteSize; // consumed bytes
  unsigned int colSize;
};

class ColumnCounter {
private:
  const CharWidthProperties &ps;
  size_t totalColLen; // for tab stop. if reach \n, reset to 0

public:
  ColumnCounter(const CharWidthProperties &ps, size_t initColLen)
      : ps(ps), totalColLen(initColLen) {}

  size_t getTotalColLen() const { return this->totalColLen; }

  ColumnLen getCharLen(StringRef ref, ColumnLenOp op);

  ColumnLen getWordLen(StringRef ref, ColumnLenOp op);
};

/**
 * get length of last consumed grapheme cluster
 * @param ref
 * @param op
 * @param ps
 * @return
 */
inline ColumnLen getCharLen(StringRef ref, ColumnLenOp op, const CharWidthProperties &ps) {
  return ColumnCounter(ps, 0).getCharLen(ref, op);
}

/**
 * get length of last consumed word
 * @param ref
 * @param op
 * @param ps
 * @return
 */
inline ColumnLen getWordLen(StringRef ref, ColumnLenOp op, const CharWidthProperties &ps) {
  return ColumnCounter(ps, 0).getWordLen(ref, op);
}

inline StringRef::size_type startsWithAnsiEscape(StringRef ref) {
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

class ANSIEscapeSeqMap {
private:
  std::unordered_map<HighlightTokenClass, std::string> values;

public:
  ANSIEscapeSeqMap() = default;

  explicit ANSIEscapeSeqMap(std::unordered_map<HighlightTokenClass, std::string> &&values)
      : values(std::move(values)) {} // for testing

  static ANSIEscapeSeqMap fromString(StringRef setting);

  const auto &getValues() const { return this->values; }
};

struct CharWidthProperties;

/**
 * generate output string for terminal
 */
class LineRenderer {
public:
  enum class LineBreakOp {
    SOFT_WRAP,
    TRUNCATE,
  };

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
   * if 0, ignore newline characters and not increment totalRows(lineNum)
   */
  size_t lineNumLimit{static_cast<size_t>(-1)};

  LineBreakOp breakOp{LineBreakOp::SOFT_WRAP};

  /**
   * append to existing content
   */
  std::string &output;

public:
  LineRenderer(const CharWidthProperties &ps, size_t initCols, std::string &output,
               ObserverPtr<const ANSIEscapeSeqMap> escapeSeqMap = nullptr)
      : ps(ps), escapeSeqMap(escapeSeqMap), initCols(initCols), totalCols(initCols),
        output(output) {}

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
  bool renderControlChar(int codePoint);

  void handleSoftWrap() {
    this->totalCols = 0;
    this->totalRows++;
    this->output += "\r\n";
  }

  void handleTruncate(char pad) {
    this->output.append(this->maxCols - this->totalCols, pad);
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

    unsigned int itemLen() const { return this->len + (4 - this->len % 4) + this->tabs * 4; }
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
    if (this->index == 0) {
      this->curRow = this->getActualRows() - 1;
      this->index = this->items.size() - 1;
    } else {
      const auto logicalRows = this->getLogicalRows();
      const auto curCols = this->index / logicalRows;
      if (curCols == 0) {
        if (this->curRow > 0) {
          this->curRow--;
        }
        this->index--;
        this->index += logicalRows * (this->getPanes() - 1);
      } else {
        this->index -= logicalRows;
      }
    }
  }

  void moveCursorToRight() {
    if (this->index == this->items.size() - 1) {
      this->curRow = 0;
      this->index = 0;
    } else {
      const auto logicalRows = this->getLogicalRows();
      const auto curCols = this->index / logicalRows;
      if (curCols == this->getPanes() - 1) {
        if (this->curRow < this->getActualRows() - 1) {
          this->curRow++;
        }
        this->index++;
        this->index -= logicalRows * (this->getPanes() - 1);
      } else {
        this->index += logicalRows;
      }
    }
  }
};

} // namespace ydsh

#endif // YDSH_LINE_RENDERER_H
