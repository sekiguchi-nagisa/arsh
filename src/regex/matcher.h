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

#ifndef ARSH_REGEX_MATCHER_H
#define ARSH_REGEX_MATCHER_H

#include "misc/codepoint_set.hpp"
#include "misc/flag_util.hpp"
#include "unicode/property.h"

namespace arsh::regex {

inline bool isLineTerminator(int codePoint) {
  return codePoint == '\n' || codePoint == '\r' || codePoint == 0x2028 || codePoint == 0x2029;
}

inline bool isWord(int codePoint) {
  return (codePoint >= 'A' && codePoint <= 'Z') || (codePoint >= 'a' && codePoint <= 'z') ||
         (codePoint >= '0' && codePoint <= '9') || codePoint == '_';
}

inline bool isExtendWord(int codePoint) {
  return ucp::hasPrimeLoneProperty(codePoint, ucp::Lone::ESRegexClassExtendWord);
}

class AsciiSet {
private:
  uint64_t sets[2]{};

public:
  AsciiSet() = default;

  AsciiSet(uint64_t first, uint64_t second) : sets{first, second} {}

  void add(int codePoint) {
    if (codePoint <= 127 && codePoint > -1) {
      setFlag(this->sets[static_cast<unsigned int>(codePoint) / 64],
              static_cast<uint64_t>(1) << (static_cast<unsigned int>(codePoint) % 64));
    }
  }

  bool contains(int codePoint) const {
    if (codePoint <= 127 && codePoint > -1) {
      return hasFlag(this->sets[static_cast<unsigned int>(codePoint) / 64],
                     static_cast<uint64_t>(1) << (static_cast<unsigned int>(codePoint) % 64));
    }
    return false;
  }

  const auto &underlying() const { return this->sets; }
};

enum class MatcherType : unsigned char {
  ASCII,
  OWNED_CODE_POINT_SET,
  BORROWED_CODE_POINT_SET,
};

class Matcher {
private:
  static_assert(sizeof(MatcherType) == sizeof(uint8_t));

  static_assert(toUnderlying(MatcherType::ASCII) == 0);

  /**
   * | 56bit (data) | 8bit (MatcherType)
   *
   * if MatcherType is ASCII
   * | 56bit (bitset, 8~63 code point) | 8bit (MatcherType=ASCII)
   *
   * if MatcherType is *_CODE_POINT_SET
   * | 16bit (bmpSize) | 16bit (packedBmpSize) | 24bit (size) | 8bit (MatcherType=*_CODE_POINT_SET)
   */
  uint64_t first;
  union {
    uint64_t second;        // if MatcherType is ASCII
    BMPCodePointRange *ptr; // if MatcherType is OWNED_CODE_POINT_SET or BORROWED_CODE_POINT_SET
  };

public:
  static constexpr bool withinAsciiSet(const int codePoint) {
    return codePoint >= 8 && codePoint <= 127;
  }

  explicit Matcher(CodePointSet &&set) noexcept {
    const bool owned = set.isOwned();
    this->first = static_cast<uint64_t>(set.getBMPSize()) << 48;
    this->first |= static_cast<uint64_t>(set.getPackedNonBMPSize()) << 32;
    this->first |= static_cast<uint64_t>(set.getSize()) << 8;
    this->first |= toUnderlying(owned ? MatcherType::OWNED_CODE_POINT_SET
                                      : MatcherType::BORROWED_CODE_POINT_SET);
    this->ptr = std::move(set).take();
  }

  explicit Matcher(AsciiSet set) noexcept {
    this->first = set.underlying()[0];
    this->first &= ~(0xFF); // always ignore the first 8bit
    this->first |= toUnderlying(MatcherType::ASCII);
    this->second = set.underlying()[1];
  }

  Matcher(Matcher &&o) noexcept : first(o.first) {
    if (o.type() == MatcherType::ASCII) {
      this->second = o.second;
    } else {
      this->ptr = o.ptr;
    }
    o.first = 0;
  }

  ~Matcher() {
    if (type() == MatcherType::OWNED_CODE_POINT_SET) {
      free(this->ptr);
    }
  }

  Matcher &operator=(Matcher &o) noexcept {
    if (this != std::addressof(o)) {
      this->~Matcher();
      new (this) Matcher(std::move(o));
    }
    return *this;
  }

  MatcherType type() const { return static_cast<MatcherType>(this->first & 0xFF); }

  CodePointSetRef asCodePointSetRef() const {
    return {this->getBMPSize(), this->getPackedBMPSize(), this->ptr, this->getSize()};
  }

  AsciiSet asAsciiSet() const { return {this->first, this->second}; }

  bool contains(const int codePoint) const {
    if (type() == MatcherType::ASCII) {
      return this->asAsciiSet().contains(codePoint);
    }
    return this->asCodePointSetRef().contains(codePoint);
  }

private:
  unsigned short getBMPSize() const { return static_cast<unsigned short>(this->first >> 48); }

  unsigned short getPackedBMPSize() const {
    return static_cast<unsigned short>((this->first >> 32) & 0xFFFF);
  }

  unsigned int getSize() const { return static_cast<unsigned int>((this->first >> 8) & 0xFFFFFF); }
};

} // namespace arsh::regex

#endif // ARSH_REGEX_MATCHER_H
