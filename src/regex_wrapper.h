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

inline PCRE compileRegex(const char *pattern, const char * &errorStr, int flag) {
    int errorOffset;
    pcre *re = pcre_compile(pattern,  PCRE_JAVASCRIPT_COMPAT | PCRE_UTF8 | flag, &errorStr, &errorOffset, nullptr);
    return PCRE(re);
}

} // namespace ydsh


#endif //YDSH_REGEX_WRAPPER_H
