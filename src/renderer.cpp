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
                        LineRenderer &renderer) {
  StringRef lineRef = buf.get();
  if (pager) {
    auto [pos, len] = buf.findCurLineInterval(true);
    lineRef = lineRef.substr(0, pos + len);
  }
  bool continueLine = false;
  if (renderer.getEscapeSeqMap()) {
    continueLine = !renderer.renderScript(lineRef);
  } else {
    renderer.renderLines(lineRef);
  }
  if (pager) {
    renderer.setInitCols(0);
    if (!lineRef.endsWith("\n")) {
      renderer.renderLines("\n"); // force newline
    }
    pager->render(renderer);
  }
  return continueLine;
}

RenderingResult doRendering(const CharWidthProperties &ps, StringRef prompt, const LineBuffer &buf,
                            ObserverPtr<const ArrayPager> pager,
                            ObserverPtr<const ANSIEscapeSeqMap> escapeSeqMap,
                            unsigned int maxCols) {
  size_t promptRows;
  size_t promptCols;
  RenderingResult result;
  {
    // render prompt and compute prompt row/column length
    LineRenderer renderer(ps, 0, result.renderedLines, escapeSeqMap);
    renderer.setMaxCols(maxCols);
    renderer.renderWithANSI(prompt);
    promptRows = renderer.getTotalRows();
    promptCols = renderer.getTotalCols();

    // render lines and compute lines row/columns length
    renderer.setInitCols(promptCols);
    result.continueLine = renderLines(buf, pager, renderer);
    result.renderedRows = renderer.getTotalRows() + 1;
  }

  // get cursor row/column length
  {
    LineRenderer renderer(ps, promptCols);
    renderer.setMaxCols(maxCols);
    renderer.renderLines(buf.getToCursor());
    result.cursorCols = renderer.getTotalCols();
    result.cursorRows = promptRows + 1 + renderer.getTotalRows();
  }
  return result;
}

static StringRef::size_type findNthPos(const StringRef ref, const unsigned int N,
                                       const StringRef delim) {
  StringRef::size_type pos = 0;
  StringRef::size_type retPos = StringRef::npos;
  for (unsigned int count = 0; count < N; count++) {
    auto r = ref.find(delim, pos);
    if (r == StringRef::npos) {
      break;
    }
    pos = r + delim.size();
    retPos = r;
  }
  return retPos;
}

bool fitToWinSize(const FitToWinSizeParams &params, RenderingResult &result) {
  constexpr StringRef NL = "\r\n";

  if (result.renderedRows <= params.winRows) {
    return false;
  }

  // update scrollRows
  unsigned int scrollRows = params.scrollRows;
  if (params.scrolling) {
    if (params.oldCursorRows <= result.cursorRows) { // cursor down
      scrollRows += result.cursorRows - params.oldCursorRows;
    } else { // cursor up
      if (const auto diff = params.oldCursorRows - result.cursorRows; diff < scrollRows) {
        scrollRows -= diff;
      } else {
        scrollRows = 1;
      }
    }
    scrollRows = std::min(scrollRows, params.winRows);
  } else if (const auto diff = result.renderedRows - result.cursorRows; diff < params.winRows) {
    scrollRows = params.winRows - diff;
  } else if (result.cursorRows < params.winRows) {
    scrollRows = result.cursorRows;
  } else {
    scrollRows = params.winRows;
  }

  /**
   * |-- (org) rendered rows -----------------------|
   * |-- (org) cursor rows ---------|
   *        |-- window rows ---------------|
   *        |-- scroll rows --------|
   */

  // remove upper rows of window
  size_t eraseRows = result.cursorRows > scrollRows ? result.cursorRows - scrollRows : 0;
  if (params.showPager) {
    eraseRows = result.renderedRows - params.winRows;
    scrollRows = result.cursorRows - eraseRows;
  } else if (result.renderedRows - eraseRows < params.winRows) {
    auto delta = params.winRows - (result.renderedRows - eraseRows);
    eraseRows -= delta;
    scrollRows += delta;
  }
  result.renderedRows -= eraseRows;
  if (auto r = findNthPos(result.renderedLines, eraseRows, NL); r != StringRef::npos) {
    result.renderedLines.erase(0, r + NL.size());
  }

  // remove lower rows of window
  if (result.renderedRows > params.winRows) {
    if (auto r = findNthPos(result.renderedLines, params.winRows, NL); r != StringRef::npos) {
      result.renderedLines.erase(result.renderedLines.begin() + r, result.renderedLines.end());
    }
    result.renderedRows = params.winRows;
  }
  result.cursorRows = scrollRows;
  return true;
}

} // namespace arsh