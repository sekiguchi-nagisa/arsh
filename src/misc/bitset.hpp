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

#ifndef MISC_LIB_BITSET_HPP
#define MISC_LIB_BITSET_HPP

#include <bitset>

#include "misc/detect.hpp"
#include "misc/inlined_array.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

class BitSet {
private:
  static constexpr size_t N = 64;
  using base = std::bitset<N>;

  size_t nbits;
  InlinedArray<base, 1> sets; // values(std::max<size_t>(nbits, 1) - 1 / N + 1)

public:
  explicit BitSet(size_t nbits) : nbits(nbits), sets((std::max<size_t>(nbits, 1) - 1) / N + 1) {}

  BitSet(const BitSet &o) = default;

  bool test(size_t pos) const { return this->sets[pos / N].test(pos % N); }

  BitSet &set() {
    size_t size = this->sets.size();
    for (size_t i = 0; i < size - 1; i++) {
      this->sets[i].set();
    }
    size_t remain = this->nbits - (size - 1) * N;
    for (size_t i = 0; i < remain; i++) {
      this->sets[size - 1].set(i);
    }
    return *this;
  }

  BitSet &set(size_t pos) {
    assert(pos < this->size());
    this->sets[pos / N].set(pos % N);
    return *this;
  }

  BitSet &reset(size_t pos) {
    assert(pos < this->size());
    this->sets[pos / N].reset(pos % N);
    return *this;
  }

  void clear() {
    for (size_t i = 0; i < this->sets.size(); i++) {
      this->sets[i].reset();
    }
  }

  size_t count() const {
    size_t sum = 0;
    for (size_t i = 0; i < this->sets.size(); i++) {
      sum += this->sets[i].count();
    }
    return sum;
  }

  size_t size() const { return this->nbits; }

  size_t countTailZero() const {
    const size_t maskBits = this->sets.size() * N - this->size();
    size_t count = 0;
    for (size_t i = 0; i < this->sets.size(); i++) {
      auto set = this->sets[i];
      if (i == this->sets.size() - 1 && maskBits != 0) {
        set.set(N - maskBits);
      }
      if (uint64_t raw = set.to_ullong()) {
        count += __builtin_ctzll(raw);
        break;
      }
      count += N;
    }
    return count;
  }

  size_t nextSetBit(size_t fromIndex) const {
    if (!fromIndex) {
      return this->countTailZero(); // fast-path
    }
    size_t retIndex = fromIndex;
    for (; retIndex < this->size(); retIndex++) {
      if (this->test(retIndex)) {
        break;
      }
    }
    return retIndex;
  }

  template <typename Iter>
  static constexpr bool iter_requirement_v =
      std::is_same_v<bool, std::invoke_result_t<Iter, size_t>>;

  template <typename Func, enable_when<iter_requirement_v<Func>> = nullptr>
  void iterateSetBitFrom(size_t fromIndex, Func func) const {
    for (; fromIndex < this->size(); fromIndex++) {
      if (this->test(fromIndex) && !func(fromIndex)) {
        break;
      }
    }
  }

  template <typename Func, enable_when<iter_requirement_v<Func>> = nullptr>
  void iterateSetBit(Func func) const {
    return this->iterateSetBitFrom(this->countTailZero(), func);
  }

  /**
   *
   * @param n 0-based index
   * @return
   */
  size_t getNthSetBitIndex(size_t n) const {
    size_t index = 0;
    size_t count = 0;
    this->iterateSetBit([&index, &n, &count](size_t setIndex) {
      if (count <= n) {
        index = setIndex;
        count++;
        return true;
      }
      return false;
    });
    return index;
  }
};

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB__BITSET_HPP
