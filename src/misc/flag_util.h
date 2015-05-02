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

#ifndef MISC_FLAG_UTIL_H_
#define MISC_FLAG_UTIL_H_

typedef unsigned char flag8_set_t;
typedef unsigned char flag8_t;

inline void setFlag(flag8_set_t &set, flag8_t flag) {
    set |= flag;
}

inline void unsetFlag(flag8_set_t &set, flag8_t flag) {
    set &= ~flag;
}

inline bool hasFlag(flag8_set_t set, flag8_t flag) {
    return (set & flag) == flag;
}

#endif /* MISC_FLAG_UTIL_H_ */
