/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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
 */
typedef int (*builtin_command_t)(DSState &state, const int argc, char *const *argv);

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


#endif //YDSH_PROC_H
