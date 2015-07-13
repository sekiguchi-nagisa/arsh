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

#ifndef YDSH_TERM_H
#define YDSH_TERM_H

#include <unistd.h>

#include <iostream>

namespace ydsh {
namespace misc {

/**
 * if stream is cout or cerr, and the file descriptor indicates tty, return true
 */
inline bool isTerminal(const std::ostream &stream) {
    // check stdout
    if(&stream == &std::cout && isatty(STDOUT_FILENO) != 0) {
        return true;
    }

    // check stderr
    if(&stream == &std::cerr && isatty(STDERR_FILENO) != 0) {
        return true;
    }

    return false;
}

enum class TermColor : unsigned int {   // ansi color code
    Black   = 30,
    Red     = 31,
    Green   = 32,
    Yellow  = 33,
    Blue    = 34,
    Magenta = 35,
    Cyan    = 36,
    White   = 37,
};

inline std::ostream &operator<<(std::ostream &stream, TermColor color) {
    if(isTerminal(stream)) {
        stream << "\033[" << static_cast<unsigned int>(color) << "m";
    }
    return stream;
}

inline std::ostream &reset(std::ostream &stream) {
    if(isTerminal(stream)) {
        stream << "\033[0m";
    }
    return stream;
}

} // namespace misc
} // namespace ydsh

#endif //YDSH_TERM_H
