/*
 * Copyright (C) 2016 Nagisa Sekiguchi
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

#ifndef YDSH_MISC_TIME_H
#define YDSH_MISC_TIME_H

#include <ctime>
#include <cstring>

#include "config.h"

namespace ydsh {
namespace misc {

#ifdef USE_FIXED_TIME
    constexpr bool useFixedTime = true;
#else
    constexpr bool useFixedTime = false;
#endif

/**
 * environmental variable for specifying time.
 * the specified time is treated as UTC.
 */
constexpr const char *timeSource = "TIME_SOURCE";


inline struct tm *getLocalTime() {
    if(useFixedTime && getenv(timeSource) != nullptr) {
        const char *src = getenv(timeSource);
        struct tm tm;
        memset(&tm, 0, sizeof(struct tm));
        if(strptime(src, "%Y-%m-%dT%H:%M:%SZ", &tm) == nullptr) {   // ex. 2012-1-12T23:45:56Z
            throw std::invalid_argument("broken time source");
        }

        // save current TZ
        char *old = nullptr;
        if(getenv("TZ") != nullptr) {
            old = strdup(getenv("TZ"));
        }
        setenv("TZ", "UTC", 1);
        time_t time = mktime(&tm);
        auto r = gmtime(&time);

        // restore TZ
        if(old != nullptr) {
            setenv("TZ", old, 1);
            free(old);
        } else {
            unsetenv("TZ");
        }

        return r;
    } else {
        time_t timer = time(nullptr);
        return localtime(&timer);
    }
}

} // namespace misc
} // namespace ydsh

#endif //YDSH_MISC_TIME_H
