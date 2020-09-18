/*
 * Copyright (C) 2017 Nagisa Sekiguchi
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

#ifndef YDSH_REGEX_WRAPPER_H
#define YDSH_REGEX_WRAPPER_H

#include <memory>

#include <pcre.h>

#include "misc/string_ref.hpp"

namespace ydsh {

struct PCREDeleter {
    void operator()(pcre *ptr) const {
        pcre_free(ptr);
    }
};

using PCRE = std::unique_ptr<pcre, PCREDeleter>;

/**
 * convert flag character to regex flag (option)
 * @param ch
 * @return
 * if specified unsupported flag character, return 0
 */
inline int toRegexFlag(char ch) {
    switch(ch) {
    case 'i':
        return PCRE_CASELESS;
    case 'm':
        return PCRE_MULTILINE;
    case 's':
        return PCRE_DOTALL;
    default:
        return 0;
    }
}

inline PCRE compileRegex(StringRef pattern, StringRef flag, std::string &errorStr) {
    if(pattern.hasNullChar()) {
        errorStr = "regex pattern contains null characters";
        return nullptr;
    }

    int flagValue = 0;
    for(auto &e : flag) {
        int r = toRegexFlag(e);
        if(!r) {
            errorStr = "unsupported regex flag: `";
            errorStr += e;
            errorStr += "'";
            return nullptr;
        }
        setFlag(flagValue, r);
    }

    const char *error;
    int errorOffset;
    flagValue |= PCRE_JAVASCRIPT_COMPAT | PCRE_UTF8;
    pcre *re = pcre_compile(pattern.data(), flagValue, &error, &errorOffset, nullptr);
    if(!re) {
        errorStr = error;
    }
    return PCRE(re);
}

} // namespace ydsh


#endif //YDSH_REGEX_WRAPPER_H
