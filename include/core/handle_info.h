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

#ifndef CORE_HANDLE_INFO_H_
#define CORE_HANDLE_INFO_H_

namespace ydsh {
namespace core {

// builtin type
#define EACH_HANDLE_INFO_TYPE(OP) \
    OP(Void) \
    OP(Any) \
    OP(Int) \
    OP(Int8) \
    OP(Uint8) \
    OP(Int16) \
    OP(Uint16) \
    OP(Int32) \
    OP(Uint32) \
    OP(Int64) \
    OP(Uint64) \
    OP(Float) \
    OP(Boolean)  \
    OP(String) \
    OP(Error) \
    OP(ArithmeticError) \
    OP(OutOfIndexError) \
    OP(KeyNotFoundError) \
    OP(TypeCastError)

// type template
#define EACH_HANDLE_INFO_TYPE_TEMP(OP) \
    OP(Array) \
    OP(Map)

// param types num
#define EACH_HANDLE_INFO_NUM(OP) \
    OP(P_N0) \
    OP(P_N1) \
    OP(P_N2) \
    OP(P_N3) \
    OP(P_N4) \
    OP(P_N5) \
    OP(P_N6) \
    OP(P_N7) \
    OP(P_N8)

// parametric type
#define EACH_HANDLE_INFO_PTYPE(OP) \
    OP(T0) \
    OP(T1)

#define EACH_HANDLE_INFO(OP) \
    EACH_HANDLE_INFO_TYPE(OP) \
    EACH_HANDLE_INFO_TYPE_TEMP(OP) \
    EACH_HANDLE_INFO_NUM(OP) \
    EACH_HANDLE_INFO_PTYPE(OP)


/*
 * encoded type definition
 * ex. function hoge(a : Int, b = "re", c : Boolean, d = 2.3) : Int
 * --> INT_T P_N4 INT_T STRING_T BOOL_T FLOAT_T
 *     defaultValueFlag (00001010)
 * ex. constructor(a : Array<Int>, b : T1)
 * --> VOID_T P_N2 ARRAY_T P_N1 INT_T T1
 *     defaultValueFlag (00000000)
 */
enum HandleInfo {
#define GEN_ENUM(ENUM) ENUM,
    EACH_HANDLE_INFO(GEN_ENUM)
#undef GEN_ENUM
};

} // namespace core
} // namespace ydsh

#endif /* CORE_HANDLE_INFO_H_ */
