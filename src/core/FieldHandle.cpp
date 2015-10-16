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

#include <cassert>

#include "FieldHandle.h"
#include "DSType.h"
#include "../misc/fatal.h"
#include "../misc/unused.h"

namespace ydsh {
namespace core {

// #########################
// ##     FieldHandle     ##
// #########################

DSType *FieldHandle::getFieldType(TypePool *typePool) {
    UNUSED(typePool);
    return this->fieldType;
}

std::string FieldHandle::toString() const {
    std::string str("{");

    str += "fieldIndex = ";
    str += std::to_string(this->fieldIndex);
    str += ", attributeSet = ";

    unsigned int count = 0;
#define EACH_ATTRIBUTE(OP) \
    OP(READ_ONLY) \
    OP(GLOBAL) \
    OP(ENV) \
    OP(FUNC_HANDLE) \
    OP(INTERFACE)

#define DECODE_ATTR(ATTR) \
    if(this->hasAttribute(ATTR)) { if(count++ > 0) { str += " | "; } str += #ATTR; }

    EACH_ATTRIBUTE(DECODE_ATTR)

#undef DECODE_ATTR
#undef EACH_ATTRIBUTE

    str += "}";
    return str;
}


// ############################
// ##     FunctionHandle     ##
// ############################

DSType *FunctionHandle::getFieldType(TypePool *typePool) {
    if(this->fieldType == nullptr) {
        this->fieldType = typePool->createAndGetFuncTypeIfUndefined(this->returnType, std::move(this->paramTypes));
    }
    return this->fieldType;
}

FunctionType *FunctionHandle::getFuncType(TypePool *typePool) {
    return static_cast<FunctionType *>(this->getFieldType(typePool));
}

const std::vector<DSType *> &FunctionHandle::getParamTypes() {
    if(this->fieldType != nullptr) {
        return static_cast<FunctionType *>(this->fieldType)->getParamTypes();
    }
    return this->paramTypes;
}


// ##########################
// ##     MethodHandle     ##
// ##########################

MethodHandle::~MethodHandle() {
    delete this->next;
    this->next = nullptr;
}

void MethodHandle::addParamType(DSType *type) {
    this->paramTypes.push_back(type);
}

static inline unsigned int decodeNum(const char *&pos) {
    return (unsigned int) (*(pos++) - P_N0);
}

static DSType *decodeType(TypePool *typePool, const char *&pos,
                          const std::vector<DSType *> *types) {
    switch(*(pos++)) {
#define GEN_CASE(ENUM) case ENUM: return typePool->get##ENUM##Type();
    EACH_HANDLE_INFO_TYPE(GEN_CASE)
#undef GEN_CASE
    case Array: {
        TypeTemplate *t = typePool->getArrayTemplate();
        unsigned int size = decodeNum(pos);
        assert(size == 1);
        std::vector<DSType *> elementTypes(size);
        elementTypes[0] = decodeType(typePool, pos, types);
        return typePool->createAndGetReifiedTypeIfUndefined(t, std::move(elementTypes));
    }
    case Map: {
        TypeTemplate *t = typePool->getMapTemplate();
        unsigned int size = decodeNum(pos);
        assert(size == 2);
        std::vector<DSType *> elementTypes(size);
        for(unsigned int i = 0; i < size; i++) {
            elementTypes[i] = decodeType(typePool, pos, types);
        }
        return typePool->createAndGetReifiedTypeIfUndefined(t, std::move(elementTypes));
    }
    case Tuple: {
        unsigned int size = decodeNum(pos);
        if(size == 0) { // variable length type
            size = types->size();
            std::vector<DSType *> elementTypes(size);
            for(unsigned int i = 0; i < size; i++) {
                elementTypes[i] = (*types)[i];
            }
            return typePool->createAndGetTupleTypeIfUndefined(std::move(elementTypes));
        } else {
            std::vector<DSType *> elementTypes(size);
            for(unsigned int i = 0; i < size; i++) {
                elementTypes[i] = decodeType(typePool, pos, types);
            }
            return typePool->createAndGetTupleTypeIfUndefined(std::move(elementTypes));
        }
    };
    case P_N0:
    case P_N1:
    case P_N2:
    case P_N3:
    case P_N4:
    case P_N5:
    case P_N6:
    case P_N7:
    case P_N8:
        fatal("must be type\n");
        break;
    case T0:
        return (*types)[0];
    case T1:
        return (*types)[1];
    default:
        fatal("broken handle info\n");
    }
    return 0;
}

void MethodHandle::init(TypePool *typePool, NativeFuncInfo *info,
                        const std::vector<DSType *> *types) {
    // init return type
    const char *pos = info->handleInfo;
    this->returnType = decodeType(typePool, pos, types);

    /**
     * init param types
     */
    unsigned int paramSize = decodeNum(pos);
    for(unsigned int i = 0; i < paramSize; i++) {
        if(i == 0) {
            this->recvType = decodeType(typePool, pos, types);
        } else {
            this->paramTypes.push_back(decodeType(typePool, pos,types));
        }
    }
}

bool MethodHandle::isSignal() {
    return this->isInterfaceMethod() && this->paramTypes.size() == 1 &&
           this->paramTypes[0]->isFuncType() && this->returnType->isVoidType();
}

} // namespace core
} // namespace ydsh