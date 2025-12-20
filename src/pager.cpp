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
#include "line_renderer.h"
#include "misc/format.hpp"
#include "vm.h"

namespace arsh {

// ########################
// ##     ArrayPager     ##
// ########################

ArrayPager ArrayPager::create(const CandidatesObject &obj, const CharWidthProperties &ps,
                              WindowSize winSize, unsigned int rowRatio) {
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
      renderer.setEmitNewline(false); // ignore newlines
      renderer.renderLines(obj.getCandidateAt(i));
      item.len = renderer.getTotalCols();
    }

    // compute signature columns
    if (const StringRef desc = obj.getDescriptionAt(i); !desc.empty()) {
      if (obj.getAttrAt(i).kind == CandidateAttr::Kind::TYPE_SIGNATURE) {
        LineRenderer renderer(ps, item.len);
        renderer.setEmitNewline(false); // ignore newlines
        renderer.renderLines(" ");
        renderer.renderLines(desc);
        item.len = renderer.getTotalCols();
      } else {
        LineRenderer renderer(ps, 0);
        renderer.setEmitNewline(false); // ignore newlines
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
  return {obj, std::move(items), maxIndex, winSize, rowRatio};
}

void ArrayPager::updateLayout() {
  this->showPager = true;
  this->showDesc = true;
  this->rows = (this->winSize.rows * this->rowRatio) / 100;
  this->rows = std::min(this->rows, saturatedSub(this->winSize.rows, ROW_MARGIN));
  if (this->rows == 0) {
    this->rows = 1;
    this->showPager = false;
  }
  this->paneLen = this->items[this->maxLenItemIndex].itemLen();
  this->panes = saturatedSub(this->winSize.cols, COL_MARGIN) / this->paneLen;
  this->panes = std::max(this->panes, 1u);
  if (this->panes == 1) {
    // truncate to multiple of TAB_WIDTH
    unsigned int colLimit = saturatedSub(this->winSize.cols, COL_MARGIN) / TAB_WIDTH * TAB_WIDTH;
    if (this->paneLen > colLimit) {
      this->paneLen = colLimit; // larger than window size
      this->showDesc = false;
    }
  }
  this->showRowNum = false;
  if (this->getActualRows() + 1 <= this->winSize.rows &&
      this->getLogicalRows() > this->getActualRows()) {
    // rows 23-111/111
    unsigned int footerSize =
        static_cast<unsigned int>(strlen("rows ")) + countDigits(this->getLogicalRows()) * 3 + 2;
    if (footerSize < this->panes * this->paneLen) {
      this->showRowNum = true;
    }
  }

  // re-compute cursor position
  if (this->filteredItemSize()) {
    const unsigned int orgIndex = this->index;
    this->index = 0;
    this->curRow = 0;
    while (this->index < orgIndex) {
      this->moveCursorToNext();
    }
  }
}

bool ArrayPager::matchItemAt(unsigned int itemIndex) const {
  StringRef candidate = this->obj.getCandidateAt(itemIndex);
  return fuzzyFind(candidate, this->query);
}

void ArrayPager::rebuildFilteredItemIndexes() {
  this->filteredItemIndexes.clear();
  for (unsigned int i = 0; i < this->items.size(); i++) {
    if (this->matchItemAt(i)) {
      this->filteredItemIndexes.push_back(i);
    }
  }
  if (const auto size = this->filteredItemSize(); size > 0 && this->index >= size) {
    this->index = 0;
    this->curRow = 0;
  }
  this->updateLayout();
}

void ArrayPager::pushQueryChar(const StringRef grapheme) {
  if (!this->isFilterMode() || grapheme.empty()) {
    return;
  }
  this->query += grapheme;
  auto iter = std::remove_if(this->filteredItemIndexes.begin(), this->filteredItemIndexes.end(),
                             [&](unsigned int itemIndex) { return !this->matchItemAt(itemIndex); });
  this->filteredItemIndexes.erase(iter, this->filteredItemIndexes.end());
  if (const auto size = this->filteredItemSize(); size > 0 && this->index >= size) {
    this->index = 0;
    this->curRow = 0;
  }
  this->updateLayout();
}

void ArrayPager::popQueryChar() {
  if (!this->isFilterMode() || this->query.empty()) {
    return;
  }
  size_t byteSize = 0;
  iterateGrapheme(this->query, [&byteSize](const GraphemeCluster &grapheme) {
    byteSize = grapheme.getRef().size();
  });
  this->query.resize(this->query.size() - byteSize);
  this->rebuildFilteredItemIndexes();
}

void ArrayPager::disableFilterMode() {
  const unsigned int newIndex = this->toActualItemIndex(this->index);
  this->filterMode = false;
  this->filteredItemIndexes.clear();
  this->query.clear();
  this->curRow = 0;
  this->index = 0;
  for (unsigned int i = 0; i < newIndex; i++) {
    this->moveCursorToNext();
  }
  this->updateLayout();
}

static void renderItem(LineRenderer &renderer, const bool showDesc, const StringRef can,
                       const CandidateAttr attr, const StringRef desc,
                       const ArrayPager::ItemEntry &e, const bool selected) {
  if (selected) {
    renderer.renderWithANSI("\x1b[7m");
  }
  renderer.renderLines(can);
  if (attr.kind == CandidateAttr::Kind::TYPE_SIGNATURE && showDesc) {
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
  } else { // with or without description
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
      if (showDesc) {
        renderer.renderLines("(");
        renderer.renderLines(desc);
        renderer.renderLines(")");
      } else {
        std::string tab;
        tab.resize(e.itemLen(), '\t'); // force fill pane
        renderer.renderLines(tab);
      }
    }
    renderer.renderLines("\t");
  }
  if (selected) {
    renderer.renderWithANSI("\x1b[0m");
  }
}

void ArrayPager::render(LineRenderer &renderer) const {
  if (!this->showPager) {
    return;
  }

  if (this->isFilterMode()) {
    renderSearchBox(renderer, this->query);
    renderer.renderLines("\n");
    if (!this->filteredItemSize()) {
      renderer.renderWithANSI("\x1b[7m(no matches)\x1b[0m\n");
      return;
    }
    renderer.setLineBreakOp(LineRenderer::LineBreakOp::SOFT_WRAP);
  }

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
  if (startIndex >= this->curRow) {
    startIndex -= this->curRow;
  }
  const unsigned int actualRows = this->getActualRows();

  renderer.setInitCols(0);
  if (this->getPanes() == 1) {
    renderer.setColLimit(this->getPaneLen());
    renderer.setLineBreakOp(LineRenderer::LineBreakOp::TRUNCATE);
  }
  for (unsigned int i = 0; i < actualRows; i++) {
    renderer.setEmitNewline(false);                  // ignore newlines
    for (unsigned int j = 0; j < this->panes; j++) { // render row
      const unsigned int actualIndex = startIndex + i + j * maxRowSize;
      if (actualIndex >= this->filteredItemSize()) {
        break;
      }
      const bool selected = actualIndex == this->index && this->showCursor;
      const unsigned int itemIndex = this->toActualItemIndex(actualIndex);
      renderItem(renderer, this->showDesc, this->obj.getCandidateAt(itemIndex),
                 this->obj.getAttrAt(itemIndex), this->obj.getDescriptionAt(itemIndex),
                 this->items[itemIndex], selected);
    }
    renderer.setEmitNewline(true); // re-enable newline characters
    renderer.renderLines("\n");
  }
  if (this->showRowNum) {
    char footer[64];
    snprintf(footer, std::size(footer), "\x1b[7mrows %d-%d/%d\x1b[0m\n", startIndex + 1,
             startIndex + actualRows, this->getLogicalRows());
    renderer.renderWithANSI(footer);
  }
}

void ArrayPager::renderSearchBox(LineRenderer &renderer, StringRef query) {
  renderer.setLineBreakOp(LineRenderer::LineBreakOp::TRUNCATE);
  renderer.setInitCols(0);
  renderer.setEmitNewline(false);
  renderer.renderWithANSI("\x1b[4m");
  renderer.renderLines(SEARCH_FILTER_PREFIX);
  renderer.renderLines(query);
  renderer.setEmitNewline(true);
  renderer.renderWithANSI("\x1b[0m");
}

EditActionStatus waitPagerAction(ArrayPager &pager, const KeyBindings &bindings,
                                 KeyCodeReader &reader, const AtomicSigSet &watchSigSet) {
// read key code and update the pager state
FETCH:
  if (ssize_t r = reader.fetch(watchSigSet); r <= 0) {
    if (r == -1 && errno == EINTR) {
      return EditActionStatus::CANCEL;
    }
    return EditActionStatus::ERROR;
  }
  if (!reader.hasControlChar()) {
    if (pager.isFilterMode()) {
      pager.pushQueryChar(reader.get());
      return EditActionStatus::CONTINUE;
    }
    return EditActionStatus::OK;
  }
  if (!reader.getEvent().hasValue()) {
    goto FETCH; // ignore unrecognized escape sequences
  }
  if (pager.isFilterMode()) {
    if (const auto codePoint = reader.getEscapedPlainCodePoint(); codePoint > -1) {
      char buf[4];
      unsigned int r = UnicodeUtil::codePointToUtf8(codePoint, buf);
      pager.pushQueryChar({buf, r});
      return EditActionStatus::CONTINUE;
    }
    if (reader.hasBracketedPasteStart()) {
      std::string out;
      const auto oldTimeout = reader.getTimeout();
      reader.setTimeout(oldTimeout * 2);
      const bool r = reader.intoBracketedPasteMode([&out](const StringRef ref) {
        out += ref; // TODO: check limit
        return true;
      });
      reader.setTimeout(oldTimeout);
      if (!r) {
        return EditActionStatus::ERROR;
      }
      pager.pushQueryChar(out);
      return EditActionStatus::CONTINUE;
    }
  }
  const auto *action = bindings.findAction(reader.getEvent());
  if (!action) {
    if (pager.isFilterMode()) {
      goto FETCH; // ignore unbound escape sequences in search filer mode
    }
    return EditActionStatus::OK;
  }
  switch (action->type) {
  case EditActionType::ACCEPT:
    reader.clear();
    return EditActionStatus::OK;
  case EditActionType::CANCEL:
    return EditActionStatus::CANCEL;
  case EditActionType::REVERT:
    reader.clear();
    return EditActionStatus::REVERT;
  case EditActionType::TOGGLE_SEARCH:
    if (pager.isFilterMode()) {
      pager.disableFilterMode();
    } else {
      pager.tryToEnableFilterMode();
    }
    break;
  case EditActionType::BACKWARD_CHAR:
    pager.moveCursorToLeft();
    break;
  case EditActionType::FORWARD_CHAR:
    pager.moveCursorToRight();
    break;
  case EditActionType::UP_OR_HISTORY:
  case EditActionType::COMPLETE_BACKWARD:
    pager.moveCursorToForward();
    break;
  case EditActionType::DOWN_OR_HISTORY:
  case EditActionType::COMPLETE:
    pager.moveCursorToNext();
    break;
  case EditActionType::BACKWARD_DELETE_CHAR:
    if (pager.isFilterMode()) {
      pager.popQueryChar();
      return EditActionStatus::CONTINUE;
    }
    return EditActionStatus::OK;
  default:
    if (pager.isFilterMode()) {
      goto FETCH; // ignore unbound actions in search filer mode
    }
    return EditActionStatus::OK;
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
      this->history->pop_back(); // not check iterator invalidation
    }
    this->truncateUntilLimit();
    for (auto &e : this->oldEntries) { // revert modified entry
      if (e.first < this->history->size()) {
        (*this->history)[e.first] = std::move(e.second); // not check iterator invalidation
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
    curBuf = (*this->history)[bufIndex].asStrRef();
    return true;
  }
}

void HistRotator::truncateUntilLimit(bool beforeAppend) {
  const unsigned int offset = beforeAppend ? 1 : 0;
  if (this->history->size() + offset > this->getMaxSize()) {
    unsigned int delSize = this->history->size() + offset - this->getMaxSize();
    this->history->erase(this->history->begin(),
                         this->history->begin() + delSize); // not check iterator invalidation
    assert(this->history->size() == this->getMaxSize() - offset);
  }
}

bool HistRotator::save(ssize_t index, StringRef curBuf) {
  if (index < static_cast<ssize_t>(this->history->size()) && index > -1) {
    auto actualIndex = static_cast<unsigned int>(index);
    auto org = (*this->history)[actualIndex];
    this->oldEntries.emplace(actualIndex, std::move(org));
    (*this->history)[actualIndex] = Value::createStr(curBuf); // not check iterator invalidation
    return true;
  }
  return false;
}

} // namespace arsh