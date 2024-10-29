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

#ifndef ARSH_TOOLS_HIGHLIGHTER_STYLES_ALGOL_HPP
#define ARSH_TOOLS_HIGHLIGHTER_STYLES_ALGOL_HPP

#include "../style.h"

namespace arsh::highlighter {

DEFINE_HIGHLIGHT_STYLE(algol) {
  constexpr auto COMMENT = ValidRule("italic #888");
  constexpr auto KEYWORD = ValidRule("underline bold");
  constexpr auto STRING = ValidRule(" italic #666");
  constexpr auto VAR = ValidRule("bold italic #666");
  constexpr auto BG = ValidRule("bg:#ffffff");
  constexpr auto ERR = ValidRule("border:#ff0000");

  return {
      {HighlightTokenClass::COMMENT, COMMENT}, {HighlightTokenClass::KEYWORD, KEYWORD},
      {HighlightTokenClass::STRING, STRING},   {HighlightTokenClass::VARIABLE, VAR},
      {HighlightTokenClass::TYPE, VAR},        {HighlightTokenClass::BACKGROUND_, BG},
      {HighlightTokenClass::ERROR_, ERR},
  };
}

} // namespace arsh::highlighter

#endif // ARSH_TOOLS_HIGHLIGHTER_STYLES_ALGOL_HPP
