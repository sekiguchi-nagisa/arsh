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

#ifndef MISC_LIB_CODEPOINT_SET_H
#define MISC_LIB_CODEPOINT_SET_H

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <type_traits>

#include "array_ref.hpp"
#include "buffer.hpp"
#include "noncopyable.h"

BEGIN_MISC_LIB_NAMESPACE_DECL

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
  static_assert(std::is_trivially_destructible_v<BMPCodePointRange>);
  static_assert(std::is_trivially_destructible_v<NonBMPCodePointRange>);

  const BMPCodePointRange *ptr{nullptr};
  unsigned short bmpSize{0};
  unsigned int size{0};

public:
  constexpr CodePointSetRef() = default;

  constexpr CodePointSetRef(unsigned short bmpSize, const BMPCodePointRange *ptr, unsigned int size)
      : ptr(ptr), bmpSize(bmpSize), size(size) {}

  explicit operator bool() const { return this->size > 0; }

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

class CodePointSet {
private:
  BMPCodePointRange *ptr{nullptr};
  unsigned int size{0};
  unsigned short bmpSize{0};
  bool borrowed{true}; // if true, not delete `ptr`

  CodePointSet(unsigned short bmpSize, BMPCodePointRange *ptr, unsigned int size, bool borrowed)
      : ptr(ptr), size(size), bmpSize(bmpSize), borrowed(borrowed) {}

public:
  static CodePointSet take(unsigned short bmpSize, FlexBuffer<BMPCodePointRange> &&buf) {
    unsigned int size = buf.size();
    return {bmpSize, buf.take(), size, false};
  }

  static CodePointSet borrow(unsigned short bmpSize, const BMPCodePointRange *ptr,
                             unsigned int size) {
    return {bmpSize, const_cast<BMPCodePointRange *>(ptr), size, true};
  }

  CodePointSet() = default;

  NON_COPYABLE(CodePointSet);

  CodePointSet(CodePointSet &&o) noexcept
      : ptr(o.ptr), size(o.size), bmpSize(o.bmpSize), borrowed(o.borrowed) {
    o.ptr = nullptr;
    o.borrowed = true;
  }

  ~CodePointSet() {
    if (!this->borrowed) {
      free(this->ptr);
    }
  }

  CodePointSet &operator=(CodePointSet &&o) noexcept {
    if (this->ptr != o.ptr) {
      this->~CodePointSet();
      new (this) CodePointSet(std::move(o));
    }
    return *this;
  }

  bool isBorrowed() const { return this->borrowed; }

  unsigned int getSize() const { return this->size; }

  unsigned short getBMPSize() const { return this->bmpSize; }

  BMPCodePointRange *take() && {
    auto *p = this->ptr;
    this->ptr = nullptr;
    return p;
  }

  CodePointSetRef ref() const { return {this->bmpSize, this->ptr, this->size}; }
};

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_CODEPOINT_SET_H
