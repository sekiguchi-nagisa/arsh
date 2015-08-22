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

#ifndef YDSH_SYSTEM_H
#define YDSH_SYSTEM_H

#include <unistd.h>

#include <string>

namespace ydsh {
namespace core {

/**
 * path is starts with tilde.
 */
std::string expandTilde(const char *path);

/**
 * after fork, reset signal setting in child process.
 */
pid_t xfork(void);


} // namespace core
} // namespace ydsh


#endif //YDSH_SYSTEM_H
