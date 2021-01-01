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

#include "enum_util.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

using flag8_set_t = unsigned char;
using flag8_t = unsigned char;

using flag16_set_t = unsigned short;
using flag16_t = unsigned short;

using flag32_set_t = unsigned int;
using flag32_t = unsigned int;

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

END_MISC_LIB_NAMESPACE_DECL


#endif //MISC_LIB_FLAG_UTIL_H
