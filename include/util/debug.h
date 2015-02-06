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

#ifndef UTIL_DEBUG_H_
#define UTIL_DEBUG_H_

#include <stdio.h>
#include <stdlib.h>

#ifndef NDEBUG
#define DEBUG_ON 1
#else
#define DEBUG_ON 0
#endif

/**
 * for debug logging. see util/debug.cpp
 */
void debug_printf(const char *format, ...);

#define debugp(fmt, ...) \
    do {\
        if(DEBUG_ON) {\
            debug_printf("%s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, ## __VA_ARGS__);\
        }\
    } while(0)

/**
 * report error and abort.
 */
#define fatal(fmt, ...) \
    do {\
        fprintf(stderr, "[abort] %s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, ## __VA_ARGS__);\
        abort();\
    } while(0)


#endif /* UTIL_DEBUG_H_ */
