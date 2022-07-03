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

#ifndef YDSH_TOOLS_HIGHLIGHTER_STYLES_MONOKAI_HPP
#define YDSH_TOOLS_HIGHLIGHTER_STYLES_MONOKAI_HPP

#include "../style.h"

namespace ydsh::highlighter {

DEFINE_HIGHLIGHT_STYLE(monokai) {
  constexpr auto COMMENT = ValidRule("#88846f");
  constexpr auto KEYWORD = ValidRule("#F92672");
  constexpr auto NUMBER = ValidRule("#AE81FF");
  constexpr auto STRING = ValidRule("#E6DB74");
  constexpr auto VAR = ValidRule("#F8F8F2");
  constexpr auto FUNC = ValidRule("#A6E22E");
  constexpr auto PARAM = ValidRule("italic #FD971F");
  constexpr auto TYPE = ValidRule("#A6E22E underline");
  constexpr auto TEXT = ValidRule("#F8F8F2");
  constexpr auto BG = ValidRule("bg:#272822");

  return {
      {HighlightTokenClass::COMMENT, COMMENT},  {HighlightTokenClass::KEYWORD, KEYWORD},
      {HighlightTokenClass::OPERATOR, KEYWORD}, {HighlightTokenClass::NUMBER, NUMBER},
      {HighlightTokenClass::REGEX, STRING},     {HighlightTokenClass::STRING, STRING},
      {HighlightTokenClass::COMMAND, FUNC},     {HighlightTokenClass::COMMAND_ARG, PARAM},
      {HighlightTokenClass::REDIRECT, PARAM},   {HighlightTokenClass::VARIABLE, VAR},
      {HighlightTokenClass::TYPE, TYPE},        {HighlightTokenClass::MEMBER, PARAM}, // FIXME:
      {HighlightTokenClass::FOREGROUND_, TEXT}, {HighlightTokenClass::BACKGROUND_, BG},
  };
}

} // namespace ydsh::highlighter

#endif // YDSH_TOOLS_HIGHLIGHTER_STYLES_MONOKAI_HPP
