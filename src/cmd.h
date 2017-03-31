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

#define EACH_RedirOP(OP) \
    OP(IN_2_FILE) \
    OP(OUT_2_FILE) \
    OP(OUT_2_FILE_APPEND) \
    OP(ERR_2_FILE) \
    OP(ERR_2_FILE_APPEND) \
    OP(MERGE_ERR_2_OUT_2_FILE) \
    OP(MERGE_ERR_2_OUT_2_FILE_APPEND) \
    OP(MERGE_ERR_2_OUT) \
    OP(MERGE_OUT_2_ERR) \
    OP(HERE_STR)

enum class RedirOP : unsigned char {
#define GEN_ENUM(ENUM) ENUM,
    EACH_RedirOP(GEN_ENUM)
#undef GEN_ENUM
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

#define PERROR(obj, fmt, ...) fprintf(stderr, "ydsh: %s: " fmt ": %s\n", str(obj.getValues()[0]), ## __VA_ARGS__, strerror(errno))
#define ERROR(obj, fmt, ...)  fprintf(stderr, "ydsh: %s: " fmt "\n", str(obj.getValues()[0]), ## __VA_ARGS__)


#endif //YDSH_CMD_H
