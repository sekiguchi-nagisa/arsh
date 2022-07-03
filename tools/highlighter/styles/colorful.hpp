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

#ifndef YDSH_TOOLS_HIGHLIGHTER_STYLES_COLORFUL_HPP
#define YDSH_TOOLS_HIGHLIGHTER_STYLES_COLORFUL_HPP

#include "../style.h"

namespace ydsh::highlighter {

DEFINE_HIGHLIGHT_STYLE(colorful) {
  constexpr auto TEXT = ValidRule("#bbbbbb");
  constexpr auto COMMENT = ValidRule("#888");
  constexpr auto KEYWORD = ValidRule("bold #080");
  constexpr auto OPERATOR = ValidRule("#333");
  constexpr auto FUNC = ValidRule("bold #06B");
  constexpr auto VAR = ValidRule("#963");
  constexpr auto STRING = ValidRule("bg:#fff0f0");
  constexpr auto REGEX = ValidRule("bg:#fff0ff #000");
  constexpr auto NUMBER = ValidRule("bold #60E");
  constexpr auto TYPE = ValidRule("bold #B06");
  constexpr auto BG = ValidRule("bg:#ffffff");

  return {
      {HighlightTokenClass::COMMENT, COMMENT},   {HighlightTokenClass::KEYWORD, KEYWORD},
      {HighlightTokenClass::OPERATOR, OPERATOR}, {HighlightTokenClass::NUMBER, NUMBER},
      {HighlightTokenClass::REGEX, REGEX},       {HighlightTokenClass::STRING, STRING},
      {HighlightTokenClass::COMMAND, FUNC},      {HighlightTokenClass::REDIRECT, OPERATOR},
      {HighlightTokenClass::VARIABLE, VAR},      {HighlightTokenClass::TYPE, TYPE},
      {HighlightTokenClass::FOREGROUND_, TEXT},  {HighlightTokenClass::BACKGROUND_, BG},
  };
}

} // namespace ydsh::highlighter

#endif // YDSH_TOOLS_HIGHLIGHTER_STYLES_COLORFUL_HPP
