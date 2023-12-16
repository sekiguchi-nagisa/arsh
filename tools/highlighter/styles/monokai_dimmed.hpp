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

#ifndef ARSH_TOOLS_HIGHLIGHTER_STYLES_MONOKAI_DIMMED_HPP
#define ARSH_TOOLS_HIGHLIGHTER_STYLES_MONOKAI_DIMMED_HPP

#include "../style.h"

namespace arsh::highlighter {

DEFINE_HIGHLIGHT_STYLE(monokai_dimmed) {
  constexpr auto COMMENT = ValidRule("#9A9B99");
  constexpr auto KEYWORD = ValidRule("#9872A2");
  constexpr auto OPERATOR = ValidRule("#676867");
  constexpr auto NUMBER = ValidRule("#6089B4");
  constexpr auto STRING = ValidRule("#9AA83A");
  constexpr auto VAR = ValidRule("#6089B4");
  constexpr auto FUNC = ValidRule("#CE6700");
  constexpr auto PARAM = ValidRule("#6089B4");
  constexpr auto TYPE = ValidRule("#9B0000");
  constexpr auto TEXT = ValidRule("#C5C8C6");
  constexpr auto BG = ValidRule("bg:#1E1E1E");

  return {
      {HighlightTokenClass::COMMENT, COMMENT},   {HighlightTokenClass::KEYWORD, KEYWORD},
      {HighlightTokenClass::OPERATOR, OPERATOR}, {HighlightTokenClass::NUMBER, NUMBER},
      {HighlightTokenClass::REGEX, STRING},      {HighlightTokenClass::STRING, STRING},
      {HighlightTokenClass::COMMAND, FUNC},      {HighlightTokenClass::COMMAND_ARG, PARAM},
      {HighlightTokenClass::REDIRECT, OPERATOR}, {HighlightTokenClass::VARIABLE, VAR},
      {HighlightTokenClass::TYPE, TYPE},         {HighlightTokenClass::MEMBER, PARAM}, // FIXME:
      {HighlightTokenClass::FOREGROUND_, TEXT},  {HighlightTokenClass::BACKGROUND_, BG},
  };
}

} // namespace arsh::highlighter

#endif // ARSH_TOOLS_HIGHLIGHTER_STYLES_MONOKAI_DIMMED_HPP
