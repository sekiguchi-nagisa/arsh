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

ArrayPager ArrayPager::create(CandidatesWrapper &&obj, const CharWidthProperties &ps,
                              WindowSize winSize) {
  unsigned int maxLen = 0;
  unsigned int maxIndex = 0;
  FlexBuffer<ItemEntry> items;
  const unsigned int size = obj.size();
  items.reserve(size);
  for (unsigned int i = 0; i < size; i++) {
    ItemEntry item{};

    // compute candidate columns
    {
      LineRenderer renderer(ps, 0);
      renderer.setLineNumLimit(0); // ignore newline
      renderer.renderLines(obj.getCandidateAt(i));
      item.len = renderer.getTotalCols();
    }

    // compute signature columns
    if (const StringRef desc = obj.getDescriptionAt(i); !desc.empty()) {
      if (obj.getAttrAt(i).kind == CandidateAttr::Kind::TYPE_SIGNATURE) {
        LineRenderer renderer(ps, item.len);
        renderer.setLineNumLimit(0); // ignore newline
        renderer.renderLines(" ");
        renderer.renderLines(desc);
        item.len = renderer.getTotalCols();
      } else {
        LineRenderer renderer(ps, 0);
        renderer.setLineNumLimit(0); // ignore newline
        renderer.renderLines("(");
        renderer.renderLines(desc);
        renderer.renderLines(")");

        const size_t sigCols = renderer.getTotalCols();
        item.leftPad = TAB_WIDTH - (sigCols % TAB_WIDTH);
        item.rightPad = TAB_WIDTH - (item.len % TAB_WIDTH);
        item.len += sigCols + item.leftPad + item.rightPad;
      }
    }

    items.push_back(item);
    if (item.len > maxLen) {
      maxLen = item.len;
      maxIndex = items.size() - 1;
    }
  }

  // compute extra tabs
  const auto paneSize = items[maxIndex].itemLen();
  assert(paneSize % TAB_WIDTH == 0);
  for (auto &e : items) {
    auto padLen = paneSize - e.itemLen();
    assert(padLen % TAB_WIDTH == 0);
    e.tabs = padLen / TAB_WIDTH;
  }
  return {ps, std::move(obj), std::move(items), maxIndex, winSize};
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
    unsigned int colLimit =
        (this->winSize.cols / TAB_WIDTH) * TAB_WIDTH; // truncate to multiple of TAB_WIDTH
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

static void renderItem(LineRenderer &renderer, const StringRef can, const CandidateAttr attr,
                       const StringRef desc, const ArrayPager::ItemEntry &e, const bool selected) {
  if (selected) {
    renderer.renderWithANSI("\x1b[7m");
  }
  renderer.renderLines(can);
  if (attr.kind == CandidateAttr::Kind::TYPE_SIGNATURE) {
    if (!desc.empty()) {
      renderer.renderWithANSI("\x1b[90m ");
      renderer.renderLines(desc);
    }
    if (e.tabs) {
      std::string tab;
      tab.resize(e.tabs, '\t');
      renderer.renderLines(tab);
    }
    renderer.renderLines("\t");
    if (!selected) {
      renderer.renderWithANSI("\x1b[0m");
    }
  } else {
    if (e.rightPad) {
      std::string pad;
      pad.resize(e.rightPad, ' ');
      renderer.renderLines(pad);
    }
    if (e.tabs) {
      std::string tab;
      tab.resize(e.tabs, '\t');
      renderer.renderLines(tab);
    }
    if (e.leftPad) {
      std::string pad;
      pad.resize(e.leftPad, ' ');
      renderer.renderLines(pad);
    }
    if (!desc.empty()) { // FIXME: highlight
      renderer.renderLines("(");
      renderer.renderLines(desc);
      renderer.renderLines(")");
    }
    renderer.renderLines("\t");
  }
  if (selected) {
    renderer.renderWithANSI("\x1b[0m");
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
      const bool selected = actualIndex == this->index && this->showCursor;
      renderItem(renderer, this->obj.getCandidateAt(actualIndex), this->obj.getAttrAt(actualIndex),
                 this->obj.getDescriptionAt(actualIndex), this->items[actualIndex], selected);
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

// #########################
// ##     HistRotator     ##
// #########################

HistRotator::HistRotator(ObjPtr<ArrayObject> history) : history(std::move(history)) {
  if (this->history) {
    this->truncateUntilLimit(true);
    this->history->append(Value::createStr()); // not check iterator invalidation
    this->history->lock(ArrayObject::LockType::HISTORY);
  }
}

void HistRotator::revertAll() {
  if (this->history) {
    if (this->history->size() > 0) {
      this->history->refValues().pop_back(); // not check iterator invalidation
    }
    this->truncateUntilLimit();
    for (auto &e : this->oldEntries) { // revert modified entry
      if (e.first < this->history->size()) {
        this->history->refValues()[e.first] =
            std::move(e.second); // not check iterator invalidation
      }
    }
    this->history->unlock();
    this->history = nullptr;
  }
}

bool HistRotator::rotate(StringRef &curBuf, HistRotator::Op op) {
  this->truncateUntilLimit();

  const auto histSize = static_cast<ssize_t>(this->history->size());
  const int newHistIndex = this->histIndex + (op == Op::PREV ? 1 : -1);
  if (newHistIndex < 0) {
    this->histIndex = 0;
    return false;
  } else if (newHistIndex >= histSize) {
    this->histIndex = static_cast<int>(histSize) - 1;
    return false;
  } else {
    ssize_t bufIndex = histSize - 1 - this->histIndex;
    if (!this->save(bufIndex, curBuf)) { // save current buffer content to current history entry
      this->histIndex = 0;
      return false;
    }
    this->histIndex = newHistIndex;
    bufIndex = histSize - 1 - this->histIndex;
    curBuf = this->history->getValues()[bufIndex].asStrRef();
    return true;
  }
}

void HistRotator::truncateUntilLimit(bool beforeAppend) {
  const unsigned int offset = beforeAppend ? 1 : 0;
  if (this->history->size() + offset > this->getMaxSize()) {
    unsigned int delSize = this->history->size() + offset - this->getMaxSize();
    auto &values = this->history->refValues(); // not check iterator invalidation
    values.erase(values.begin(), values.begin() + delSize);
    assert(values.size() == this->getMaxSize() - offset);
  }
}

bool HistRotator::save(ssize_t index, StringRef curBuf) {
  if (index < static_cast<ssize_t>(this->history->size()) && index > -1) {
    auto actualIndex = static_cast<unsigned int>(index);
    auto org = this->history->getValues()[actualIndex];
    this->oldEntries.emplace(actualIndex, std::move(org));
    this->history->refValues()[actualIndex] =
        Value::createStr(curBuf); // not check iterator invalidation
    return true;
  }
  return false;
}

} // namespace arsh