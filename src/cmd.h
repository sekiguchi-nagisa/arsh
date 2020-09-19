/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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

#ifndef YDSH_CMD_H
#define YDSH_CMD_H

#include "misc/string_ref.hpp"

struct DSState;

namespace ydsh {

class ArrayObject;

/**
 * return exit status.
 * argvObj must be Array_Object
 */
using builtin_command_t = int (*)(DSState &state, ArrayObject &argvObj);

unsigned int getBuiltinCommandSize();

/**
 *
 * @param index
 * must be less than getBuiltinCommandSize().
 * @return
 */
const char *getBuiltinCommandName(unsigned int index);

builtin_command_t lookupBuiltinCommand(const char *commandName);

// common function for field splitting
inline bool isSpace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n';
}

inline bool hasSpace(StringRef ifs) {
    for(auto ch : ifs) {
        if(isSpace(ch)) {
            return true;
        }
    }
    return false;
}

inline bool matchFieldSep(StringRef ifs, char ch) {
    for(auto e : ifs) {
        if(e == ch) {
            return true;
        }
    }
    return false;
}

/**
 * see. http://man7.org/linux/man-pages/man3/exit.3.html
 * @param status
 * @return
 * 0-255
 */
inline int maskExitStatus(int64_t status) {
    return status & 0xFF;
}

} // namespace ydsh

#define PERROR(obj, fmt, ...) fprintf(stderr, "ydsh: %s: " fmt ": %s\n", obj.getValues()[0].asCStr(), ## __VA_ARGS__, strerror(errno))
#define ERROR(obj, fmt, ...)  fprintf(stderr, "ydsh: %s: " fmt "\n", obj.getValues()[0].asCStr(), ## __VA_ARGS__)


#endif //YDSH_CMD_H
