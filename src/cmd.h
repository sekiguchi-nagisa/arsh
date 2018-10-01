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

struct DSState;

namespace ydsh {

class Array_Object;

/**
 * return exit status.
 * argvObj must be Array_Object
 */
using builtin_command_t = int (*)(DSState &state, Array_Object &argvObj);

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
inline bool isSpace(int ch) {
    return ch == ' ' || ch == '\t' || ch == '\n';
}

inline bool hasSpace(unsigned int size, const char *ifs) {
    for(unsigned int i = 0; i < size; i++) {
        if(isSpace(ifs[i])) {
            return true;
        }
    }
    return false;
}

inline bool isFieldSep(unsigned int size, const char *ifs, int ch) {
    for(unsigned int i = 0; i < size; i++) {
        if(ifs[i] == ch) {
            return true;
        }
    }
    return false;
}

} // namespace ydsh

#define PERROR(obj, fmt, ...) fprintf(stderr, "ydsh: %s: " fmt ": %s\n", str(obj.getValues()[0]), ## __VA_ARGS__, strerror(errno))
#define ERROR(obj, fmt, ...)  fprintf(stderr, "ydsh: %s: " fmt "\n", str(obj.getValues()[0]), ## __VA_ARGS__)


#endif //YDSH_CMD_H
