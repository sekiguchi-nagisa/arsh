/*
 * Copyright (C) 2025 Nagisa Sekiguchi
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

#ifndef ARSH_VALUE_H
#define ARSH_VALUE_H

#include <cstdint>
#include <cstring>

#include "misc/detect.hpp"

namespace arsh {

enum class TaggedValue : uint64_t {}; // lower 3bit is a tag (up to 7)

inline uint8_t getTag(const TaggedValue v) { return static_cast<uint64_t>(v) & 0x7; }

inline bool hasTag(const TaggedValue v, const uint8_t t) { return getTag(v) == t; }

inline uint64_t rotateLeft(const uint64_t x, const unsigned int k) {
  return (x << k) | (x >> (64 - k));
}

/**
 * encode float64 to tagged value (Float Self-Tagging with 1 Tag)
 * see. https://doi.org/10.1145/3763108
 * @tparam TAG
 * @param v
 * @return
 */
template <uint8_t TAG>
TaggedValue encodeTaggedFloat(const double v) {
  static_assert(TAG <= 7);
  union {
    double d;
    uint64_t u;
  } e = {.d = v};
  return static_cast<TaggedValue>(rotateLeft(e.u + (static_cast<uint64_t>(1 + 2 * TAG) << 58), 5));
}

/**
 * decode tagged value to float64 (Float Self-Tagging with 1 Tag)
 * see. https://doi.org/10.1145/3763108
 * @tparam TAG
 * @param v
 * @return
 */
template <uint8_t TAG>
double decodeTaggedFloat(const TaggedValue v) {
  static_assert(TAG <= 7);
  const uint64_t vv =
      rotateLeft(static_cast<uint64_t>(v), 59) - (static_cast<uint64_t>(1 + 2 * TAG) << 58);
  union {
    double d;
    uint64_t u;
  } e = {.u = vv};
  return e.d;
}

union InlinedString {
  TaggedValue v; // lower 8bit => len (5bit) | tag (3bit)
  char s[8];     // assume little-endian

  static constexpr unsigned int MAX_SIZE = sizeof(s) - 2;

  unsigned int size() const { return static_cast<unsigned int>(this->s[0]) >> 3; }

  /**
   *
   * @return null terminated
   */
  const char *data() const { return this->s + 1; }

  /**
   * @tparam TAG
   * @param data
   * @param size up to 6
   */
  template <uint8_t TAG>
  void set(const char *data, const size_t size) {
    static_assert(TAG <= 7);
    this->s[0] = static_cast<uint8_t>((size << 3) | TAG);
    memcpy(this->s + 1, data, size);
    this->s[size + 1] = '\0';
  }

  template <uint8_t TAG>
  void append(const char *data, const size_t size) {
    static_assert(TAG <= 7);
    size_t oldSize = this->size();
    size_t newSize = oldSize + size;
    this->s[0] = static_cast<uint8_t>((newSize << 3) | TAG);
    memcpy(this->s + 1 + oldSize, data, size);
    this->s[newSize + 1] = '\0';
  }
};

constexpr auto INT56_MAX = static_cast<int64_t>(0x7FFFFFFFFFFFFF);
constexpr auto INT56_MIN = static_cast<int64_t>(0xFF80000000000000);

inline bool withinInt56(int64_t v) { return v >= INT56_MIN && v <= INT56_MAX; }

/**
 * sign (1bit) | truncate (8bit) | remain (55bit)
 * => remain (55bit) | sign (1bit) | tag (8bit)
 * @tparam T
 * @param v must be int56
 * @return
 */
template <typename T, enable_when<sizeof(T) == sizeof(uint8_t) &&
                                  (std::is_integral_v<T> || std::is_enum_v<T>)> = nullptr>
TaggedValue encodeTaggedInt(const T tag, const int64_t v) {
  uint64_t vv = rotateLeft(static_cast<uint64_t>(v), 9);
  return static_cast<TaggedValue>((vv & ~static_cast<uint64_t>(0xFF)) | static_cast<uint8_t>(tag));
}

/**
 * remain (55bit) | sign (1bit) | tag (8bit)
 * => remain (55bit) | sign (1bit) | pad (8bit)
 * => sign (1bit) | pad (8bit) | remain (55bit)
 * if sign is 1, pad (0xFF)
 * @param v
 * @return */
inline int64_t decodeTaggedInt(const TaggedValue v) {
  auto vv = static_cast<uint64_t>(v) & ~static_cast<uint64_t>(0xFF);
  if (vv & 0x100) { // sign
    vv |= 0xFF;
  }
  return static_cast<int64_t>(rotateLeft(vv, 55));
}

constexpr auto UINT56_MAX = static_cast<uint64_t>(0xFFFFFFFFFFFFFF);

inline bool withinUInt56(const uint64_t v) { return v <= UINT56_MAX; }

/**
 * truncate (8bit) | remain (56bit)
 * => remain (56bit) | tag (8bit)
 * @tparam T
 * @param v must be uint56
 * @return
 */
template <typename T, enable_when<sizeof(T) == sizeof(uint8_t) &&
                                  (std::is_integral_v<T> || std::is_enum_v<T>)> = nullptr>
TaggedValue encodeTaggedUInt(const T tag, const uint64_t v) {
  uint64_t vv = rotateLeft(v, 8);
  return static_cast<TaggedValue>((vv & ~static_cast<uint64_t>(0xFF)) | static_cast<uint8_t>(tag));
}

/**
 * remain (56bit) | tag (8bit)
 * => pad (8bit) | remain (56bit)
 * pad is 0
 * @param v
 * @return
 */
inline uint64_t decodeTaggedUInt(const TaggedValue v) {
  auto vv = static_cast<uint64_t>(v) & ~static_cast<uint64_t>(0xFF);
  return rotateLeft(vv, 56);
}

} // namespace arsh

#endif // ARSH_VALUE_H
