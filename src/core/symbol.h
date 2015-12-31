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

#ifndef YDSH_SYMBOL_H
#define YDSH_SYMBOL_H

namespace ydsh {
namespace core {

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

constexpr const char *VAR_DEFAULT_PATH = "/bin:/usr/bin:/usr/local/bin";


// =====  magic method  =====

// unary op definition
constexpr const char *OP_PLUS = "__PLUS__";     // +
constexpr const char *OP_MINUS = "__MINUS__";    // -
constexpr const char *OP_NOT = "__NOT__";      // not

// binary op definition
constexpr const char *OP_ADD = "__ADD__";      // +
constexpr const char *OP_SUB = "__SUB__";      // -
constexpr const char *OP_MUL = "__MUL__";      // *
constexpr const char *OP_DIV = "__DIV__";      // /
constexpr const char *OP_MOD = "__MOD__";      // %

constexpr const char *OP_EQ = "__EQ__";       // ==
constexpr const char *OP_NE = "__NE__";       // !=

constexpr const char *OP_LT = "__LT__";       // <
constexpr const char *OP_GT = "__GT__";       // >
constexpr const char *OP_LE = "__LE__";       // <=
constexpr const char *OP_GE = "__GE__";       // >=

constexpr const char *OP_AND = "__AND__";      // &
constexpr const char *OP_OR = "__OR__";       // |
constexpr const char *OP_XOR = "__XOR__";      // ^

constexpr const char *OP_MATCH   = "__MATCH__";    // =~
constexpr const char *OP_UNMATCH = "__UNMATCH__";    // !~

// indexer op
constexpr const char *OP_GET = "__GET__";      // []
constexpr const char *OP_SET = "__SET__";      // [] =

// iterator (for-in)
constexpr const char *OP_ITER = "__ITERATOR__";
constexpr const char *OP_NEXT = "__NEXT__";
constexpr const char *OP_HAS_NEXT = "__HAS_NEXT__";

// to string
constexpr const char *OP_STR = "__STR__";    // for string cast or command argument
constexpr const char *OP_INTERP = "__INTERP__";    // for interpolation

// to command argument
constexpr const char *OP_CMD_ARG = "__CMD_ARG__";  // for command argument

} // namespace core
} // namespace ydsh

#endif //YDSH_SYMBOL_H
