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

/*
 * encoded type definition
 * ex. function hoge(a : Int, b = "re", c : Boolean, d = 2.3) : Int
 * --> INT_T P_N4 INT_T STRING_T BOOL_T FLOAT_T
 *     defaultValueFlag (00001010)
 * ex. constructor(a : Array<Int>, b : T1)
 * --> VOID_T P_N2 ARRAY_T P_N1 INT_T T1
 *     defaultValueFlag (00000000)
 */
typedef enum {
    VOID_T = 32,    // type definition
    ANY_T,
    INT_T,
    FLOAT_T,
    BOOL_T,
    STRING_T,
    ARRAY_T,// type template
    MAP_T,
    P_N0,   // param types number
    P_N1,
    P_N2,
    P_N3,
    P_N4,
    P_N5,
    P_N6,
    P_N7,
    P_N8,
    T0,     // parametric type
    T1,
} TypeInfo;

} // namespace core
} // namespace ydsh

#endif /* CORE_HANDLE_INFO_H_ */
