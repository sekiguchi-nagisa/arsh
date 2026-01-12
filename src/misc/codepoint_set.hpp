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

class PackedNonBMPCodePointRange : public BMPCodePointRange {
public:
  /**
   *
   * @param first
   * @param size must be less than 2048 (1<<11)
   */
  constexpr PackedNonBMPCodePointRange(int first, unsigned int size)
      : BMPCodePointRange(static_cast<uint32_t>(first) | (size << 21)) {}

  static constexpr auto pack(int first, int last) {
    if (first <= last && (last - first) < (1 << 11)) {
      return PackedNonBMPCodePointRange(first, static_cast<unsigned int>(last - first));
    }
    throw 23;
  }

  int firstNonBMP() const { return static_cast<int>(this->underlying() & 0x1FFFFF); }

  int lastNonBMP() const {
    return this->firstNonBMP() + static_cast<int>(this->underlying() >> 21);
  }

  struct SearchComp {
    bool operator()(const PackedNonBMPCodePointRange &x, int y) const { return x.lastNonBMP() < y; }

    bool operator()(int x, const PackedNonBMPCodePointRange &y) const {
      return x < y.firstNonBMP();
    }
  };
};

class CodePointSetRef {
private:
  static_assert(sizeof(BMPCodePointRange) == sizeof(uint32_t));
  static_assert(sizeof(NonBMPCodePointRange) == sizeof(uint32_t) * 2);
  static_assert(sizeof(PackedNonBMPCodePointRange) == sizeof(uint32_t));
  static_assert(std::is_trivially_destructible_v<BMPCodePointRange>);
  static_assert(std::is_trivially_destructible_v<NonBMPCodePointRange>);
  static_assert(std::is_trivially_destructible_v<PackedNonBMPCodePointRange>);

  const BMPCodePointRange *ptr{nullptr};
  unsigned short bmpSize{0};
  unsigned short packedNonBmpSize{0};
  unsigned int size{0};

public:
  constexpr CodePointSetRef() = default;

  constexpr CodePointSetRef(unsigned short bmpSize, const BMPCodePointRange *ptr, unsigned int size)
      : ptr(ptr), bmpSize(bmpSize), size(size) {}

  constexpr CodePointSetRef(unsigned short bmpSize, unsigned short packedSize,
                            const BMPCodePointRange *ptr, unsigned int size)
      : ptr(ptr), bmpSize(bmpSize), packedNonBmpSize(packedSize), size(size) {}

  explicit operator bool() const { return this->size > 0; }

  const BMPCodePointRange *getPtr() const { return this->ptr; }

  unsigned short getBMPSize() const { return this->bmpSize; }

  unsigned short getPackedNonBMPSize() const { return this->packedNonBmpSize; }

  unsigned int getSize() const { return this->size; }

  ArrayRef<BMPCodePointRange> getBMPRanges() const { return {this->ptr, this->bmpSize}; }

  ArrayRef<PackedNonBMPCodePointRange> getPackedNonBMPRanges() const {
    auto *remain = static_cast<const PackedNonBMPCodePointRange *>(this->ptr + this->bmpSize);
    return {remain, this->packedNonBmpSize};
  }

  ArrayRef<NonBMPCodePointRange> getNonBMPRanges() const {
    assert(this->bmpSize + this->packedNonBmpSize <= this->size);
    assert((this->size - this->bmpSize - this->packedNonBmpSize) % 2 == 0);
    unsigned int remainSize = (this->size - this->bmpSize - this->packedNonBmpSize) / 2;
    auto *remain = static_cast<const NonBMPCodePointRange *>(this->ptr + this->bmpSize +
                                                             this->packedNonBmpSize);
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
    if (this->packedNonBmpSize) {
      auto packedRanges = this->getPackedNonBMPRanges();
      if (std::binary_search(packedRanges.begin(), packedRanges.end(), codePoint,
                             PackedNonBMPCodePointRange::SearchComp())) {
        return true;
      }
    }
    auto ranges = this->getNonBMPRanges();
    return std::binary_search(ranges.begin(), ranges.end(), codePoint,
                              NonBMPCodePointRange::SearchComp());
  }
};

class CodePointSet {
private:
  BMPCodePointRange *ptr{nullptr};
  unsigned short bmpSize{0};
  unsigned short packedNonBmpSize{0};
  unsigned int sizeWithMeta{0}; // | 1bit (1:owned, 0:borrowed) | 31bit (set size, up to 21bit) |

  CodePointSet(unsigned short bmpSize, unsigned short packedSize, BMPCodePointRange *ptr,
               unsigned int size, bool borrowed)
      : ptr(ptr), bmpSize(bmpSize), packedNonBmpSize(packedSize),
        sizeWithMeta(size | ((borrowed ? 0 : 1) << 31)) {}

public:
  static CodePointSet take(unsigned short bmpSize, unsigned short packedSize,
                           FlexBuffer<BMPCodePointRange> &&buf) {
    unsigned int size = buf.size();
    return {bmpSize, packedSize, buf.take(), size, false};
  }

  static CodePointSet take(unsigned short bmpSize, FlexBuffer<BMPCodePointRange> &&buf) {
    return take(bmpSize, 0, std::move(buf));
  }

  static CodePointSet borrow(unsigned short bmpSize, unsigned short packedSize,
                             const BMPCodePointRange *ptr, unsigned int size) {
    return {bmpSize, packedSize, const_cast<BMPCodePointRange *>(ptr), size, true};
  }

  static CodePointSet borrow(unsigned short bmpSize, const BMPCodePointRange *ptr,
                             unsigned int size) {
    return borrow(bmpSize, 0, ptr, size);
  }

  static CodePointSet borrow(const CodePointSetRef ref) {
    return borrow(ref.getBMPSize(), ref.getPackedNonBMPSize(), ref.getPtr(), ref.getSize());
  }

  CodePointSet() = default;

  NON_COPYABLE(CodePointSet);

  CodePointSet(CodePointSet &&o) noexcept
      : ptr(o.ptr), bmpSize(o.bmpSize), packedNonBmpSize(o.packedNonBmpSize),
        sizeWithMeta(o.sizeWithMeta) {
    o.ptr = nullptr;
    o.sizeWithMeta = o.getSize(); // clear first bit
  }

  ~CodePointSet() {
    if (this->isOwned()) {
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

  explicit operator bool() const { return this->getSize() > 0; }

  bool isOwned() const { return (this->sizeWithMeta >> 31) == 1; }

  unsigned int getSize() const { return this->sizeWithMeta & ((1u << 31) - 1); }

  unsigned short getBMPSize() const { return this->bmpSize; }

  unsigned short getPackedNonBMPSize() const { return this->packedNonBmpSize; }

  BMPCodePointRange *take() && {
    auto *p = this->ptr;
    this->ptr = nullptr;
    return p;
  }

  CodePointSetRef ref() const {
    return {this->bmpSize, this->packedNonBmpSize, this->ptr, this->getSize()};
  }
};

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_CODEPOINT_SET_H
