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

#ifndef MISC_NUM_H
#define MISC_NUM_H


/**
 * if success, status is 0.
 * if out of range, status is 1.
 * if cannot convert, status is -1.
 * if found illegal character, status is -2.
 */
inline long convertToInt64(const char *str, int &status, bool skipIllegalChar) {
    // convert to int
    char *end;
    const long value = strtol(str, &end, 10);

    // check error
    if(end == str) {
        return -1;
    }
    if(*end != '\0' && !skipIllegalChar) {
        return -2;
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
inline unsigned long convertToUint64(const char *str, int &status, bool skipIllegalChar) {
    // convert to int
    char *end;
    const long long value = strtoll(str, &end, 10);

    // check error
    if(end == str) {
        return -1;
    }
    if(*end != '\0' && !skipIllegalChar) {
        return -2;
    }
    if((value == LLONG_MIN || value == LLONG_MAX) && errno == ERANGE) {
        status = 1;
        return 0;
    }
    if(value > UINT64_MAX || value < 0) {
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
inline double convertToDouble(const char *str, int &status, bool skipIllegalChar) {
    // convert to double
    char *end;
    const double value = strtod(str, &end);

    // check error
    if(value == 0 && end == str) {
        return -1;
    }
    if(*end != '\0' && !skipIllegalChar) {
        return -2;
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


#endif //MISC_NUM_H
