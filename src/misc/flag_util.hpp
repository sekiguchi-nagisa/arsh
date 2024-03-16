/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#ifndef MISC_LIB_FLAG_UTIL_H
#define MISC_LIB_FLAG_UTIL_H

#include <cstdint>

#include "enum_util.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

using flag8_set_t = uint8_t;
using flag8_t = uint8_t;

using flag16_set_t = uint16_t;
using flag16_t = uint16_t;

using flag32_set_t = uint32_t;
using flag32_t = uint32_t;

template <typename T>
inline constexpr void setFlag(T &set, T flag) {
  set |= flag;
}

template <typename T>
inline constexpr void unsetFlag(T &set, T flag) {
  set &= ~flag;
}

template <typename T>
inline constexpr bool hasFlag(T set, T flag) {
  return (set & flag) == flag;
}

template <typename T, enable_when<std::is_unsigned_v<T> && sizeof(T) <= sizeof(uint64_t)> = nullptr>
class StaticBitSet {
private:
  T value{0};

public:
  static constexpr size_t BIT_SIZE = sizeof(T) * 8;

  static constexpr bool checkRange(uint8_t v) { return v < BIT_SIZE; }

  void add(uint8_t v) {
    const auto f = static_cast<T>(1) << v;
    setFlag(this->value, f);
  }

  void del(uint8_t v) {
    const auto f = static_cast<T>(1) << v;
    unsetFlag(this->value, f);
  }

  bool has(uint8_t v) const {
    const auto f = static_cast<T>(1) << v;
    return hasFlag(this->value, f);
  }

  bool empty() const { return this->value == 0; }

  void clear() { this->value = 0; }
};

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_FLAG_UTIL_H
