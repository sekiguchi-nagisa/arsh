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

} // namespace ydsh


#endif //YDSH_MISC_NUM_H
