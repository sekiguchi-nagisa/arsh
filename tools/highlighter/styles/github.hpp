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

#ifndef YDSH_TOOLS_HIGHLIGHTER_STYLES_GITHUB_HPP
#define YDSH_TOOLS_HIGHLIGHTER_STYLES_GITHUB_HPP

#include "../style.h"

namespace ydsh::highlighter {

DEFINE_HIGHLIGHT_STYLE(github) {
  constexpr auto COMMENT = ValidRule("#6e7781");
  constexpr auto KEYWORD = ValidRule("#cf222e");
  constexpr auto NUMBER = ValidRule("#0550ae");
  constexpr auto STRING = ValidRule("#0a3069");
  constexpr auto REGEX = ValidRule("#0550ae");
  constexpr auto TYPE = ValidRule("#953800");
  constexpr auto VAR = ValidRule("#24292f");
  constexpr auto FUNC = ValidRule("#8250df");
  constexpr auto PARAM = ValidRule("#953800");
  constexpr auto TEXT = ValidRule("#24292F");
  constexpr auto BG = ValidRule(" bg:#FFFFFF");

  return {
      {HighlightTokenClass::COMMENT, COMMENT},   {HighlightTokenClass::KEYWORD, KEYWORD},
      {HighlightTokenClass::OPERATOR, KEYWORD},  {HighlightTokenClass::NUMBER, NUMBER},
      {HighlightTokenClass::REGEX, REGEX},       {HighlightTokenClass::STRING, STRING},
      {HighlightTokenClass::SIGNAL, VAR},        {HighlightTokenClass::COMMAND, FUNC},
      {HighlightTokenClass::COMMAND_ARG, PARAM}, {HighlightTokenClass::REDIRECT, PARAM},
      {HighlightTokenClass::VARIABLE, VAR},      {HighlightTokenClass::TYPE, TYPE},
      {HighlightTokenClass::MEMBER, PARAM}, // FIXME:
      {HighlightTokenClass::FOREGROUND_, TEXT},  {HighlightTokenClass::BACKGROUND_, BG},
  };
}

} // namespace ydsh::highlighter

#endif // YDSH_TOOLS_HIGHLIGHTER_STYLES_GITHUB_HPP
