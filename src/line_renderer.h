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
#include "misc/grapheme.hpp"

namespace ydsh {

// high level api for unicode-aware character op

#define EACH_CHAR_WIDTH_PROPERTY(OP)                                                               \
  OP(EAW, "○")                                                                                     \
  OP(EMOJI_FLAG_SEQ, "🇯🇵")                                                                         \
  OP(EMOJI_ZWJ_SEQ, "👩🏼‍🏭")

enum class CharWidthProperty {
#define GEN_ENUM(E, S) E,
  EACH_CHAR_WIDTH_PROPERTY(GEN_ENUM)
#undef GEN_ENUM
};

constexpr unsigned int getCharWidthPropertyLen() {
  constexpr const CharWidthProperty table[] = {
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
  UnicodeUtil::AmbiguousCharWidth eaw{UnicodeUtil::HALF_WIDTH};
  unsigned char flagSeqWidth{4};
  bool zwjSeqFallback{false};
  bool replaceInvalid{false};

  void setProperty(CharWidthProperty p, std::size_t len) {
    switch (p) {
    case CharWidthProperty::EAW:
      this->eaw = len == 2 ? UnicodeUtil::FULL_WIDTH : UnicodeUtil::HALF_WIDTH;
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
unsigned int getGraphemeWidth(const CharWidthProperties &ps, const GraphemeScanner::Result &ret);

enum class ColumnLenOp {
  NEXT,
  PREV,
};

struct ColumnLen {
  unsigned int byteSize; // consumed bytes
  unsigned int colSize;
};

class ColumnCounter {
private:
  const CharWidthProperties &ps;
  size_t totalColLen; // for tab stop. if reach \n, reset to 0

public:
  ColumnCounter(const CharWidthProperties &ps, size_t initColLen)
      : ps(ps), totalColLen(initColLen) {}

  size_t getTotalColLen() const { return this->totalColLen; }

  ColumnLen getCharLen(StringRef ref, ColumnLenOp op);

  ColumnLen getWordLen(StringRef ref, ColumnLenOp op);
};

/**
 * get length of last consumed grapheme cluster
 * @param ref
 * @param op
 * @param ps
 * @return
 */
inline ColumnLen getCharLen(StringRef ref, ColumnLenOp op, const CharWidthProperties &ps) {
  return ColumnCounter(ps, 0).getCharLen(ref, op);
}

/**
 * get length of last consumed word
 * @param ref
 * @param op
 * @param ps
 * @return
 */
inline ColumnLen getWordLen(StringRef ref, ColumnLenOp op, const CharWidthProperties &ps) {
  return ColumnCounter(ps, 0).getWordLen(ref, op);
}

inline StringRef::size_type startsWithAnsiEscape(StringRef ref) {
  if (ref.size() > 2 && ref[0] == '\x1b' && ref[1] == '[') {
    for (StringRef::size_type i = 2; i < ref.size(); i++) {
      switch (ref[i]) {
      case 'A':
      case 'B':
      case 'C':
      case 'D':
      case 'E':
      case 'F':
      case 'G':
      case 'H':
      case 'J':
      case 'K':
      case 'S':
      case 'T':
      case 'f':
      case 'm':
        return i + 1;
      default:
        break;
      }
    }
  }
  return 0;
}

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

  const size_t initColLen;

  size_t totalColLen{0};

  size_t lineNum{0};

  /**
   * if 0, ignore newline characters and not increment lineNum
   */
  size_t lineNumLimit{static_cast<size_t>(-1)};

  /**
   * append to existing content
   */
  std::string &output;

public:
  LineRenderer(const CharWidthProperties &ps, size_t initColLen, std::string &output,
               ObserverPtr<const ANSIEscapeSeqMap> escapeSeqMap = nullptr)
      : ps(ps), escapeSeqMap(escapeSeqMap), initColLen(initColLen), output(output) {}

  void setLineNumLimit(size_t limit) { this->lineNumLimit = limit; }

  /**
   * render lines with color code (ansi escape sequence).
   * ansi escape sequences are not quoted
   * @param prompt
   */
  void renderWithANSI(StringRef prompt);

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

  /**
   *
   * @param ref
   * @param tokenClass
   * @return
   * if reach lineNumLimit, return false
   */
  bool render(StringRef ref, HighlightTokenClass tokenClass);

  void renderControlChar(int codePoint);
};

} // namespace ydsh

#endif // YDSH_LINE_RENDERER_H
