/*
 * Copyright (C) 2015 Nagisa Sekiguchi
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

#ifndef YDSH_UTF8_HPP
#define YDSH_UTF8_HPP

namespace ydsh {
namespace misc {

namespace __detail_utf8 {

template <bool T>
class UTF8Util {
private:
    static_assert(T, "not allowed instantiation");

public:
    static unsigned int getNextPos(unsigned int pos, unsigned char ch) {
        return pos + getByteSize(ch);
    }

    static unsigned int getByteSize(unsigned char ch) {
        if((ch & 0x80) == 0) {
            return 1;
        } else if((ch & 0xE0) == 0xC0) {
            return 2;
        } else if((ch & 0xF0) == 0xE0) {
            return 3;
        } else if((ch & 0xF8) == 0xF0) {
            return 4;
        }
        return 1;
    }
};

} // namespace __detail

typedef __detail_utf8::UTF8Util<true> UTF8Util;


} // namespace misc
} // namespace ydsh

#endif //YDSH_UTF8_HPP
