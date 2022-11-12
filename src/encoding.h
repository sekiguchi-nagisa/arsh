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

#ifndef YDSH_ENCODING_H
#define YDSH_ENCODING_H

#include <array>

#include "misc/string_ref.hpp"

namespace ydsh {

// for unicode-aware character length counting

#define EACH_CHAR_WIDTH_PROPERY(OP)                                                                \
  OP(EAW, "○")                                                                                     \
  OP(EMOJI_FLAG_SEQ, "🇯🇵")                                                                         \
  OP(EMOJI_ZWJ_SEQ, "👩🏼‍🏭")

enum class CharWidthProperty {
#define GEN_ENUM(E, S) E,
  EACH_CHAR_WIDTH_PROPERY(GEN_ENUM)
#undef GEN_ENUM
};

constexpr unsigned int getCharWidthPropertyLen() {
  constexpr const CharWidthProperty table[] = {
#define GEN_ENUM(E, S) CharWidthProperty::E,
      EACH_CHAR_WIDTH_PROPERY(GEN_ENUM)
#undef GEN_ENUM
  };
  return std::size(table);
}

using CharWidthPropertyList =
    std::array<std::pair<CharWidthProperty, const char *>, getCharWidthPropertyLen()>;

const CharWidthPropertyList &getCharWidthPropertyList();

struct CharWidthProperties {
  bool fullWidth{false};
  unsigned char flagSeqWidth{4};
  bool zwjSeqFallback{false};

  void setProperty(CharWidthProperty p, std::size_t len) {
    switch (p) {
    case CharWidthProperty::EAW:
      this->fullWidth = len == 2;
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

#endif // YDSH_ENCODING_H
