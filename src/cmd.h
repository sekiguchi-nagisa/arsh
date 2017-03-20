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

#ifndef YDSH__PROC_H
#define YDSH__PROC_H

#include <cstring>
#include <cassert>

struct DSState;

namespace ydsh {

class Array_Object;

constexpr unsigned int READ_PIPE = 0;
constexpr unsigned int WRITE_PIPE = 1;

#define EACH_RedirectOP(OP) \
    OP(IN_2_FILE, "<") \
    OP(OUT_2_FILE, "1>") \
    OP(OUT_2_FILE_APPEND, "1>>") \
    OP(ERR_2_FILE, "2>") \
    OP(ERR_2_FILE_APPEND, "2>>") \
    OP(MERGE_ERR_2_OUT_2_FILE, "&>") \
    OP(MERGE_ERR_2_OUT_2_FILE_APPEND, "&>>") \
    OP(MERGE_ERR_2_OUT, "2>&1") \
    OP(MERGE_OUT_2_ERR, "1>&2")

enum RedirectOP : unsigned char {
#define GEN_ENUM(ENUM, STR) ENUM,
    EACH_RedirectOP(GEN_ENUM)
#undef GEN_ENUM
    DUMMY,
};

/**
 * return exit status.
 * argvObj must be Array_Object
 */
typedef int (*builtin_command_t)(DSState &state, Array_Object &argvObj);

unsigned int getBuiltinCommandSize();

/**
 *
 * @param index
 * must be less than getBuiltinCommandSize().
 * @return
 */
const char *getBuiltinCommandName(unsigned int index);

builtin_command_t lookupBuiltinCommand(const char *commandName);

} // namespace ydsh

#define PERROR0(obj)          fprintf(stderr, "-ydsh: %s: %s\n", str(obj.getValues()[0]), strerror(errno))
#define PERROR(obj, fmt, ...) fprintf(stderr, "-ydsh: %s: " fmt ": %s\n", str(obj.getValues()[0]), ## __VA_ARGS__, strerror(errno))
#define ERROR(obj, fmt, ...)  fprintf(stderr, "-ydsh: %s: " fmt "\n", str(obj.getValues()[0]), ## __VA_ARGS__)


#endif //YDSH_PROC_H
