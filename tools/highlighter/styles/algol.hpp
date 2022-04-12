/*
 * Copyright (C) 2022 Nagisa Sekiguchi
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

#ifndef YDSH_TOOLS_HIGHLIGHTER_STYLES_ALGOL_HPP
#define YDSH_TOOLS_HIGHLIGHTER_STYLES_ALGOL_HPP

#include "../style.h"

namespace ydsh::highlighter {

DEFINE_HIGHLIGHT_STYLE(algol) {
  constexpr auto COMMENT = styleRule("italic #888");
  constexpr auto KEYWORD = styleRule("underline bold");
  constexpr auto STRING = styleRule(" italic #666");
  constexpr auto VAR = styleRule("bold italic #666");

  return {
      {HighlightTokenClass::COMMENT, COMMENT}, {HighlightTokenClass::KEYWORD, KEYWORD},
      {HighlightTokenClass::STRING, STRING},   {HighlightTokenClass::VARIABLE, VAR},
      {HighlightTokenClass::TYPE, VAR},
  };
}

} // namespace ydsh::highlighter

#endif // YDSH_TOOLS_HIGHLIGHTER_STYLES_ALGOL_HPP
