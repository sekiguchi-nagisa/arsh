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

#ifndef ARSH_TOOLS_HIGHLIGHTER_FORMATTER_H
#define ARSH_TOOLS_HIGHLIGHTER_FORMATTER_H

#include <ostream>

#include "style.h"

namespace arsh::highlighter {

class Formatter : public Tokenizer {
protected:
  const Style style;
  const std::unordered_set<std::string> notFoundCmds; // for error highlight
  std::ostream &output;

public:
  Formatter(Style style, std::unordered_set<std::string> notFoundCmds, std::ostream &output)
      : Tokenizer(""), style(std::move(style)), notFoundCmds(std::move(notFoundCmds)),
        output(output) {}

  virtual void initialize(StringRef newSource);

  /**
   * write internal buffer to output
   */
  virtual void finalize();

  /**
   * dump internal formatter setting
   * @return
   */
  virtual std::string dump();

protected:
  void write(StringRef ref) { this->output.write(ref.data(), static_cast<ssize_t>(ref.size())); }

  void drawTrivia(StringRef ref);

  virtual void draw(StringRef ref, const HighlightTokenClass *tokenClass) = 0;
};

/**
 * just output
 */
class NullFormatter : public Formatter {
private:
  void draw(StringRef ref, const HighlightTokenClass *tokenClass) override;

public:
  NullFormatter(const Style &style, std::ostream &output) : Formatter(style, {}, output) {}
};

enum class TermColorCap : unsigned char {
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
  ANSIFormatter(const Style &style, std::unordered_set<std::string> notFoundCmds,
                std::ostream &output, TermColorCap cap)
      : Formatter(style, std::move(notFoundCmds), output), colorCap(cap) {}

  std::string dump() override;
};

enum class HTMLFormatOp : unsigned char {
  FULL = 1u << 0u,   // generate self-contained HTML (set background color)
  LINENO = 1u << 1u, // emit line number
  TABLE = 1u << 2u,  // emit as an HTML table (always combine LINENO option)
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
  HTMLFormatter(const Style &style, std::unordered_set<std::string> notFoundCmds,
                std::ostream &output, HTMLFormatOp op, unsigned int lineNumOffset)
      : Formatter(style, std::move(notFoundCmds), output), formatOp(op),
        lineNumOffset(lineNumOffset) {}

  void initialize(StringRef newSource) override;

  void finalize() override;
};

} // namespace arsh::highlighter

namespace arsh {

template <>
struct allow_enum_bitop<highlighter::HTMLFormatOp> : std::true_type {};

} // namespace arsh

#endif // ARSH_TOOLS_HIGHLIGHTER_FORMATTER_H
