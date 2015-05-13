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

#ifndef CORE_SYMBOL_H_
#define CORE_SYMBOL_H_

namespace ydsh {
namespace core {

/**
 * built-in symbol(builtin variable, magic method) definition
 */

// =====  builtin variable  =====

// boolean
constexpr char VAR_TRUE[] = "TRUE";
constexpr char VAR_FALSE[] = "FALSE";

// io
constexpr char VAR_STDIN[] = "STDIN";
constexpr char VAR_STDOUT[] = "STDOUT";
constexpr char VAR_STDERR[] = "STDERR";


// =====  magic method  =====

// unary op definition
constexpr char OP_PLUS[] = "__PLUS__";     // +
constexpr char OP_MINUS[] = "__MINUS__";    // -
constexpr char OP_NOT[] = "__NOT__";      // not

// binary op definition
constexpr char OP_ADD[] = "__ADD__";      // +
constexpr char OP_SUB[] = "__SUB__";      // -
constexpr char OP_MUL[] = "__MUL__";      // *
constexpr char OP_DIV[] = "__DIV__";      // /
constexpr char OP_MOD[] = "__MOD__";      // %

constexpr char OP_EQ[] = "__EQ__";       // ==
constexpr char OP_NE[] = "__NE__";       // !=

constexpr char OP_LT[] = "__LT__";       // <
constexpr char OP_GT[] = "__GT__";       // >
constexpr char OP_LE[] = "__LE__";       // <=
constexpr char OP_GE[] = "__GE__";       // >=

constexpr char OP_AND[] = "__AND__";      // &
constexpr char OP_OR[] = "__OR__";       // |
constexpr char OP_XOR[] = "__XOR__";      // ^

constexpr char OP_RE_EQ[] = "__RE_EQ__";    // =~
constexpr char OP_RE_NE[] = "__RE_NE__";    // !~

// indexer op
constexpr char OP_GET[] = "__GET__";      // []
constexpr char OP_SET[] = "__SET__";      // [] =

// iterator
constexpr char OP_ITER[] = "__ITERATOR__"; // must be Func<Any> type
constexpr char OP_NEXT[] = "__NEXT__";  // must be Func<Any> type
constexpr char OP_HAS_NEXT[] = "__HAS_NEXT__";  // must be Func<Boolean> type

// to string
constexpr char OP_STR[] = "__STR__";    // for string cast
constexpr char OP_INTERP[] = "__INTERP__";    // for interpolation

// to command argument(string or string array)
constexpr char OP_CMD_ARG[] = "__CMD_ARG__";  // for command argument

} // namespace core
} // namespace ydsh

#endif /* CORE_SYMBOL_H_ */
