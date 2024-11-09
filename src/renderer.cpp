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
    if (!lineRef.endsWith("\n")) {
      renderer.renderLines("\n"); // force newline
    }
    pager->render(renderer);
  }
  return continueLine;
}

#define OSC133_(O) "\x1b]133;" O "\x1b\\"

static int emitPromptStart(char (&data)[64], int prevExitStatus) {
  return snprintf(data, std::size(data), "\x1b]133;D;%d\x1b\\" OSC133_("A"), prevExitStatus);
}

RenderingResult doRendering(const RenderingContext &ctx, ObserverPtr<const ArrayPager> pager,
                            ObserverPtr<const ANSIEscapeSeqMap> escapeSeqMap,
                            unsigned int maxCols) {
  size_t promptRows;
  size_t promptCols;
  RenderingResult result;
  {
    // render prompt and compute prompt row/column length
    if (ctx.semanticPrompt) {
      char data[64];
      emitPromptStart(data, ctx.prevExitStatus);
      result.renderedLines += data;
    }
    LineRenderer renderer(ctx.ps, 0, result.renderedLines, escapeSeqMap);
    renderer.setMaxCols(maxCols);
    renderer.renderWithANSI(ctx.prompt);
    promptRows = renderer.getTotalRows();
    promptCols = renderer.getTotalCols();

    // render lines and compute lines row/columns length
    if (ctx.semanticPrompt) {
      result.renderedLines += OSC133_("B");
    }
    renderer.setInitCols(promptCols);
    result.continueLine = renderLines(ctx.buf, pager, renderer, ctx.errorCmdChecker);
    result.renderedRows = renderer.getTotalRows() + 1;
    result.promptRows = static_cast<unsigned int>(promptRows + 1);
    if (ctx.semanticPrompt) {
      result.renderedLines += OSC133_("C");
    }
  }

  // get cursor row/column length
  {
    LineRenderer renderer(ctx.ps, promptCols);
    renderer.setMaxCols(maxCols);
    renderer.renderLines(ctx.buf.getToCursor());
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

bool fitToWinSize(const RenderingContext &ctx, const bool showPager, const unsigned int winRows,
                  RenderingResult &result) {
  constexpr StringRef NL = "\r\n";

  if (result.renderedRows <= winRows) {
    return false;
  }

  // update scrollRows
  unsigned int scrollRows = ctx.oldCursorRows;
  if (ctx.scrolling) {
    if (ctx.oldActualCursorRows <= result.cursorRows) { // cursor down
      scrollRows += result.cursorRows - ctx.oldActualCursorRows;
    } else { // cursor up
      if (const auto diff = ctx.oldActualCursorRows - result.cursorRows; diff < scrollRows) {
        scrollRows -= diff;
      } else {
        scrollRows = 1;
      }
    }
    scrollRows = std::min(scrollRows, winRows);
  } else if (const auto diff = result.renderedRows - result.cursorRows; diff < winRows) {
    scrollRows = winRows - diff;
  } else if (result.cursorRows < winRows) {
    scrollRows = result.cursorRows;
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
  size_t eraseRows = result.cursorRows > scrollRows ? result.cursorRows - scrollRows : 0;
  if (showPager) {
    eraseRows = result.renderedRows - winRows;
    scrollRows = result.cursorRows - eraseRows;
  } else if (result.renderedRows - eraseRows < winRows) {
    auto delta = winRows - (result.renderedRows - eraseRows);
    eraseRows -= delta;
    scrollRows += delta;
  }
  result.renderedRows -= eraseRows;
  if (auto r = findNthPos(result.renderedLines, eraseRows, NL); r != StringRef::npos) {
    result.renderedLines.erase(0, r + NL.size());
    if (ctx.semanticPrompt) {
      if (eraseRows >= result.promptRows) {
        result.renderedLines.insert(0, OSC133_("B"));
      }
      char data[64];
      emitPromptStart(data, ctx.prevExitStatus);
      result.renderedLines.insert(0, data);
    }
  }

  // remove lower rows of window
  if (result.renderedRows > winRows) {
    if (auto r = findNthPos(result.renderedLines, winRows, NL); r != StringRef::npos) {
      result.renderedLines.erase(result.renderedLines.begin() + r, result.renderedLines.end());
      if (ctx.semanticPrompt) {
        result.renderedLines += OSC133_("C");
      }
    }
    result.renderedRows = winRows;
  }
  result.cursorRows = scrollRows;
  if (eraseRows >= result.promptRows) {
    result.promptRows = 1;
  } else {
    result.promptRows -= eraseRows;
  }
  return true;
}

} // namespace arsh