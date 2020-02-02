/*
 * Copyright (C) 2020 Nagisa Sekiguchi
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

#ifndef YDSH_MISC_GLOB_HPP
#define YDSH_MISC_GLOB_HPP

#include <dirent.h>

#include <string>
#include <vector>

#include "files.h"

namespace ydsh {
namespace __detail_glob {

/**
 * based on (https://www.codeproject.com/Articles/1088/Wildcard-string-compare-globbing)
 *
 * @tparam Iter
 * @tparam Meta
 * detect meta character `?`, `*`
 * @param name
 * must be null terminated
 * @param iter
 * @param end
 * @return
 * if match pattern, return true.
 */
template <typename Meta, typename Iter>
inline bool matchPattern(const char *name, Iter iter, Iter end) {
    const char *cp = nullptr;
    auto oldIter = end;

    while(*name) {
        if(iter == end) {
            return false;
        } else if(Meta::isZeroOrMore(iter)) {
            break;
        } else if(*name != *iter && !Meta::isAny(iter)) {
            return false;
        }
        ++name;
        ++iter;
    }

    while(*name) {
        if(iter == end) {
            iter = oldIter;
            name = cp++;
        } else if(*name == *iter || Meta::isAny(iter)) {
            ++name;
            ++iter;
        } else if(Meta::isZeroOrMore(iter)) {
            if(++iter == end) {
                return true;
            }
            oldIter = iter;
            cp = name + 1;
        } else {
            iter = oldIter;
            name = cp++;
        }
    }

    for(; iter != end && Meta::isZeroOrMore(iter); iter++);
    return iter == end;
}

} // namespace __detail_glob

// for testing
struct StrMetaChar {
    static bool isAny(const char *iter) {
        return *iter == '?';
    }

    static bool isZeroOrMore(const char *iter) {
        return *iter == '*';
    }
};

inline bool matchWildcard(const char *name, const char *pattern) {
    return __detail_glob::matchPattern<StrMetaChar>(name, pattern, pattern + strlen(pattern));
}

} // namespace ydsh

#endif //YDSH_MISC_GLOB_HPP
