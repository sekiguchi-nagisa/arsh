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

#ifndef CORE_NATIVE_FUNC_INFO_H_
#define CORE_NATIVE_FUNC_INFO_H_

class DSType;
class TypePool;

/**
 * for function handle(method handle or constructor handle) creation.
 */
struct native_func_info_t {
    /**
     * if null, treat as constructor.
     */
    const char *funcName;

    /**
     * serialized function handle
     */
    char *handleInfo;

    const char **paramNames;

    /**
     * DSObject *func(RuntimeContext *ctx, DSObject *arg1, DSObject *arg2, ....)
     */
    void *func_ptr;

    /**
     * if arg1, arg3, arg4 has default value, then (00001101).
     * support up to 8 arguments.
     */
    const unsigned char defaultValueFlag;
};

struct native_type_info_t {
    /**
     * may be null, if has no constructor.
     */
    struct native_func_info_t *initInfo;

    unsigned int methodSize;

    /**
     * may be null, if methodSize is 0
     */
    native_func_info_t **funcInfos;
};

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
    // type definition
    VOID_T = 32,
    ANY_T,
    INT_T,
    FLOAT_T,
    BOOL_T,
    STRING_T,
    // type template
    ARRAY_T,
    MAP_T,
    // param types number
    P_N0,
    P_N1,
    P_N2,
    P_N3,
    P_N4,
    P_N5,
    P_N6,
    P_N7,
    P_N8,
    // parametric type
    T0,
    T1,
} TypeInfo;

#define GET_PARAM_SIZE(info) ((unsigned int)(info->handleInfo[1] - P_N0))

/**
 * check correctness of typeInfo.
 */
bool verifyHandleInfo(char *handleInfo);


#endif /* CORE_NATIVE_FUNC_INFO_H_ */
