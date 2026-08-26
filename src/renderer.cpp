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

#include "renderer.h"
#include "pager.h"

namespace arsh {

static bool renderLines(const LineBuffer &buf, ObserverPtr<const ArrayPager> pager,
                        LineRenderer &renderer,
                        const std::function<bool(StringRef)> &errorCmdChecker) {
  StringRef lineRef = buf.get();
  if (pager) {
    auto [pos, len] = buf.findCurLineInterval(true);
    lineRef = lineRef.substr(0, pos + len);
  }
  bool continueLine = false;
  if (renderer.getEscapeSeqMap()) {
    continueLine = !renderer.renderScript(lineRef, errorCmdChecker);
  } else {
    renderer.renderLines(lineRef);
  }
  if (pager) {
    renderer.setInitCols(0);
    renderer.renderLines("\n"); // force newline
    pager->render(renderer);
  }
  return continueLine;
}

#define OSC133_(O) "\x1b]133;" O "\x1b\\"

RenderingResult doRendering(const RenderingContext &ctx, ObserverPtr<const ArrayPager> pager,
                            ObserverPtr<const ANSIEscapeSeqMap> escapeSeqMap,
                            unsigned int maxCols) {
  unsigned int promptRows;
  unsigned int promptCols;
  RenderingResult result;
  {
    // render prompt and compute prompt row/column length
    LineRenderer renderer(ctx.ps, 0, result.renderedLines, escapeSeqMap);
    renderer.setColLimit(maxCols);
    renderer.renderWithANSI(ctx.prompt);
    promptRows = renderer.getTotalRows();
    promptCols = renderer.getTotalCols();

    // render lines and compute lines row/columns length
    if (ctx.semanticPrompt) {
      result.renderedLines.back() += OSC133_("B");
    }
    renderer.setInitCols(promptCols);
    renderer.setTokenizeResult(makeObserver(ctx.tokenizeCache));
    result.continueLine = renderLines(ctx.buf, pager, renderer, ctx.errorCmdChecker);
    result.renderedCols = renderer.getMaxTotalCols();
    result.promptRows = promptRows + 1;
  }

  // get cursor row/column length
  {
    LineRenderer renderer(ctx.ps, promptCols);
    renderer.setColLimit(maxCols);
    renderer.renderLines(ctx.buf.getToCursor());
    if (pager && pager->isFilterMode()) {
      renderer.setInitCols(0);
      renderer.renderLines("\n");
      ArrayPager::renderSearchBox(renderer, pager->getQuery());
    }
    result.cursorCols = renderer.getTotalCols();
    result.cursorRows = promptRows + 1 + renderer.getTotalRows();
    result.originalCursorRows = result.cursorRows;
  }
  return result;
}

void RenderingResult::fitToWinSize(const RenderingResult &prev, const bool showPager,
                                   const unsigned int winRows) {
  if (this->renderedRows() <= winRows) {
    this->scrolling = false;
    return;
  }

  // update scrollRows
  unsigned int scrollRows = prev.cursorRows;
  if (prev.scrolling) {
    if (prev.originalCursorRows <= this->cursorRows) { // cursor down
      scrollRows += this->cursorRows - prev.originalCursorRows;
    } else { // cursor up
      if (const auto diff = prev.originalCursorRows - this->cursorRows; diff < scrollRows) {
        scrollRows -= diff;
      } else {
        scrollRows = 1;
      }
    }
    scrollRows = std::min(scrollRows, winRows);
  } else if (const auto diff = this->renderedRows() - this->cursorRows; diff < winRows) {
    scrollRows = winRows - diff;
  } else if (this->cursorRows < winRows) {
    scrollRows = this->cursorRows;
  } else {
    scrollRows = winRows;
  }

  /**
   * |-- (org) rendered rows -----------------------|
   * |-- (org) cursor rows ---------|
   *        |-- window rows ---------------|
   *        |-- scroll rows --------|
   */

  // remove upper rows of window
  size_t eraseRows = this->cursorRows > scrollRows ? this->cursorRows - scrollRows : 0;
  if (showPager) {
    eraseRows = this->renderedRows() - winRows;
    scrollRows = this->cursorRows - eraseRows;
  } else if (this->renderedRows() - eraseRows < winRows) {
    auto delta = winRows - (this->renderedRows() - eraseRows);
    eraseRows -= delta;
    scrollRows += delta;
  }
  this->renderedLines.erase(this->renderedLines.begin(),
                            this->renderedLines.begin() + static_cast<ssize_t>(eraseRows));

  // remove lower rows of window
  if (this->renderedRows() > winRows) {
    this->renderedLines.resize(winRows);
    if (auto &last = this->renderedLines.back(); StringRef(last).endsWith("\r\n")) {
      last.resize(last.size() - 2);
    }
  }
  this->cursorRows = scrollRows;
  if (eraseRows >= this->promptRows) {
    this->promptRows = 1;
  } else {
    this->promptRows -= eraseRows;
  }
  this->scrolling = true;
}

} // namespace arsh