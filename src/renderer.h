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

#ifndef ARSH_RENDERER_H
#define ARSH_RENDERER_H

#include "line_buffer.h"
#include "line_renderer.h"

namespace arsh {

class ArrayPager;

struct RenderingResult {
  std::vector<std::string> renderedLines;
  unsigned int renderedCols{0};
  unsigned int cursorRows{0};
  unsigned int cursorCols{0};
  unsigned int promptRows{0};
  bool continueLine{false};

  void appendTo(std::string &out) const {
    for (auto &e : this->renderedLines) {
      out += e;
    }
  }

  std::string join() const {
    std::string ret;
    this->appendTo(ret);
    return ret;
  }

  unsigned int renderedRows() const { return this->renderedLines.size(); }
};

struct RenderingContext {
  LineBuffer buf;
  const StringRef prompt;
  unsigned int oldCursorRows{0}; // previous refresh cursor rows (relative to initial rows)
  unsigned int oldActualCursorRows{0};
  unsigned int oldRenderedCols{0};
  CharWidthProperties ps;
  bool scrolling{false};
  bool semanticPrompt{false};
  std::function<bool(StringRef)> errorCmdChecker;
  mutable TokenizerResult tokenizeCache; // previously tokenized result

  RenderingContext(char *data, size_t len, StringRef prompt,
                   std::function<bool(StringRef)> &&errorCmdChecker)
      : buf(data, len), prompt(prompt), errorCmdChecker(std::move(errorCmdChecker)) {
    this->ps.replaceInvalid = true;
  }
};

RenderingResult doRendering(const RenderingContext &ctx, ObserverPtr<const ArrayPager> pager,
                            ObserverPtr<const ANSIEscapeSeqMap> escapeSeqMap, unsigned int maxCols);

/**
 *
 * @param ctx
 * @param showPager
 * @param winRows
 * @param result
 * @return
 * if resize rendered lines, return true
 */
bool fitToWinSize(const RenderingContext &ctx, bool showPager, unsigned int winRows,
                  RenderingResult &result);

} // namespace arsh

#endif // ARSH_RENDERER_H
