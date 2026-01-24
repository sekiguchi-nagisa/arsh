/*
 * Copyright (C) 2026 Nagisa Sekiguchi
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

#ifndef ARSH_UNICODE_EMOJI_SEQ_H
#define ARSH_UNICODE_EMOJI_SEQ_H

#include <algorithm>

#include "../misc/enum_util.hpp"
#include "../misc/string_ref.hpp"

namespace arsh {

#define EACH_EMOJI_PROPERTY(E)                                                                     \
  E(Basic_Emoji, (1u << 0u))                                                                       \
  E(Emoji_Keycap_Sequence, (1u << 1u))                                                             \
  E(RGI_Emoji_Modifier_Sequence, (1u << 2u))                                                       \
  E(RGI_Emoji_Flag_Sequence, (1u << 3u))                                                           \
  E(RGI_Emoji_Tag_Sequence, (1u << 4u))                                                            \
  E(RGI_Emoji_ZWJ_Sequence, (1u << 5u))                                                            \
  E(RGI_Emoji, ((1u << 6u) - 1u))

enum class EmojiProperty : unsigned char {
  None = 0,
#define GEN_ENUM(E, B) E = (B),
  EACH_EMOJI_PROPERTY(GEN_ENUM)
#undef GEN_ENUM
};

template <>
struct allow_enum_bitop<EmojiProperty> : std::true_type {};

struct RadixChildIter {
  static constexpr unsigned int CHILD_OFFSET_BYTES = 2;

  const uint8_t *ptr{nullptr};

  unsigned int offset{0};

  using difference_type = std::ptrdiff_t;

  using value_type = char;

  using reference = char &;

  using pointer = char *;

  using iterator_category = std::forward_iterator_tag;

  RadixChildIter(const uint8_t *ptr, size_t offset, size_t n)
      : ptr(ptr), offset(offset + (n * CHILD_OFFSET_BYTES)) {}

  unsigned int childOffset() const {
    unsigned int ret = 0;
    for (unsigned int i = 0; i < CHILD_OFFSET_BYTES; i++) {
      ret <<= 8;
      ret |= this->ptr[this->offset + i];
    }
    return ret;
  }

  auto operator*() const { return static_cast<char>(this->ptr[this->childOffset() + 1]); }

  auto operator-(const RadixChildIter &other) const {
    return (this->offset - other.offset) / CHILD_OFFSET_BYTES;
  }

  bool operator==(const RadixChildIter &other) const { return this->offset == other.offset; }

  bool operator!=(const RadixChildIter &other) const { return this->offset != other.offset; }

  auto &operator++() {
    this->offset += CHILD_OFFSET_BYTES;
    return *this;
  }
};

inline EmojiProperty lookupEmojiPropertyFrom(StringRef ref, const uint8_t *const ptr,
                                             const size_t size) {
  /*
   * lookup from a serialized radix tree
   *
   * `ptr` confirm the following binary format
   *
   * | prefix len (1 byte, up to 64 code points) | prefix (N bytes) | property (1 byte) |
   * | n_child (1 byte, up to 256 children) | 1 child (3 bytes) | ... | N child (3 bytes) |
   * | ... next node
   *
   */
  for (unsigned int offset = 0; offset < size;) {
    const unsigned int prefixLen = ptr[offset++];
    const StringRef prefix(reinterpret_cast<const char *>(ptr) + offset, prefixLen);
    if (!ref.startsWith(prefix)) {
      break;
    }
    offset += prefixLen;
    auto property = static_cast<EmojiProperty>(ptr[offset++]);
    ref.removePrefix(prefixLen);
    if (ref.empty()) {
      if (property != EmojiProperty::None) { // reach edge
        return property;
      }
      break;
    }
    // visit children
    const unsigned int n = ptr[offset++];
    const RadixChildIter begin(ptr, offset, 0);
    const RadixChildIter end(ptr, offset, n);
    const auto iter = std::lower_bound(begin, end, ref[0]);
    if (iter != end && *iter == ref[0]) {
      offset = iter.childOffset();
      assert(offset < size);
    } else {
      break;
    }
  }
  return EmojiProperty::None;
}

} // namespace arsh

#endif // ARSH_UNICODE_EMOJI_SEQ_H
