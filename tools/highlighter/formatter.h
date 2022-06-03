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

  void emit(HighlightTokenClass tokenClass, Token token) override;

  virtual void finalize() = 0; // write internal buffer to output

protected:
  void write(StringRef ref) { this->output.write(ref.data(), ref.size()); }

  virtual void draw(StringRef ref, const HighlightTokenClass *tokenClass) = 0;
};

/**
 * just output
 */
class NullFormatter : public Formatter {
private:
  void draw(StringRef ref, const HighlightTokenClass *tokenClass) override;

public:
  NullFormatter(StringRef source, const Style &style, std::ostream &output)
      : Formatter(source, style, output) {}

  void finalize() override;
};

enum class TermColorCap {
  TRUE_COLOR,
  INDEXED_256,
};

class IndexedColorPalette256 {
private:
  std::vector<Color> values;

public:
  IndexedColorPalette256();

  Color operator[](unsigned char index) const { return this->values[index]; }

  [[nodiscard]] unsigned char findClosest(Color color) const;
};

class ANSIFormatter : public Formatter {
private:
  const TermColorCap colorCap;

  const IndexedColorPalette256 colorPalette256;

  std::unordered_map<HighlightTokenClass, std::string> escapeSeqCache;

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

  const std::string &toEscapeSeq(HighlightTokenClass tokenClass);

  void draw(StringRef ref, const HighlightTokenClass *tokenClass) override;

public:
  ANSIFormatter(StringRef source, const Style &style, std::ostream &output, TermColorCap cap)
      : Formatter(source, style, output), colorCap(cap) {}

  void finalize() override;
};

enum class HTMLFormatOp : unsigned int {
  FULL = 1u << 0u,   // generate self-contained html (set background color)
  LINENO = 1u << 1u, // emit line number
};

class HTMLFormatter : public Formatter {
private:
  const HTMLFormatOp formatOp;

  const unsigned int lineNumOffset;

  std::unordered_map<HighlightTokenClass, std::string> cssCache;

  unsigned int newlineCount{0}; // 0-based line number count

  unsigned int maxLineNumDigits{0}; // for line number width

  void emitLineNum(unsigned int lineNum);

  const std::string &toCSS(HighlightTokenClass tokenClass);

  void draw(StringRef ref, const HighlightTokenClass *tokenClass) override;

public:
  HTMLFormatter(StringRef source, const Style &style, std::ostream &output, HTMLFormatOp op,
                unsigned int lineNumOffset);

  void finalize() override;
};

} // namespace ydsh::highlighter

namespace ydsh {

template <>
struct allow_enum_bitop<highlighter::HTMLFormatOp> : std::true_type {};

} // namespace ydsh

#endif // YDSH_TOOLS_HIGHLIGHTER_FORMATTER_H
