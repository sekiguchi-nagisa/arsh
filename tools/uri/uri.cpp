/*
 * Copyright (C) 2018 Nagisa Sekiguchi
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

#include "uri.h"

#include <misc/num.h>

namespace ydsh {
namespace uri {

std::string URI::toString() const {
    std::string value;
    return value;
}

static bool isEscaped(char ch) {
    switch(ch) {
    case '-':
    case '.':
    case '_':
    case '~':
        return false;
    default:
        if((ch >= '0'&& ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
            return false;
        }
    }
    return true;
}

std::string URI::encode(const char *begin, const char *end) {
    std::string value;
    for(; begin != end; ++begin) {
        char ch = *begin;
        if(isEscaped(ch)) {
            value += '%';
            char buf[16];
            snprintf(buf, 16, "%02X", ch);
            value += buf;
        } else {
            value += ch;
        }
    }
    return value;
}

std::string URI::decode(const char *begin, const char *end) {
    std::string value;
    for(; begin != end; ++begin) {
        char ch = *begin;
        if(ch != '%') {
            value += ch;
            continue;
        }

        if(ch == '%' && begin + 2 < end && isHex(*(begin + 1)) && isHex(*(begin + 2))) {
            char ch1 = *++begin;
            char ch2 = *++begin;
            int v = 16 * hexToNum(ch1) + hexToNum(ch2);
            value += (char) v;
        } else {
            value += ch;
        }
    }
    return value;
}

} // namespace uri
} // namespace ydsh