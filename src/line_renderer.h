/*
 * Copyright (C) 2023 Nagisa Sekiguchi
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

#ifndef YDSH_LINE_RENDERER_H
#define YDSH_LINE_RENDERER_H

#include "highlighter_base.h"

namespace ydsh {

class ANSIEscapeSeqMap {
private:
  std::unordered_map<HighlightTokenClass, std::string> values;

public:
  ANSIEscapeSeqMap() = default;

  explicit ANSIEscapeSeqMap(std::unordered_map<HighlightTokenClass, std::string> &&values)
      : values(std::move(values)) {} // for testing

  static ANSIEscapeSeqMap fromString(StringRef setting);

  const auto &getValues() const { return this->values; }
};

struct CharWidthProperties;

/**
 * generate output string for terminal
 */
class LineRenderer {
private:
  const CharWidthProperties &ps;

  /**
   * may be null
   */
  const ObserverPtr<const ANSIEscapeSeqMap> escapeSeqMap;

  std::vector<std::pair<HighlightTokenClass, Token>> tokens; // for syntax highlight

  const size_t initOffset;

  /**
   * append to existing content
   */
  std::string &output;

public:
  LineRenderer(const CharWidthProperties &ps, size_t initOffset, std::string &output,
               ObserverPtr<const ANSIEscapeSeqMap> escapeSeqMap = nullptr)
      : ps(ps), escapeSeqMap(escapeSeqMap), initOffset(initOffset), output(output) {}

  /**
   * render prompt string.
   * asci escape sequence is not quoted
   * @param prompt
   */
  void renderPrompt(StringRef prompt);

  /**
   * render script (perform syntax highlight, line continuation check)
   * @param source
   * @return
   * reach incomplete input (need more characters), return false
   */
  bool renderScript(StringRef source);

  void renderLines(StringRef source) { this->render(source, HighlightTokenClass::NONE); }

private:
  const std::string *findColorCode(HighlightTokenClass tokenClass) const;

  void render(StringRef ref, HighlightTokenClass tokenClass);
};

} // namespace ydsh

#endif // YDSH_LINE_RENDERER_H
