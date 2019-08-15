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

#ifndef YDSH_MISC_NUM_H
#define YDSH_MISC_NUM_H

#include <climits>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ydsh {

/**
 * if success, status is 0.
 * if out of range, status is 1.
 * if cannot convert, status is -1.
 * if found illegal character, status is -2.
 */
inline long convertToInt64(const char *str, int &status, int base = 0, bool skipIllegalChar = false) {
    errno = 0;

    if(base == 0 && strstr(str, "0o") == str) {
        base = 8;
        str += 2;   // skip '0o'
    }

    // convert to int
    char *end;
    const long value = strtol(str, &end, base);

    // check error
    if(end == str) {
        status = -1;
        return 0;
    }
    if(*end != '\0' && !skipIllegalChar) {
        status = -2;
        return 0;
    }
    if((value == LONG_MIN || value == LONG_MAX) && errno == ERANGE) {
        status = 1;
        return 0;
    }
    status = 0;
    return value;
}

/**
 * if success, status is 0.
 * if out of range, status is 1.
 * if cannot convert, status is -1.
 * if found illegal character, status is -2.
 */
inline unsigned long convertToUint64(const char *str, int &status, int base = 0, bool skipIllegalChar = false) {
    errno = 0;

    if(base == 0 && strstr(str, "0o") == str) {
        base = 8;
        str += 2;   // skip '0o'
    }

    // convert to int
    char *end;
    const unsigned long long value = strtoull(str, &end, base);

    // check error
    if(end == str) {
        status = -1;
        return 0;
    }
    if(*end != '\0' && !skipIllegalChar) {
        status = -2;
        return 0;
    }
    if(value == ULLONG_MAX && errno == ERANGE) {
        status = 1;
        return 0;
    }
    status = 0;
    return (unsigned long) value;
}

/**
 * if success, status is 0.
 * if out of range, status is 1.
 * if cannot convert, status is -1.
 * if found illegal character, status is -2.
 */
inline double convertToDouble(const char *str, int &status, bool skipIllegalChar = false) {
    errno = 0;

    // convert to double
    char *end;
    const double value = strtod(str, &end);

    // check error
    if(value == 0 && end == str) {
        status = -1;
        return 0;
    }
    if(*end != '\0' && !skipIllegalChar) {
        status = -2;
        return 0;
    }
    if(value == 0 && errno == ERANGE) {
        status = 1;
        return 0;
    }
    if((value == HUGE_VAL || value == -HUGE_VAL) && errno == ERANGE) {
        status = 1;
        return 0;
    }
    status = 0;
    return value;
}

inline bool isDecimal(char ch) {
    return ch >= '0' && ch <= '9';
}

inline bool isOctal(char ch) {
    return ch >= '0' && ch < '8';
}

inline bool isHex(char ch) {
    return (ch >= '0' && ch <= '9') ||
           (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f');
}

/**
 * convert hex character to number
 * @param ch
 * @return
 */
inline int hexToNum(char ch) {
    if(ch >= '0' && ch <= '9') {
        return ch - '0';
    } else if(ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    } else if(ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return 0;
}

} // namespace ydsh


#endif //YDSH_MISC_NUM_H
