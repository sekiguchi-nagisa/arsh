/*
 * Copyright (C) 2024 Nagisa Sekiguchi
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

#ifndef ARSH_PAGER_H
#define ARSH_PAGER_H

#include "candidates.h"
#include "keybind.h"
#include "misc/buffer.hpp"

namespace arsh {

class LineRenderer;
struct CharWidthProperties;
class AtomicSigSet;

/**
 * for completion candidates paging
 */
class ArrayPager {
public:
  static constexpr size_t TAB_WIDTH = SYS_LINE_RENDERER_TAB_WIDTH;

  struct WindowSize {
    unsigned short rows{24};
    unsigned short cols{80};

    bool operator==(WindowSize o) const { return this->rows == o.rows && this->cols == o.cols; }

    bool operator!=(WindowSize o) const { return !(*this == o); }
  };

  struct ItemEntry {
    unsigned int len;       // actual item columns size
    unsigned short tabs;    // number of extra tab characters
    unsigned char leftPad;  // for left padding size of signature
    unsigned char rightPad; // for right padding size of the candidate

    unsigned int itemLen() const {
      return this->len + (TAB_WIDTH - this->len % TAB_WIDTH) + (this->tabs * TAB_WIDTH);
    }
  };

private:
  static constexpr unsigned int DEFAULT_ROW_RATIO = 40;
  static constexpr unsigned int COL_MARGIN = 1;
  static constexpr unsigned int ROW_MARGIN = 3; // line + pager-item + newline

  const CandidatesObject &obj;
  WindowSize winSize{0, 0};
  const unsigned int rowRatio;
  FlexBuffer<unsigned int> filteredItemIndexes; // maintains filtered item indexes
  const FlexBuffer<ItemEntry> items;            // pre-computed item column size
  const unsigned int maxLenItemIndex;           // index of item with longest len
  unsigned int paneLen{0}; // pager pane length (paneLen * pages < window col size)
  unsigned int rows{0};    // pager row size (less than window row size)
  unsigned int panes{0};   // number of pager panes
  unsigned int index{0};   // index of the currently selected filtered item
  unsigned int curRow{0};  // row of currently selected item (related to rows)
  bool showPager{true};    // if true, render pager
  bool showCursor{true};   // if true, render cursor
  bool showRowNum{false};  // if true, render the row number
  bool showDesc{true};     // if true, render description/signature
  bool filterMode{false};  // if true, enable search filter mode
  std::string query;       // for search filter

  ArrayPager(const CandidatesObject &obj, FlexBuffer<ItemEntry> &&items, unsigned int maxIndex,
             WindowSize winSize, unsigned int rowRatio)
      : obj(obj), rowRatio(rowRatio), items(std::move(items)), maxLenItemIndex(maxIndex) {
    this->updateWinSize(winSize);
  }

public:
  static ArrayPager create(const CandidatesObject &obj, const CharWidthProperties &ps,
                           WindowSize winSize, unsigned int rowRatio = DEFAULT_ROW_RATIO);

  /**
   * update windows size.
   * if windows size changed, recompute panes, rows
   * @param size
   */
  void updateWinSize(const WindowSize size) {
    if (this->getWinSize() == size) {
      return; // no update
    }
    this->winSize = size;
    this->updateLayout();
  }

  void setQuery(StringRef ref) {
    if (this->isFilterMode()) {
      this->query = ref.toString();
      this->rebuildFilteredItemIndex();
    }
  }

  void pushQueryChar(StringRef grapheme);

  void popQueryChar();

  const auto &getQuery() const { return this->query; }

  void setShowCursor(bool set) { this->showCursor = set; }

  WindowSize getWinSize() const { return this->winSize; }

  bool isFilterMode() const { return this->filterMode; }

  bool tryToEnableFilterMode() {
    this->filterMode = true; // try to set mode
    if (this->getRows() < 2) {
      this->filterMode = false; // getActualRows() must be larger than 0
    }
    if (this->isFilterMode()) {
      this->rebuildFilteredItemIndex(); // fill filtered index
      return true;
    }
    return false;
  }

  void disableFilterMode() {
    this->filterMode = false;
    this->filteredItemIndexes.clear();
    this->updateLayout();
  }

