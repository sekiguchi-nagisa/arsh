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

#include "pager.h"
#include "core.h"
#include "line_renderer.h"
#include "misc/format.hpp"

namespace arsh {

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
  assert(paneSize % LineRenderer::TAB_WIDTH == 0);
  for (auto &e : items) {
    auto padLen = paneSize - e.itemLen();
    assert(padLen % LineRenderer::TAB_WIDTH == 0);
    e.tabs = padLen / LineRenderer::TAB_WIDTH;
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
    unsigned int colLimit = (this->winSize.cols / LineRenderer::TAB_WIDTH) *
                            LineRenderer::TAB_WIDTH; // truncate to multiple of TAB_WIDTH
    if (this->paneLen > colLimit) {
      this->paneLen = colLimit; // larger than window size
    }
  }
  this->showPageNum = false;
  if (this->getActualRows() + 1 <= this->winSize.rows &&
      this->getLogicalRows() > this->getActualRows()) {
    // rows 23-111/111
    unsigned int footerSize =
        static_cast<unsigned int>(strlen("rows ")) + countDigits(this->getLogicalRows()) * 3 + 2;
    if (footerSize < this->panes * this->paneLen) {
      this->showPageNum = true;
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
  if (this->showPageNum) {
    char footer[64];
    snprintf(footer, std::size(footer), "\x1b[7mrows %d-%d/%d\x1b[0m\n", startIndex + 1,
             startIndex + actualRows, this->getLogicalRows());
    renderer.renderWithANSI(footer);
  }
}

EditActionStatus waitPagerAction(ArrayPager &pager, const KeyBindings &bindings,
                                 KeyCodeReader &reader) {
  // read key code and update pager state
  if (reader.fetch() <= 0) {
    return EditActionStatus::ERROR;
  }
  if (!reader.hasControlChar()) {
    return EditActionStatus::OK;
  }
  const auto *action = bindings.findPagerAction(reader.get());
  if (!action) {
    return EditActionStatus::OK;
  }
  reader.clear();
  switch (*action) {
  case PagerAction::SELECT:
    return EditActionStatus::OK;
  case PagerAction::CANCEL:
    return EditActionStatus::CANCEL;
  case PagerAction::PREV:
    pager.moveCursorToForward();
    break;
  case PagerAction::NEXT:
    pager.moveCursorToNext();
    break;
  case PagerAction::LEFT:
    pager.moveCursorToLeft();
    break;
  case PagerAction::RIGHT:
    pager.moveCursorToRight();
    break;
  }
  return EditActionStatus::CONTINUE;
}

} // namespace arsh