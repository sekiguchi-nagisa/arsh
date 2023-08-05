/*
 * Copyright (C) 2019 Nagisa Sekiguchi
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

#ifndef MISC_LIB_ENUM_UTIL_HPP
#define MISC_LIB_ENUM_UTIL_HPP

#include "detect.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

template <typename T>
struct allow_enum_bitop : std::false_type {};

namespace detail {

template <typename T>
constexpr auto allowBitop = std::is_enum_v<T> && allow_enum_bitop<T>::value;

} // namespace detail

template <typename T, enable_when<std::is_enum_v<T>> = nullptr>
constexpr std::underlying_type_t<T> toUnderlying(T v) {
  return static_cast<std::underlying_type_t<T>>(v);
}

template <typename T, enable_when<detail::allowBitop<T>> = nullptr>
constexpr T operator|(T x, T y) {
  auto x1 = toUnderlying(x);
  auto y1 = toUnderlying(y);
  return static_cast<T>(x1 | y1);
}

template <typename T, enable_when<detail::allowBitop<T>> = nullptr>
constexpr T operator&(T x, T y) {
  auto x1 = toUnderlying(x);
  auto y1 = toUnderlying(y);
  return static_cast<T>(x1 & y1);
}

template <typename T, enable_when<detail::allowBitop<T>> = nullptr>
constexpr T operator^(T x, T y) {
  auto x1 = toUnderlying(x);
  auto y1 = toUnderlying(y);
  return static_cast<T>(x1 ^ y1);
}

template <typename T, enable_when<detail::allowBitop<T>> = nullptr>
constexpr T operator~(T x) {
  auto x1 = toUnderlying(x);
  return static_cast<T>(~x1);
}

template <typename T, enable_when<detail::allowBitop<T>> = nullptr>
constexpr bool empty(T x) {
  auto x1 = toUnderlying(x);
  return x1 == 0;
}

template <typename T, enable_when<detail::allowBitop<T>> = nullptr>
constexpr T &operator|=(T &x, T y) {
  x = x | y;
  return x;
}

template <typename T, enable_when<detail::allowBitop<T>> = nullptr>
constexpr T &operator&=(T &x, T y) {
  x = x & y;
  return x;
}

template <typename T, enable_when<detail::allowBitop<T>> = nullptr>
constexpr T &operator^=(T &x, T y) {
  x = x ^ y;
  return x;
}

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_ENUM_UTIL_HPP
