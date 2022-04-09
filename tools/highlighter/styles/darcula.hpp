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

#ifndef YDSH_TOOLS_HIGHLIGHTER_STYLES_DARCULA_HPP
#define YDSH_TOOLS_HIGHLIGHTER_STYLES_DARCULA_HPP

#include "../style.h"

namespace ydsh::highlighter {
/**
 * intellij darcula theme
 */
DEFINE_HIGHLIGHT_STYLE(darcula) {
  constexpr auto COMMENT = styleRule("#808080");
  constexpr auto KEYWORD = styleRule("#CC7832 bold");
  constexpr auto NUMBER = styleRule("#6897BB");
  constexpr auto REGEX = styleRule("#646695");
  constexpr auto STRING = styleRule("#6A8759");
  constexpr auto VAR = styleRule("#9876AA");
  constexpr auto FUNC = styleRule("#FFC66D");
  constexpr auto PARAM = styleRule("#A9B7C6");
  constexpr auto TEXT = styleRule("#D4D4D4");

  return {
      {HighlightTokenClass::COMMENT, COMMENT},   {HighlightTokenClass::KEYWORD, KEYWORD},
      {HighlightTokenClass::OPERATOR, KEYWORD},  {HighlightTokenClass::NUMBER, NUMBER},
      {HighlightTokenClass::REGEX, REGEX},       {HighlightTokenClass::STRING, STRING},
      {HighlightTokenClass::SIGNAL, VAR},        {HighlightTokenClass::COMMAND, FUNC},
      {HighlightTokenClass::COMMAND_ARG, PARAM}, {HighlightTokenClass::REDIRECT, PARAM},
      {HighlightTokenClass::VARIABLE, VAR},      {HighlightTokenClass::TYPE, FUNC},
      {HighlightTokenClass::MEMBER, PARAM}, // FIXME:
      {HighlightTokenClass::NONE, TEXT},
  };
}

} // namespace ydsh::highlighter

#endif // YDSH_TOOLS_HIGHLIGHTER_STYLES_DARCULA_HPP
