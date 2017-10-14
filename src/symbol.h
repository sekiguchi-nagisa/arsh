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

namespace ydsh {

/**
 * built-in symbol(builtin variable, magic method) definition
 */

// =====  builtin variable  =====

// boolean
constexpr const char *VAR_TRUE = "TRUE";
constexpr const char *VAR_FALSE = "FALSE";

// io
constexpr const char *VAR_STDIN = "STDIN";
constexpr const char *VAR_STDOUT = "STDOUT";
constexpr const char *VAR_STDERR = "STDERR";

// shell builtin
constexpr const char *VAR_PS1 = "PS1";
constexpr const char *VAR_PS2 = "PS2";

constexpr const char *VAR_HISTFILE = "HISTFILE";
constexpr const char *VAR_HISTSIZE = "HISTSIZE";
constexpr const char *VAR_HISTFILESIZE = "HISTFILESIZE";

constexpr const char *VAR_SCRIPT_DIR = "SCRIPT_DIR";

constexpr const char *VAR_SIG_DFL = "SIG_DFL";
constexpr const char *VAR_SIG_IGN = "SIG_IGN";

// =====  magic method  =====

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

// to string
constexpr const char *OP_STR = "%str";    // for string cast or command argument
constexpr const char *OP_INTERP = "%interp";    // for interpolation

// to command argument
constexpr const char *OP_CMD_ARG = "%cmd_arg";  // for command argument

// =====  environmental variable  =====

constexpr const char *ENV_HOME = "HOME";
constexpr const char *ENV_LOGNAME = "LOGNAME";
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


} // namespace ydsh

#endif //YDSH_SYMBOL_H
