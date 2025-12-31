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

#ifndef ARSH_UNICODE_CODEPOINT_SET_REF_H
#define ARSH_UNICODE_CODEPOINT_SET_REF_H

#include <algorithm>
#include <cassert>
#include <cstdint>

#include "../misc/array_ref.hpp"

namespace arsh {

class BMPCodePointRange {
private:
  /**
   * BMP code point range (inclusive, inclusive)
   */
  uint32_t value;

public:
  constexpr BMPCodePointRange(uint16_t first, uint16_t last)
      : value(static_cast<uint32_t>(first) << 16 | static_cast<uint32_t>(last)) {}

  /**
   * unsage api. normally unused
   * @param value
   */
  constexpr BMPCodePointRange(uint32_t value) : value(value) {} // NOLINT

  uint16_t firstBMP() const { return static_cast<uint16_t>(this->value >> 16); }

  uint16_t lastBMP() const { return static_cast<uint16_t>(this->value & 0xFFFF); }

  uint32_t underlying() const { return this->value; }

  struct SearchComp {
    bool operator()(const BMPCodePointRange &x, uint16_t y) const { return x.lastBMP() < y; }

    bool operator()(uint16_t x, const BMPCodePointRange &y) const { return x < y.firstBMP(); }
  };
};

class NonBMPCodePointRange : public BMPCodePointRange {
private:
  BMPCodePointRange next;

public:
  constexpr NonBMPCodePointRange(int first, int last) : BMPCodePointRange(first), next(last) {}

  int firstNonBMP() const { return static_cast<int>(this->underlying()); }

  int lastNonBMP() const { return static_cast<int>(this->next.underlying()); }

  struct SearchComp {
    bool operator()(const NonBMPCodePointRange &x, int y) const { return x.lastNonBMP() < y; }

    bool operator()(int x, const NonBMPCodePointRange &y) const { return x < y.firstNonBMP(); }
  };
};

class CodePointSetRef {
private:
  static_assert(sizeof(BMPCodePointRange) == sizeof(uint32_t));
  static_assert(sizeof(NonBMPCodePointRange) == sizeof(uint32_t) * 2);

  const BMPCodePointRange *ptr{nullptr};
  unsigned short bmpSize{0};
  unsigned int size{0};

public:
  constexpr CodePointSetRef() = default;

  constexpr CodePointSetRef(unsigned short bmpSize, const BMPCodePointRange *ptr, unsigned int size)
      : ptr(ptr), bmpSize(bmpSize), size(size) {}

  ArrayRef<BMPCodePointRange> getBMPRanges() const { return {this->ptr, this->bmpSize}; }

  ArrayRef<NonBMPCodePointRange> getNonBMPRanges() const {
    assert(this->bmpSize <= this->size);
    assert((this->size - this->bmpSize) % 2 == 0);
    unsigned int remainSize = (this->size - this->bmpSize) / 2;
    auto *remain = static_cast<const NonBMPCodePointRange *>(this->ptr + this->bmpSize);
    return {remain, remainSize};
  }

  bool contains(const int codePoint) const {
    if (codePoint < 0) {
      return false;
    }
    if (codePoint <= UINT16_MAX) {
      auto ranges = this->getBMPRanges();
      return std::binary_search(ranges.begin(), ranges.end(), static_cast<uint16_t>(codePoint),
                                BMPCodePointRange::SearchComp());
    }
    auto ranges = this->getNonBMPRanges();
    return std::binary_search(ranges.begin(), ranges.end(), codePoint,
                              NonBMPCodePointRange::SearchComp());
  }
};

} // namespace arsh

#endif // ARSH_UNICODE_CODEPOINT_SET_REF_H
