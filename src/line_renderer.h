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

#ifndef ARSH_LINE_RENDERER_H
#define ARSH_LINE_RENDERER_H

#include "highlighter_base.h"
#include "misc/grapheme.hpp"

namespace arsh {

// high-level api for unicode-aware character op

#define EACH_CHAR_WIDTH_PROPERTY(OP)                                                               \
  OP(EAW, "○")                                                                                     \
  OP(RGI, "🇯")                                                                                     \
  OP(EMOJI_FLAG_SEQ, "🇯🇵")                                                                         \
  OP(EMOJI_ZWJ_SEQ, "👩🏼‍🏭")

enum class CharWidthProperty : unsigned char {
#define GEN_ENUM(E, S) E,
  EACH_CHAR_WIDTH_PROPERTY(GEN_ENUM)
#undef GEN_ENUM
};

constexpr unsigned int getCharWidthPropertyLen() {
  constexpr CharWidthProperty table[] = {
#define GEN_ENUM(E, S) CharWidthProperty::E,
      EACH_CHAR_WIDTH_PROPERTY(GEN_ENUM)
#undef GEN_ENUM
  };
  return std::size(table);
}

using CharWidthPropertyList =
    std::array<std::pair<CharWidthProperty, const char *>, getCharWidthPropertyLen()>;

const CharWidthPropertyList &getCharWidthPropertyList();

struct CharWidthProperties {
  AmbiguousCharWidth eaw{AmbiguousCharWidth::HALF};
  unsigned char reginalIndicatorWidth{0}; // if 0, use original width
  unsigned char flagSeqWidth{2};
  bool zwjSeqFallback{false};
  bool replaceInvalid{false};

  void setProperty(CharWidthProperty p, std::size_t len) {
    switch (p) {
    case CharWidthProperty::RGI:
      this->reginalIndicatorWidth = len;
      break;
    case CharWidthProperty::EAW:
      this->eaw = len == 2 ? AmbiguousCharWidth::FULL : AmbiguousCharWidth::HALF;
      break;
    case CharWidthProperty::EMOJI_FLAG_SEQ:
      this->flagSeqWidth = len;
      break;
    case CharWidthProperty::EMOJI_ZWJ_SEQ:
      this->zwjSeqFallback = len > 2;
      break;
    }
  }
};

/**
 * get width of a grapheme cluster
 * @param ps
 * @param ret
 * @return
 */
unsigned int getGraphemeWidth(const CharWidthProperties &ps, const GraphemeCluster &ret);

class ANSIEscapeSeqMap {
private:
  std::unordered_map<HighlightTokenClass, std::string> values;

public:
  ANSIEscapeSeqMap() = default;

  explicit ANSIEscapeSeqMap(std::unordered_map<HighlightTokenClass, std::string> &&values)
      : values(std::move(values)) {} // for testing

  static ANSIEscapeSeqMap fromString(StringRef setting);

  static bool checkSGRSeq(StringRef seq);

  const auto &getValues() const { return this->values; }
};

/**
 * generate output string for terminal
 */
class LineRenderer {
public:
  enum class LineBreakOp : unsigned char {
    SOFT_WRAP,
    TRUNCATE,
  };

  static constexpr unsigned int TAB_WIDTH = SYS_LINE_RENDERER_TAB_WIDTH;

private:
  const CharWidthProperties &ps;

  /**
   * maybe null
   */
  const ObserverPtr<const ANSIEscapeSeqMap> escapeSeqMap;

  size_t initCols;

  size_t totalCols{0};

  size_t totalRows{0};

  size_t maxTotalCols{0};

  size_t colLimit{static_cast<size_t>(-1)};

  bool emitNewline{true}; // if false, not append newline (\n) and not increment totalRows

  LineBreakOp breakOp{LineBreakOp::SOFT_WRAP};

  /**
   * append to existing content
   */
  ObserverPtr<std::string> output;

  ObserverPtr<TokenizerResult> tokenizeResult;

public:
  LineRenderer(const CharWidthProperties &ps, size_t initCols, ObserverPtr<std::string> output,
               ObserverPtr<const ANSIEscapeSeqMap> escapeSeqMap)
      : ps(ps), escapeSeqMap(escapeSeqMap), initCols(initCols), totalCols(initCols),
        output(output) {}

  LineRenderer(const CharWidthProperties &ps, size_t initCols, std::string &output,
               ObserverPtr<const ANSIEscapeSeqMap> escapeSeqMap = nullptr)
      : LineRenderer(ps, initCols, makeObserver(output), escapeSeqMap) {}

  LineRenderer(const CharWidthProperties &ps, size_t initCols)
      : LineRenderer(ps, initCols, nullptr, nullptr) {}

  void setInitCols(size_t init) { this->initCols = init; }

  void setColLimit(size_t limit) { this->colLimit = limit; }

  size_t getTotalCols() const { return this->totalCols; }

  size_t getTotalRows() const { return this->totalRows; }

  size_t getMaxTotalCols() const { return std::max(this->maxTotalCols, this->totalCols); }

  void setEmitNewline(bool set) { this->emitNewline = set; }

  void setLineBreakOp(LineBreakOp op) { this->breakOp = op; }

  const ANSIEscapeSeqMap *getEscapeSeqMap() const { return this->escapeSeqMap.get(); }

  void setTokenizeResult(ObserverPtr<TokenizerResult> ret) { this->tokenizeResult = ret; }

  /**
   * render lines with color code (ansi escape sequence).
   * ansi escape sequences are not quoted
   * @param prompt
   */
  void renderWithANSI(StringRef prompt);

  /**
   * render a script (perform syntax highlight, line continuation check)
   * @param source
   * @param errorCmdChecker
   * @return
   * reach incomplete input (need more characters), return false
   */
  bool renderScript(StringRef source,
                    const std::function<bool(StringRef)> &errorCmdChecker = nullptr);

  void renderLines(StringRef source) { this->render(source, HighlightTokenClass::NONE_); }

private:
  const std::string *findColorCode(HighlightTokenClass tokenClass) const;

  /**
   *
   * @param ref
   * @param tokenClass
   * @return
   * if reach lineNumLimit or colLenLimit, return false
   */
  bool render(StringRef ref, HighlightTokenClass tokenClass);

  /**
   *
   * @param codePoint
   * @param color
   * @return
   * if reach lineNumLimit or colLenLimit, return false
   */
  bool renderControlChar(int codePoint, const std::string *color);

  void handleSoftWrap(const std::string *color) {
    this->totalCols = 0;
    this->totalRows++;
    if (this->output) {
      if (color) {
        *this->output += "\x1b[0m";
      }
      *this->output += "\r\n";
      if (color) {
        *this->output += *color;
      }
    }
    this->maxTotalCols = std::max(this->maxTotalCols, this->colLimit);
  }

  void handleTruncate(char pad) {
    if (this->output) {
      this->output->append(this->colLimit - this->totalCols, pad);
    }
    this->totalCols = this->colLimit;
    this->maxTotalCols = std::max(this->maxTotalCols, this->colLimit);
  }
};

} // namespace arsh

#endif // ARSH_LINE_RENDERER_H
