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

#ifndef CORE_MAGIC_METHOD_H_
#define CORE_MAGIC_METHOD_H_

#include <string>

// unary op definition
extern const char * const OP_PLUS;  // +
extern const char * const OP_MINUS; // -
extern const char * const OP_NOT;   // not

// binary op definition
extern const char * const OP_ADD;   // +
extern const char * const OP_SUB;   // -
extern const char * const OP_MUL;   // *
extern const char * const OP_DIV;   // /
extern const char * const OP_MOD;   // %

extern const char * const OP_EQ;    // ==
extern const char * const OP_NE;    // !=

extern const char * const OP_LT;    // <
extern const char * const OP_GT;    // >
extern const char * const OP_LE;    // <=
extern const char * const OP_GE;    // >=

extern const char * const OP_AND;   // &
extern const char * const OP_OR;    // |
extern const char * const OP_XOR;   // ^

extern const char * const OP_RE_EQ; // =~
extern const char * const OP_RE_NE; // !~

// indexer op
extern const char * const OP_GET;   // []
extern const char * const OP_SET;   // [] =

// iterator
extern const char * const OP_RESET; // must be Func<Any> type
extern const char * const OP_NEXT;  // must be Func<Any> type
extern const char * const OP_HAS_NEXT;  // must be Func<Boolean> type

// to string
extern const char * const OP_TO_STR;   // for string cast
extern const char * const OP_INTERP;   // for interpolation


#endif /* CORE_MAGIC_METHOD_H_ */
