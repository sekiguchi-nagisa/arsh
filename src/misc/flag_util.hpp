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

#ifndef YDSH_MISC_FLAG_UTIL_H
#define YDSH_MISC_FLAG_UTIL_H

#include <type_traits>

namespace ydsh {

typedef unsigned char flag8_set_t;
typedef unsigned char flag8_t;

typedef unsigned int flag32_set_t;
typedef unsigned int flag32_t;

template <typename T>
inline void setFlag(T &set, T flag) {
    static_assert(std::is_unsigned<T>::value, "must be unsigned type");

    set |= flag;
}

template <typename T>
inline void unsetFlag(T &set, T flag) {
    static_assert(std::is_unsigned<T>::value, "must be unsigned type");

    set &= ~flag;
}

template <typename T>
inline bool hasFlag(T set, T flag) {
    static_assert(std::is_unsigned<T>::value, "must be unsigned type");

    return (set & flag) == flag;
}

} // namespace ydsh


#endif //YDSH_MISC_FLAG_UTIL_H
