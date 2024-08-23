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

} // namespace arsh