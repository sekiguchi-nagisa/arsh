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

#ifndef YDSH_INCLUDE_CORE_BIND_H_
#define YDSH_INCLUDE_CORE_BIND_H_

#include <core/DSType.h>
#include <core/TypePool.h>

/**
 * for function handle(method handle or constructor handle) creation.
 */
struct native_func_info_t {
    const char *funcName;

    /**
     * serialized function type
     */
    const char *typeInfo;

    /**
     * parameter size of native function(exclude first parameter).
     * must be under 8.
     */
    int paramSize;

    /**
     * DSObject *func(RuntimeContext *ctx, DSObject *arg1, DSObject *arg2, ....)
     */
    void *func_ptr;

    /**
     * ex. if arg1, arg3 has default value, 00000101
     */
    unsigned char defaultValueFlag;

};

/**
 * create function handle, function object and add to type.
 */
void bindNativeFunc(TypePool *typePool, ClassType *type, native_func_info_t *info);

/**
 * create constructor handle and add to type.
 */
void bindNativeFuncAsInit(TypePool *typePool, ClassType *type, native_func_info_t *info);

#endif /* YDSH_INCLUDE_CORE_BIND_H_ */
