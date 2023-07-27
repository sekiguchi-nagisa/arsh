/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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

#ifndef MISC_LIB_HASH_HPP
#define MISC_LIB_HASH_HPP

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

BEGIN_MISC_LIB_NAMESPACE_DECL

struct FNVHash32 {
  using type = uint32_t;

  static constexpr uint32_t FNV_offset_basis = 0x811c9dc5;
  static constexpr uint32_t FNV_prime = 0x01000193;

  static void update(uint32_t &hash, uint8_t value) {
    hash *= FNV_prime;
    hash ^= value;
  }

  static uint32_t compute(const char *begin, const char *end) {
    uint32_t hash = FNV_offset_basis;
    for (; begin != end; ++begin) {
      update(hash, *begin);
    }
    return hash;
  }
};

struct FNVHash64 {
  using type = uint64_t;

  static constexpr uint64_t FNV_offset_basis = 0xcbf29ce484222325;
  static constexpr uint64_t FNV_prime = 0x100000001b3;

  static void update(uint64_t &hash, uint8_t value) {
    hash *= FNV_prime;
    hash ^= value;
  }

  static uint64_t compute(const char *begin, const char *end) {
    uint64_t hash = FNV_offset_basis;
    for (; begin != end; ++begin) {
      update(hash, *begin);
    }
    return hash;
  }
};

using FNVHash = std::conditional_t<
    sizeof(std::size_t) == sizeof(uint64_t), FNVHash64,
    std::conditional_t<sizeof(std::size_t) == sizeof(uint32_t), FNVHash32, void>>;

struct CStringComparator {
  bool operator()(const char *x, const char *y) const { return strcmp(x, y) == 0; }
};

struct CStringHash {
  std::size_t operator()(const char *key) const {
    FNVHash::type hash = FNVHash::FNV_offset_basis;
    while (*key != '\0') {
      FNVHash::update(hash, static_cast<uint8_t>(*key++));
    }
    return hash;
  }
};

template <typename T>
using CStringHashMap = std::unordered_map<const char *, T, CStringHash, CStringComparator>;

using CStringHashSet = std::unordered_set<const char *, CStringHash, CStringComparator>;

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_HASH_HPP
