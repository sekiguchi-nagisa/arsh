/*
 * native_bind.h
 *
 *  Created on: 2015/01/20
 *      Author: skgchxngsxyz-osx
 */

#ifndef CORE_BIND_H_
#define CORE_BIND_H_

#include "DSType.h"
#include "TypePool.h"

/**
 * for function handle(method handle or constructor handle) creation.
 */
typedef struct {
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

} native_func_info_t;

/**
 * create function handle, function object and add to type.
 */
void bindNativeFunc(TypePool *typePool, ClassType *type, native_func_info_t *info);

/**
 * create constructor handle and add to type.
 */
void bindNativeFuncAsInit(TypePool *typePool, ClassType *type, native_func_info_t *info);

#endif /* CORE_BIND_H_ */
