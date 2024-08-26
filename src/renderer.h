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
  std::string renderedLines;
  size_t renderedRows{0};
  size_t cursorRows{0};
  size_t cursorCols{0};
  bool continueLine{false};
};

RenderingResult doRendering(const CharWidthProperties &ps, StringRef prompt, const LineBuffer &buf,
                            ObserverPtr<const ArrayPager> pager,
                            ObserverPtr<const ANSIEscapeSeqMap> escapeSeqMap, unsigned int maxCols);

struct FitToWinSizeParams {
  size_t winRows;
  unsigned int oldCursorRows;
  bool showPager;
  bool scrolling;
  size_t scrollRows;
};

void fitToWinSize(const FitToWinSizeParams &params, RenderingResult &result);

} // namespace arsh

#endif // ARSH_RENDERER_H
