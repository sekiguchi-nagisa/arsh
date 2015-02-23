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

#include <core/native_type_info.h>
#include <core/DSType.h>
#include <core/TypePool.h>
#include <util/debug.h>

#include <assert.h>

typedef struct {
    const char *handleInfo;
    unsigned int pos;
} context_t;

static bool isType(context_t *ctx) {
    if(ctx->handleInfo[ctx->pos] != '\0') {
        switch(ctx->handleInfo[ctx->pos++]) {
        case VOID_T:
        case ANY_T:
        case INT_T:
        case FLOAT_T:
        case BOOL_T:
        case STRING_T:
        case ARRAY_T:
        case MAP_T:
            return true;
        case P_N0:
        case P_N1:
        case P_N2:
        case P_N3:
        case P_N4:
        case P_N5:
        case P_N6:
        case P_N7:
        case P_N8:
            return false;
        case T0:
        case T1:
            return true;
        }
    }
    return false;
}

static int getNum(context_t *ctx) {
    if(ctx->handleInfo[ctx->pos] != '\0') {
        char ch = ctx->handleInfo[ctx->pos++];
        switch(ch) {
        case VOID_T:
        case ANY_T:
        case INT_T:
        case FLOAT_T:
        case BOOL_T:
        case STRING_T:
        case ARRAY_T:
        case MAP_T:
            return -1;
        case P_N0:
        case P_N1:
        case P_N2:
        case P_N3:
        case P_N4:
        case P_N5:
        case P_N6:
        case P_N7:
        case P_N8:
            return (int) (ch - P_N0);
        case T0:
        case T1:
            return -1;
        }
    }
    return -1;
}


bool verifyHandleInfo(char *handleInfo) {
    context_t ctx = {handleInfo, 0};

    /**
     * check return type
     */
    if(!isType(&ctx)) {
        return false;
    }

    /**
     * check param size
     */
    int paramSize = getNum(&ctx);
    if(paramSize < 0 || paramSize > 8) {
        return false;
    }

    /**
     * check param types
     */
    for(int i = 0; i < paramSize; i++) {
        if(!isType(&ctx)) {
            return false;
        }
    }

    /**
     * check null terminate
     */
    return ctx.handleInfo[ctx.pos] == '\0';
}

static inline unsigned int decodeNum(char *&pos) {
    return (unsigned int) (*(pos++) - P_N0);
}

static DSType *decodeType(TypePool *typePool, char *&pos,
        DSType *elementType0, DSType *elementType1) {
    switch(*(pos++)) {
    case VOID_T:
        return typePool->getVoidType();
    case ANY_T:
        return typePool->getAnyType();
    case INT_T:
        return typePool->getIntType();
    case FLOAT_T:
        return typePool->getFloatType();
    case BOOL_T:
        return typePool->getBooleanType();
    case STRING_T:
        return typePool->getStringType();
    case ARRAY_T: {
        TypeTemplate *t = typePool->getArrayTemplate();
        unsigned int size = decodeNum(pos);
        assert(size == 1);
        std::vector<DSType*> elementTypes(size);
        elementTypes[0] = decodeType(typePool, pos, elementType0, elementType1);
        return typePool->createAndGetReifiedTypeIfUndefined(t, elementTypes);
    }
    case MAP_T: {
        TypeTemplate *t = typePool->getMapTemplate();
        unsigned int size = decodeNum(pos);
        assert(size == 2);
        std::vector<DSType*> elementTypes(size);
        for(unsigned int i = 0; i < size; i++) {
            elementTypes[i] = decodeType(typePool, pos, elementType0, elementType1);
        }
        return typePool->createAndGetReifiedTypeIfUndefined(t, elementTypes);
    }
    case P_N0:
    case P_N1:
    case P_N2:
    case P_N3:
    case P_N4:
    case P_N5:
    case P_N6:
    case P_N7:
    case P_N8:
        fatal("must be type");
        break;
    case T0:
        return elementType0;
    case T1:
        return elementType1;
    default:
        fatal("broken handle info");
    }
    return 0;
}

FunctionHandle *decodeToFuncHandle(TypePool *typePool, int fieldIndex, native_func_info_t *info,
        DSType *elementType0, DSType *elementType1) {

    /**
     * init return type
     */
    char *pos = info->handleInfo;
    DSType *returnType = decodeType(typePool, pos, elementType0, elementType1);

    /**
     * init param types
     */
    unsigned int paramSize = decodeNum(pos);
    std::vector<DSType*> paramTypes(paramSize);
    for(unsigned int i = 0; i < paramSize; i++) {
        paramTypes[i] = decodeType(typePool, pos, elementType0, elementType1);
    }

    /**
     * create handle
     */
    FunctionHandle *handle = new FunctionHandle(returnType, paramTypes, fieldIndex);

    /**
     * init default value map
     */
    for(unsigned int i = 0; i < paramSize; i++) {
        unsigned int mask = (1 << i);
        bool defaultValue = ((info->defaultValueFlag & mask) == mask);
        handle->addParamName(std::string(info->paramNames[i]), defaultValue);
    }
    return handle;
}
