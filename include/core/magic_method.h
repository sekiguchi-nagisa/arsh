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

#ifndef SRC_CORE_MAGIC_METHOD_H_
#define SRC_CORE_MAGIC_METHOD_H_

#include <string>

// unary op definition
extern const char * const PLUS;  // +
extern const char * const MINUS; // -
extern const char * const NOT;   // not

// binary op definition
extern const char * const ADD;   // +
extern const char * const SUB;   // -
extern const char * const MUL;   // *
extern const char * const DIV;   // /
extern const char * const MOD;   // %

extern const char * const EQ;    // ==
extern const char * const NE;    // !=

extern const char * const LT;    // <
extern const char * const GT;    // >
extern const char * const LE;    // <=
extern const char * const GE;    // >=

extern const char * const AND;   // &
extern const char * const OR;    // |
extern const char * const XOR;   // ^

extern const char * const RE_EQ; // =~
extern const char * const RE_NE; // !~

// indexer op
extern const char * const GET;   // []
extern const char * const SET;   // [] =

// iterator
extern const char * const RESET; // must be Func<Any> type
extern const char * const NEXT;  // must be Func<Any> type
extern const char * const HAS_NEXT;  // must be Func<Boolean> type


#endif /* SRC_CORE_MAGIC_METHOD_H_ */
