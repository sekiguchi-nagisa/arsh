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

#include "misc/inlined_array.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

class BitSet {
private:
  static constexpr size_t N = 64;
  using base = std::bitset<N>;

  size_t nbits;
  InlinedArray<base, 1> sets; // values(std::max<size_t>(nbits, 1) - 1 / N + 1)

public:
  explicit BitSet(size_t nbits) : nbits(nbits), sets(std::max<size_t>(nbits, 1) - 1 / N + 1) {}

  BitSet(const BitSet &o) = default;

  bool test(size_t pos) const { return this->sets[pos / N].test(pos % N); }

  BitSet &set() {
    size_t size = this->sets.size();
    for (size_t i = 0; i < size - 1; i++) {
      this->sets[i].set();
    }
    size_t remain = this->nbits % N;
    for (size_t i = 0; i < remain; i++) {
      this->sets[size - 1].set(i);
    }
    return *this;
  }

  BitSet &set(size_t pos) {
    this->sets[pos / N].set(pos % N);
    return *this;
  }

  BitSet &reset(size_t pos) {
    this->sets[pos / N].reset(pos % N);
    return *this;
  }

  size_t count() const {
    size_t sum = 0;
    for (size_t i = 0; i < this->sets.size(); i++) {
      sum += this->sets[i].count();
    }
    return sum;
  }

  size_t size() const { return this->nbits; }
};

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB__BITSET_HPP
