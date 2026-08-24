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
#include "unicode/grapheme.h"

namespace arsh {

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

  unsigned int initCols;

  unsigned int totalCols{0};

  unsigned int totalRows{0};

  unsigned int maxTotalCols{0};

  unsigned int colLimit{UINT32_MAX};

  bool emitNewline{true}; // if false, not append newline (\n) and not increment totalRows

  LineBreakOp breakOp{LineBreakOp::SOFT_WRAP};

  /**
   * append to existing content
   */
  ObserverPtr<std::vector<std::string>> output;

  ObserverPtr<TokenizerResult> tokenizeResult;

public:
  LineRenderer(const CharWidthProperties &ps, unsigned int initCols,
               ObserverPtr<std::vector<std::string>> output,
               ObserverPtr<const ANSIEscapeSeqMap> escapeSeqMap)
      : ps(ps), escapeSeqMap(escapeSeqMap), initCols(initCols), totalCols(initCols),
        output(output) {
    if (this->output && this->output->empty()) {
      this->output->emplace_back();
    }
  }

  LineRenderer(const CharWidthProperties &ps, unsigned int initCols, std::vector<std::string> &output,
               ObserverPtr<const ANSIEscapeSeqMap> escapeSeqMap = nullptr)
      : LineRenderer(ps, initCols, makeObserver(output), escapeSeqMap) {}

  LineRenderer(const CharWidthProperties &ps, unsigned int initCols)
      : LineRenderer(ps, initCols, nullptr, nullptr) {}

  void setInitCols(unsigned int init) { this->initCols = init; }

  void setColLimit(unsigned int limit) { this->colLimit = limit; }

  unsigned int getTotalCols() const { return this->totalCols; }

  unsigned int getTotalRows() const { return this->totalRows; }

  unsigned int getMaxTotalCols() const { return std::max(this->maxTotalCols, this->totalCols); }

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

  std::string &line() const { return this->output->back(); }

  void newline() const {
    this->line() += "\r\n";
    this->output->emplace_back();
  }

  void handleSoftWrap(const std::string *color) {
    this->totalCols = 0;
    this->totalRows++;
    if (this->output) {
      if (color) {
        this->line() += "\x1b[0m";
      }
      this->newline();
      if (color) {
        this->line() += *color;
      }
    }
    this->maxTotalCols = std::max(this->maxTotalCols, this->colLimit);
  }

  void handleTruncate(const char pad) {
    if (this->output) {
      this->line().append(this->colLimit - this->totalCols, pad);
    }
    this->totalCols = this->colLimit;
    this->maxTotalCols = std::max(this->maxTotalCols, this->colLimit);
  }
};

} // namespace arsh

#endif // ARSH_LINE_RENDERER_H
