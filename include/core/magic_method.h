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
extern const std::string PLUS;  // +
extern const std::string MINUS; // -
extern const std::string NOT;   // not

// binary op definition
extern const std::string ADD;   // +
extern const std::string SUB;   // -
extern const std::string MUL;   // *
extern const std::string DIV;   // /
extern const std::string MOD;   // %

extern const std::string EQ;    // ==
extern const std::string NE;    // !=

extern const std::string LT;    // <
extern const std::string GT;    // >
extern const std::string LE;    // <=
extern const std::string GE;    // >=

extern const std::string AND;   // &
extern const std::string OR;    // |
extern const std::string XOR;   // ^

extern const std::string RE_EQ; // =~
extern const std::string RE_NE; // !~

// indexer op
extern const std::string GET;   // []
extern const std::string SET;   // [] =

// iterator
extern const std::string RESET; // must be Func<Any> type
extern const std::string NEXT;  // must be Func<Any> type
extern const std::string HAS_NEXT;  // must be Func<Boolean> type


#endif /* SRC_CORE_MAGIC_METHOD_H_ */
