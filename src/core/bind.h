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
 * for function handle(method handle) creation.
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
 * for constructor handle creation.
 */
typedef struct {
    /**
     * serialized param types
     */
    const char *typeInfo;

    /**
     * parameter size of native function(exclude first parameter).
     * must be under 8.
     */
    int paramSize;

    /**
     * DSObject *func(RuntimeContext *ctx, DSObject *arg1, DSObject *arg2, ....).
     * return value is always null.
     */
    void *func_ptr;

    /**
     * ex. if arg2, arg3 has default value, 00000110
     */
    unsigned char defaultValueFlag;
} native_init_info_t;

/**
 * for function handle initialization
 */
DSType *decodeFuncTypeInfo(TypePool *typePool, const char *typeInfo);

/**
 * for constructor handle initialization
 */
std::vector<DSType*> decodeParamTypesOfInit(TypePool *typePool, const char *typeInfo);


#endif /* CORE_BIND_H_ */
