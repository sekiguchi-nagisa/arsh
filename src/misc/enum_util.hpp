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

#ifndef YDSH_MISC_ENUM_UTIL_HPP
#define YDSH_MISC_ENUM_UTIL_HPP

#include "detect.hpp"

namespace ydsh {

template <typename T>
struct allow_enum_bitop : std::false_type {};

namespace __detail {

template <typename T>
constexpr auto allowBitop = std::is_enum<T>::value && allow_enum_bitop<T>::value;

} // namespace __detail

template <typename T, enable_when<__detail::allowBitop<T>> = nullptr>
constexpr T operator|(T x, T y) {
    auto x1 = static_cast<std::underlying_type_t<T>>(x);
    auto y1 = static_cast<std::underlying_type_t<T>>(y);
    return static_cast<T>(x1 | y1);
}

template <typename T, enable_when<__detail::allowBitop<T>> = nullptr>
constexpr T operator&(T x, T y) {
    auto x1 = static_cast<std::underlying_type_t<T>>(x);
    auto y1 = static_cast<std::underlying_type_t<T>>(y);
    return static_cast<T>(x1 & y1);
}

template <typename T, enable_when<__detail::allowBitop<T>> = nullptr>
constexpr T operator^(T x, T y) {
    auto x1 = static_cast<std::underlying_type_t<T>>(x);
    auto y1 = static_cast<std::underlying_type_t<T>>(y);
    return static_cast<T>(x1 ^ y1);
}

template <typename T, enable_when<__detail::allowBitop<T>> = nullptr>
constexpr T operator~(T x) {
    auto x1 = static_cast<std::underlying_type_t<T>>(x);
    return static_cast<T>(~x1);
}

template <typename T, enable_when<__detail::allowBitop<T>> = nullptr>
constexpr bool empty(T x) {
    auto x1 = static_cast<std::underlying_type_t<T>>(x);
    return x1 == 0;
}

template <typename T, enable_when<__detail::allowBitop<T>> = nullptr>
constexpr T &operator|=(T &x, T y) {
    x = x | y;
    return x;
}

template <typename T, enable_when<__detail::allowBitop<T>> = nullptr>
constexpr T &operator&=(T &x, T y) {
    x = x & y;
    return x;
}

template <typename T, enable_when<__detail::allowBitop<T>> = nullptr>
constexpr T &operator^=(T &x, T y) {
    x = x ^ y;
    return x;
}

} // namespace ydsh

#endif //YDSH_MISC_ENUM_UTIL_HPP
