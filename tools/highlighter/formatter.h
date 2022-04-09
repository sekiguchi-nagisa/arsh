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

#ifndef YDSH_TOOLS_HIGHLIGHTER_FORMATTER_H
#define YDSH_TOOLS_HIGHLIGHTER_FORMATTER_H

#include <ostream>

#include "style.h"

namespace ydsh::highlighter {

class Formatter : public TokenEmitter {
protected:
  const Style &style;
  std::ostream &output;
  unsigned int curSrcPos{0};

public:
  Formatter(StringRef source, const Style &style, std::ostream &output)
      : TokenEmitter(source), style(style), output(output) {}

  virtual void finalize() = 0; // write internal buffer to output

protected:
  void write(StringRef ref) { this->output.write(ref.data(), ref.size()); }
};

/**
 * just output
 */
class NullFormatter : public Formatter {
private:
  void emit(HighlightTokenClass tokenClass, Token token) override;

public:
  NullFormatter(StringRef source, const Style &style, std::ostream &output)
      : Formatter(source, style, output) {}

  void finalize() override;
};

enum class TermColorCap {
  TRUE_COLOR,
  INDEXED_256,
};

class ANSIFormatter : public Formatter {
private:
  TermColorCap colorCap{TermColorCap::TRUE_COLOR};

  /**
   * format color escape sequence
   * if colorCap is not true-color, decrease color.
   * cache decreased result
   * @param c
   * @param background
   * if true, treat as background color
   * @return
   */
  std::string format(Color c, bool background);

  void emit(HighlightTokenClass tokenClass, Token token) override;

public:
  ANSIFormatter(StringRef source, const Style &style, std::ostream &output)
      : Formatter(source, style, output) {}

  void setColorCap(TermColorCap cap) { this->colorCap = cap; }

  void finalize() override;
};

} // namespace ydsh::highlighter

#endif // YDSH_TOOLS_HIGHLIGHTER_FORMATTER_H
