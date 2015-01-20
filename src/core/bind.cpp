/*
 * bind.cpp
 *
 *  Created on: 2015/01/20
 *      Author: skgchxngsxyz-osx
 */

#include "bind.h"

static void bindNativeFuncImpl(TypePool *typePool, ClassType *type,
        native_func_info_t *info, bool asInit) {
    //TODO: implement decoder
}

void bindNativeFunc(TypePool *typePool, ClassType *type, native_func_info_t *info) {
    bindNativeFuncImpl(typePool, type, info, false);
}

void bindNativeFuncAsInit(TypePool *typePool, ClassType *type, native_func_info_t *info) {
    bindNativeFuncImpl(typePool, type, info, true);
}
