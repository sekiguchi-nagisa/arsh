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

#ifndef ARSH_COMP_CANDIDATES_H
#define ARSH_COMP_CANDIDATES_H

#include "line_editor.h"
#include "object.h"

namespace arsh {

// for runtime code completion

class CompCandidateObject : public ObjectWithRtti<ObjectKind::CompCandidate> {
private:
  const unsigned int allocSize; // len(candidate) + len(signature)
  const unsigned int size;      // len(candidate)

  char payload[]; // candidate + signature

  CompCandidateObject(StringRef cadidate, StringRef signature)
      : ObjectWithRtti(TYPE::String), allocSize(cadidate.size() + signature.size()),
        size(cadidate.size()) {
    memcpy(this->payload, cadidate.data(), cadidate.size());
    memcpy(this->payload + this->size, signature.data(), signature.size());
  }

public:
  static ObjPtr<CompCandidateObject> create(StringRef candidate, StringRef signature) {
    assert(candidate.size() <= INT32_MAX);
    assert(signature.size() <= INT32_MAX);
    const unsigned int allocSize = candidate.size() + signature.size();
    void *ptr = malloc(sizeof(CompCandidateObject) + sizeof(char) * allocSize);
    auto *obj = new (ptr) CompCandidateObject(candidate, signature);
    return ObjPtr<CompCandidateObject>(obj);
  }

  static void operator delete(void *ptr) noexcept { // NOLINT
    free(ptr);
  }

  unsigned int candidateSize() const { return this->size; }

  unsigned int signatureSize() const { return this->allocSize - this->size; }

  StringRef candidate() const { return {this->payload, this->candidateSize()}; }

  StringRef signature() const { return {this->payload + this->size, this->signatureSize()}; }
};

class CompCandidatesObject : public ObjectWithRtti<ObjectKind::CompCandidates> {
private:
  std::vector<Value> values;

public:
  static constexpr size_t MAX_SIZE = SYS_LIMIT_ARRAY_MAX;

  CompCandidatesObject() : ObjectWithRtti(TYPE::Candidates) {}

  const auto &getValues() const { return this->values; }

  unsigned int size() const { return this->values.size(); }

  /**
   * \brief
   * @param state
   * @param candidate
   * must be String or CompCandidate
   * @return if has error, return false
   */
  [[nodiscard]] bool add(ARState &state, Value &&candidate);

  [[nodiscard]] bool add(ARState &state, StringRef candidate, StringRef signature);
};

/**
 * for completion candidates paging
 */
class CompletionPager {
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

  CompletionPager(const CharWidthProperties &ps, const ArrayObject &obj,
                  FlexBuffer<ItemEntry> &&items, unsigned int maxIndex, WindowSize winSize)
      : ps(ps), obj(obj), items(std::move(items)), maxLenIndex(maxIndex) {
    this->updateWinSize(winSize);
  }

public:
  static CompletionPager create(const ArrayObject &obj, const CharWidthProperties &ps,
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

#endif // ARSH_COMP_CANDIDATES_H
