/*
 * Copyright (C) 2015-2018 Nagisa Sekiguchi
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

#ifndef MISC_LIB_FATAL_H
#define MISC_LIB_FATAL_H

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define BASE_FILENAME__                                                                            \
  (strrchr(__FILE__, '/') != nullptr ? ((const char *)strrchr(__FILE__, '/') + 1) : __FILE__)

/**
 * report error and abort.
 */
#define fatal(fmt, ...)                                                                            \
  do {                                                                                             \
    fprintf(stderr, "%s:%d: [fatal error] " fmt, BASE_FILENAME__, __LINE__, ##__VA_ARGS__);        \
    abort();                                                                                       \
  } while (false)

#define fatal_perror(fmt, ...) fatal(fmt ": %s\n", ##__VA_ARGS__, strerror(errno))

#endif // MISC_LIB_FATAL_H
