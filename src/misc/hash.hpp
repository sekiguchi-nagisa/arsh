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

#ifndef YDSH_MISC_HASH_HPP
#define YDSH_MISC_HASH_HPP

#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace ydsh {

struct CStringComparator {
    bool operator()(const char *x, const char *y) const {
        return strcmp(x, y) == 0;
    }
};

struct CStringHash {
    std::size_t operator()(const char *key) const {
        constexpr uint64_t FNV_offset_basis = 0xcbf29ce484222325;
        constexpr uint64_t FNV_prime = 0x100000001b3;
        std::size_t hash = FNV_offset_basis;
        while(*key != '\0') {
            hash = hash * FNV_prime;
            hash = hash ^ static_cast<uint8_t>(*(key++));
        }
        return hash;
    }
};

template <typename T>
using CStringHashMap = std::unordered_map<const char *, T, CStringHash, CStringComparator>;

using CStringHashSet = std::unordered_set<const char *, CStringHash, CStringComparator>;

} // namespace ydsh


#endif //YDSH_MISC_HASH_HPP
