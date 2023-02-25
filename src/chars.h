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

#ifndef YDSH_CHARS_H
#define YDSH_CHARS_H

#include <array>
#include <functional>

#include "misc/detect.hpp"
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

/**
 * iterate grapheme cluster
 * @param ref
 * @param limit
 * if number of grapheme clusters reach limit, break iteration
 * @param consumer
 * callback for scanned grapheme
 * @return
 * total number of scanned grapheme clusters
 */

template <typename Consumer>
static constexpr bool grapheme_consumer_requirement_v =
    std::is_same_v<void, std::invoke_result_t<Consumer, const GraphemeScanner::Result &>>;

template <typename Func, enable_when<grapheme_consumer_requirement_v<Func>> = nullptr>
size_t iterateGraphemeUntil(StringRef ref, size_t limit, Func consumer) {
  GraphemeScanner scanner(ref);
  GraphemeScanner::Result ret;
  size_t count = 0;
  for (; count < limit && scanner.next(ret); count++) {
    consumer(ret);
  }
  return count;
}

template <typename Func, enable_when<grapheme_consumer_requirement_v<Func>> = nullptr>
size_t iterateGrapheme(StringRef ref, Func consumer) {
  return iterateGraphemeUntil(ref, static_cast<size_t>(-1), std::move(consumer));
}

enum class CharLenOp {
  NEXT_CHAR,
  PREV_CHAR,
};

struct ColumnLen {
  unsigned int byteSize; // consumed bytes
  unsigned int colSize;
};

ColumnLen getCharLen(StringRef ref, CharLenOp op, const CharWidthProperties &ps);

enum class WordLenOp {
  NEXT_WORD,
  PREV_WORD,
};

ColumnLen getWordLen(StringRef ref, WordLenOp op, const CharWidthProperties &ps);

} // namespace ydsh

#endif // YDSH_CHARS_H
