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

#ifndef YDSH_SYMBOL_H
#define YDSH_SYMBOL_H

#include <cstdint>
#include <cstddef>

#include <config.h>
#include "misc/flag_util.hpp"

namespace ydsh {

/**
 * built-in symbol(builtin variable, magic method) definition
 */

// =====  builtin variable  =====

// io
constexpr const char *VAR_STDIN = "STDIN";
constexpr const char *VAR_STDOUT = "STDOUT";
constexpr const char *VAR_STDERR = "STDERR";

// shell builtin
constexpr const char *VAR_EIDT_HOOK = "EDIT_HOOK";
constexpr const char *VAR_CMD_FALLBACK = "CMD_FALLBACK";
constexpr const char *CMD_FALLBACK_HANDLER = "_cmd_fallback_handler";

constexpr const char *VAR_HISTFILE = "HISTFILE";
constexpr const char *VAR_HISTSIZE = "HISTSIZE";
constexpr const char *VAR_HISTFILESIZE = "HISTFILESIZE";

constexpr const char *VAR_SIG_DFL = "SIG_DFL";
constexpr const char *VAR_SIG_IGN = "SIG_IGN";

constexpr const char *VAR_TERM_HOOK = "TERM_HOOK";
constexpr const char *VAR_COMP_HOOK = "COMP_HOOK";

// =====  magic method  =====

// constructor
constexpr const char *OP_INIT = "";

// unary op definition
constexpr const char *OP_PLUS = "%plus";     // +
constexpr const char *OP_MINUS = "%minus";    // -
constexpr const char *OP_NOT = "%not";      // not

// binary op definition
constexpr const char *OP_ADD = "%add";      // +
constexpr const char *OP_SUB = "%sub";      // -
constexpr const char *OP_MUL = "%mul";      // *
constexpr const char *OP_DIV = "%div";      // /
constexpr const char *OP_MOD = "%mod";      // %

constexpr const char *OP_EQ = "%eq";       // ==
constexpr const char *OP_NE = "%ne";       // !=

constexpr const char *OP_LT = "%lt";       // <
constexpr const char *OP_GT = "%gt";       // >
constexpr const char *OP_LE = "%le";       // <=
constexpr const char *OP_GE = "%ge";       // >=

constexpr const char *OP_AND = "%and";      // &
constexpr const char *OP_OR = "%or";       // |
constexpr const char *OP_XOR = "%xor";      // ^

constexpr const char *OP_MATCH   = "%match";    // =~
constexpr const char *OP_UNMATCH = "%unmatch";    // !~

// indexer op
constexpr const char *OP_GET = "%get";      // []
constexpr const char *OP_SET = "%set";      // [] =

// iterator (for-in)
constexpr const char *OP_ITER = "%iter";
constexpr const char *OP_NEXT = "%next";
constexpr const char *OP_HAS_NEXT = "%has_nex";

constexpr const char *OP_BOOL = "%bool";    // for boolean cast

// to string
constexpr const char *OP_STR = "%str";    // for string cast or command argument
constexpr const char *OP_INTERP = "%interp";    // for interpolation

// to command argument
constexpr const char *OP_CMD_ARG = "%cmd_arg";  // for command argument

// num cast
constexpr const char *OP_TO_INT = "toInt";
constexpr const char *OP_TO_FLOAT = "toFloat";

// =====  environmental variable  =====

constexpr const char *ENV_HOME = "HOME";
constexpr const char *ENV_LOGNAME = "LOGNAME";
constexpr const char *ENV_USER = "USER";
constexpr const char *ENV_PWD = "PWD";
constexpr const char *ENV_OLDPWD = "OLDPWD";
constexpr const char *ENV_PATH = "PATH";
constexpr const char *ENV_SHLVL = "SHLVL";
constexpr const char *ENV_TERM = "TERM";

// =====  default value  =====

constexpr const char *VAL_DEFAULT_PATH = "/bin:/usr/bin:/usr/local/bin";

// =====  system error message prefix  =====

constexpr const char *EXEC_ERROR = "execution error: ";
constexpr const char *REDIR_ERROR = "io redirection error";
constexpr const char *UNDEF_ENV_ERROR = "undefined environmental variable: ";

// =====  generic type name  =====

constexpr const char *TYPE_ARRAY = "Array";
constexpr const char *TYPE_MAP = "Map";
constexpr const char *TYPE_TUPLE = "Tuple";
constexpr const char *TYPE_OPTION = "Option";
constexpr const char *TYPE_FUNC = "Func";

// =====  UDC parameter offset =====

constexpr unsigned int UDC_PARAM_ATTR  = 0;
constexpr unsigned int UDC_PARAM_REDIR = 1;
constexpr unsigned int UDC_PARAM_ARGV  = 2;

// =====  termination kind  =====

constexpr unsigned int TERM_ON_EXIT   = 1u << 0u;
constexpr unsigned int TERM_ON_ERR    = 1u << 1u;
constexpr unsigned int TERM_ON_ASSERT = 1u << 2u;

// =====  for symbol lookup =====
constexpr const char *CMD_SYMBOL_PREFIX = "%c";
constexpr const char *MOD_SYMBOL_PREFIX = "%mod";

// =====  for user-defined command  =====
constexpr flag8_t UDC_ATTR_SETVAR    = 1u << 0u;
constexpr flag8_t UDC_ATTR_NEED_FORK = 1u << 1u;

// =====  for code generation  =====
constexpr size_t CODE_MAX_LEN = UINT32_MAX;

enum class ForkKind : unsigned char {
    STR,        // capture stdout as string. ex. "$(echo)"
    ARRAY,      // capture stdout as string array. ex. $(echo)
    IN_PIPE,    // capture stdin as pipe. ex. >(echo)
    OUT_PIPE,   // capture stdout as pipe. ex. <(echo)
    COPROC,     // launch as co-process. ex. coproc echo
    JOB,        // launch as background job. ex. echo &
    DISOWN,     // launch as disowned background job. ex. echo &!
};

enum class GlobMeta : unsigned char {
    ANY,
    ZERO_OR_MORE,
};

inline const char *toString(GlobMeta meta) {
    switch(meta) {
    case GlobMeta::ANY:
        return "?";
    case GlobMeta::ZERO_OR_MORE:
        return "*";
    }
    return "";  // normally unreachable
}


// ===== for configuration =====
constexpr const char *LOCAL_CONFIG_DIR = "~/.ydsh";
constexpr const char *LOCAL_MOD_DIR = "~/.ydsh/module";

constexpr const char *SYSTEM_CONFIG_DIR = X_INSTALL_PREFIX "/etc/ydsh";
constexpr const char *SYSTEM_MOD_DIR = X_INSTALL_PREFIX "/etc/ydsh/module";

} // namespace ydsh

#endif //YDSH_SYMBOL_H
