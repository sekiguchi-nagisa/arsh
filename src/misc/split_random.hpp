/*
 * Copyright (C) 2023 Nagisa Sekiguchi
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

#ifndef MISC_LIB_SPLIT_RANDOM_HPP
#define MISC_LIB_SPLIT_RANDOM_HPP

#include <cstdint>

BEGIN_MISC_LIB_NAMESPACE_DECL

/**
 * splittable random number generator implementation
 *  based on [LXM: better splittable pseudorandom number generators (and almost as fast)]
 *  (https://doi.org/10.1145/3485525)
 */

class L64X128MixRNG {
private:
  static constexpr uint64_t M = 0xd1342543de82ef95L;

  uint64_t a; // must be odd (not modified during next() or split())

  uint64_t s;

  // (x0 and x1 are never both zero)
  uint64_t x0;
  uint64_t x1;

public:
  explicit L64X128MixRNG(uint64_t seed) : L64X128MixRNG(seed, 42, 42, 42) {}

  L64X128MixRNG(uint64_t a, uint64_t s, uint64_t x0, uint64_t x1)
      : a(a | 1), s(s), x0(x0), x1(x1) {}

  uint64_t next() {
    uint64_t ss = this->s;
    uint64_t q0 = this->x0;
    uint64_t q1 = this->x1;

    uint64_t z = ss + q0;
    z = (z ^ (z >> 32)) * 0xdaba0b6eb09322e3L;
    z = (z ^ (z >> 32)) * 0xdaba0b6eb09322e3L;
    z = (z ^ (z >> 32));

    this->s = M * ss + this->a;

    q1 ^= q0;
    q0 = rotateLeft(q0, 24);
    q0 = q0 ^ q1 ^ (q1 << 16);
    q1 = rotateLeft(q1, 37);
    this->x0 = q0;
    this->x1 = q1;

    return z;
  }

  int64_t nextInt64() {
    uint64_t v = this->next();
    v &= INT64_MAX; // 0 to INT64_MAX
    return static_cast<int64_t>(v);
  }

  L64X128MixRNG split() {
    uint64_t aa = this->next();
    uint64_t ss = this->next();
    uint64_t xx0;
    do {
      xx0 = this->next();
    } while (xx0 == 0);
    uint64_t xx1 = this->next();
    return {aa, ss, xx0, xx1};
  }

private:
  static uint64_t rotateLeft(uint64_t x, unsigned int k) { return (x << k) | (x >> (64 - k)); }
};

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_SPLIT_RANDOM_HPP