  unsigned int getRowRatio() const { return this->rowRatio; }

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
    const unsigned int size = this->filteredItemSize();
    return (size / this->panes) + (size % this->panes == 0 ? 0 : 1);
  }

  /**
   * get row size of actually rendered pager items
   * @return
   */
  unsigned int getActualRows() const {
    unsigned int actual = std::min(this->getLogicalRows(), this->getRows());
    if (this->isFilterMode() && actual + 1 > this->getRows()) {
      actual--;
    }
    return actual;
  }

  /**
   * get row size of actually rendered (include row-number indicator)
   * @return
   */
  unsigned int getRenderedRows() const {
    return this->showPager
               ? this->getActualRows() + (this->showRowNum ? 1 : 0) + (this->isFilterMode() ? 1 : 0)
               : 0;
  }

  StringRef getCurCandidate() const {
    return this->obj.getCandidateAt(this->toActualItemIndex(this->getIndex()));
  }

  CandidateAttr getCurCandidateAttr() const {
    return this->obj.getAttrAt(this->toActualItemIndex(this->getIndex()));
  }

  /**
   * actual rendering function
   * @param renderer
   */
  void render(LineRenderer &renderer) const;

  // for pager api
  void moveCursorToForward() {
    if (this->index == 0) {
      this->curRow = this->getActualRows() - 1;
      this->index = this->filteredItemSize() - 1;
      const unsigned int offset = this->index % this->getLogicalRows();
      this->curRow = std::min(this->curRow, offset);
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
    if (this->index == this->filteredItemSize() - 1) {
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
      unsigned int nextIndex = this->index - 1 + (logicalRows * (this->getPanes() - 1));
      unsigned int curLogicalRows = this->index % logicalRows;
      if (this->curRow > 0) {
        this->curRow--;
      }
      if (curLogicalRows == 0) {
        nextIndex = logicalRows * this->getPanes() - 1;
        this->curRow = this->getActualRows() - 1;
      }
      while (nextIndex >= this->filteredItemSize()) {
        nextIndex -= logicalRows;
      }
      this->index = nextIndex;
    }
  }

  void moveCursorToRight() {
    const auto logicalRows = this->getLogicalRows();
    const auto curCols = this->index / logicalRows;
    if (curCols < this->getPanes() - 1 && this->index + logicalRows < this->filteredItemSize()) {
      this->index += logicalRows;
    } else {
      unsigned int nextIndex = this->index + 1 - (logicalRows * curCols);
      unsigned int curLogicalRows = this->index % logicalRows;
      if (nextIndex < this->filteredItemSize() && curLogicalRows < logicalRows - 1) {
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

private:
  void updateLayout();

  void rebuildFilteredItemIndex();

  unsigned int filteredItemSize() const {
    return this->isFilterMode() ? this->filteredItemIndexes.size() : this->items.size();
  }

  unsigned int toActualItemIndex(unsigned int filteredIndex) const {
    return this->isFilterMode() ? this->filteredItemIndexes[filteredIndex] : filteredIndex;
  }

  bool matchItemAt(unsigned int itemIndex) const {
    StringRef candidate = this->obj.getCandidateAt(itemIndex);
    return candidate.contains(this->query);
  }
};

EditActionStatus waitPagerAction(ArrayPager &pager, const KeyBindings &bindings,
                                 KeyCodeReader &reader, const AtomicSigSet &watchSigSet);

class HistRotator {
private:
  static_assert(SYS_LIMIT_HIST_SIZE < SYS_LIMIT_ARRAY_MAX);
  static_assert(SYS_LIMIT_HIST_SIZE < UINT32_MAX);

  std::unordered_map<unsigned int, Value> oldEntries;
  ObjPtr<ArrayObject> history;
  int histIndex{0};
  unsigned int maxSize{SYS_LIMIT_HIST_SIZE};

public:
  enum class Op : unsigned char {
    PREV,
    NEXT,
  };

  explicit HistRotator(ObjPtr<ArrayObject> history);

  ~HistRotator() { this->revertAll(); }

  void setMaxSize(unsigned int size) {
    this->maxSize = std::min(size, static_cast<unsigned int>(SYS_LIMIT_HIST_SIZE));
  }

  unsigned int getMaxSize() const { return this->maxSize; }

  void revertAll();

  explicit operator bool() const { return this->history && this->history->size() > 0; }

  /**
   * save current buffer and get next entry
   * @param curBuf
   * @param op
   */
  bool rotate(StringRef &curBuf, Op op);

private:
  void truncateUntilLimit(bool beforeAppend = false);

  bool save(ssize_t index, StringRef curBuf);
};

} // namespace arsh

#endif // ARSH_PAGER_H
