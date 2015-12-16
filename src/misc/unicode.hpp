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

#ifndef YDSH_UNICODE_HPP
#define YDSH_UNICODE_HPP

#include <algorithm>
#include <clocale>

namespace ydsh {
namespace misc {

namespace __detail_unicode {

template <bool T>
struct UnicodeUtil {
    static_assert(T, "not allowed instantiation");

    /**
     * if b is illegal start byte of UTF-8, skip it.
     */
    static unsigned int utf8NextPos(unsigned int pos, unsigned char b) {
        unsigned int size = utf8ByteSize(b);
        return pos + (size > 0 ? size : 1);
    }

    /**
     * if b is illegal start byte of UTF-8, return always 0.
     */
    static unsigned int utf8ByteSize(unsigned char b) {
        static const unsigned char table[256] = {
                1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,

                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

                2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
                2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
                3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0,
        };
        return table[b];
    }

    /**
     * if illegal UTF-8 code, return -1.
     * otherwise, return converted code.
     */
    static int utf8ToCodePoint(const char *const buf, std::size_t bufSize) {
        int codePoint = 0;
        utf8ToCodePoint(buf, bufSize, codePoint);
        return codePoint;
    }

    /**
     * write converted value to codePoint.
     * if illegal UTF-8 code, write -1 and return 0.
     * otherwise, return byte size of UTF-8.
     */
    static unsigned int utf8ToCodePoint(const char *const buf, std::size_t bufSize, int &codePoint) {
        if(bufSize > 0) {
            switch(utf8ByteSize(buf[0])) {
            case 1:
                codePoint = buf[0];
                return 1;
            case 2:
                if(bufSize >= 2) {
                    codePoint = (((unsigned long)(buf[0] & 0x1F)) << 6) |
                            ((unsigned long)(buf[1] & 0x3F));
                    return 2;
                }
                break;
            case 3:
                if(bufSize >= 3) {
                    codePoint = (((unsigned long)(buf[0] & 0x0F)) << 12) |
                            (((unsigned long)(buf[1] & 0x3F)) << 6) |
                            ((unsigned long)(buf[2] & 0x3F));
                    return 3;
                }
                break;
            case 4:
                if(bufSize >= 4) {
                    codePoint = (((unsigned long)(buf[0] & 0x07)) << 18) |
                            (((unsigned long)(buf[1] & 0x3F)) << 12) |
                            (((unsigned long)(buf[2] & 0x3F)) << 6) |
                            ((unsigned long)(buf[3] & 0x3F));
                    return 4;
                }
                break;
            default:
                break;
            }
        }
        codePoint = -1;
        return 0;
    }

    enum AmbiguousCharWidth {
        ONE_WIDTH,
        TWO_WIDTH,
    };

    /**
     * get width of unicode code point.
     * return -1, if control character.
     * return 0, if combining character.
     * return 2, if wide width character.
     * return 1, otherwise.
     *
     * codePoint must be unicode code point.
     */
    static int width(int codePoint, AmbiguousCharWidth ambiguousCharWidth = ONE_WIDTH) {
#include "unicode_width.h"

        if(codePoint == 0) {
            return 0;   // null character width is 0
        }

        if(codePoint >= 32 && codePoint < 127) {    // ascii printable character
            return 1;
        }

        if(codePoint < 32 || (codePoint >= 0x7F && codePoint < 0xA0)) { // control character
            return -1;
        }

        // search zero-width (combining) character
        struct Comparator {
            bool operator()(const Interval &l, int r) const {
                return l.end < r;
            }

            bool operator()(int l, const Interval &r) const {
                return l < r.begin;
            }
        };

#define BINARY_SEARCH(t, v) (std::binary_search(t, t + sizeof(t) / sizeof(Interval), v, Comparator()))

        if(BINARY_SEARCH(zero_width_table, codePoint)) {
            return 0;
        }

        // search ambiguous width character
        if(ambiguousCharWidth == TWO_WIDTH && BINARY_SEARCH(ambiguous_width_table, codePoint)) {
            return 2;
        }

        // search two width character
        if(codePoint < 0x1100) {
            return 1;
        }

        if(BINARY_SEARCH(two_width_table, codePoint)) {
            return 2;
        }

#undef BINARY_SEARCH
        return 1;
    }

    /**
     * if LC_CTYPE is CJK, call width(codePoint, TWO_WIDTH)
     */
    static int localeAwareWidth(int codePoint) {
        static const char *cjk[] = {"ja", "zh", "ko"};

        auto e = ONE_WIDTH;
        const char *ctype = setlocale(LC_CTYPE, nullptr);
        if(ctype != nullptr) {
            for(unsigned int i = 0; i < (sizeof(cjk) / sizeof(const char *)); i++) {
                if(strstr(ctype, cjk[i]) != nullptr) {
                    e = TWO_WIDTH;
                    break;
                }
            }
        }
        return width(codePoint, e);
    }
};

} // namespace __detail_unicode

typedef __detail_unicode::UnicodeUtil<true> UnicodeUtil;


} // namespace misc
} // namespace ydsh

#endif //YDSH_UNICODE_HPP
