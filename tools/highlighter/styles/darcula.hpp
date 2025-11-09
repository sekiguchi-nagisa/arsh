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

#ifndef ARSH_TOOLS_HIGHLIGHTER_STYLES_DARCULA_HPP
#define ARSH_TOOLS_HIGHLIGHTER_STYLES_DARCULA_HPP

#include "../style.h"

namespace arsh::highlighter {
/**
 * intellij darcula theme
 */
DEFINE_HIGHLIGHT_STYLE(darcula) {
  constexpr auto COMMENT = ValidRule("#808080");
  constexpr auto KEYWORD = ValidRule("#CC7832 bold");
  constexpr auto NUMBER = ValidRule("#6897BB");
  constexpr auto REGEX = ValidRule("#646695");
  constexpr auto STRING = ValidRule("#6A8759");
  constexpr auto VAR = ValidRule("#9876AA");
  constexpr auto FUNC = ValidRule("#FFC66D");
  constexpr auto PARAM = ValidRule("#A9B7C6");
  constexpr auto TEXT = ValidRule("#D4D4D4");
  constexpr auto BG = ValidRule("bg:#2B2B2B");
  constexpr auto ERR = ValidRule("bold #BC3F3C");

  return {
      {HighlightTokenClass::COMMENT, COMMENT},  {HighlightTokenClass::KEYWORD, KEYWORD},
      {HighlightTokenClass::OPERATOR, KEYWORD}, {HighlightTokenClass::NUMBER, NUMBER},
      {HighlightTokenClass::REGEX, REGEX},      {HighlightTokenClass::STRING, STRING},
      {HighlightTokenClass::COMMAND, FUNC},     {HighlightTokenClass::COMMAND_ARG, PARAM},
      {HighlightTokenClass::REDIRECT, PARAM},   {HighlightTokenClass::VARIABLE, VAR},
      {HighlightTokenClass::TYPE, FUNC},        {HighlightTokenClass::MEMBER, PARAM}, // FIXME:
      {HighlightTokenClass::FOREGROUND_, TEXT}, {HighlightTokenClass::BACKGROUND_, BG},
      {HighlightTokenClass::ERROR_, ERR},
  };
}

} // namespace arsh::highlighter

#endif // ARSH_TOOLS_HIGHLIGHTER_STYLES_DARCULA_HPP
