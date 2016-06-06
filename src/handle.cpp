/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#include "handle.h"
#include "type.h"
#include "misc/fatal.h"

namespace ydsh {

// #########################
// ##     FieldHandle     ##
// #########################

DSType *FieldHandle::getFieldType(TypePool &) {
    return this->fieldType;
}


// ############################
// ##     FunctionHandle     ##
// ############################

DSType *FunctionHandle::getFieldType(TypePool &typePool) {
    if(this->fieldType == nullptr) {
        this->fieldType = &typePool.createFuncType(this->returnType, std::move(this->paramTypes));
    }
    return this->fieldType;
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
}

void MethodHandle::addParamType(DSType &type) {
    this->paramTypes.push_back(&type);
}

static inline unsigned int decodeNum(const char *&pos) {
    return (unsigned int) (*(pos++) - P_N0);
}

static DSType *decodeType(TypePool &typePool, const char *&pos,
                          const std::vector<DSType *> *types) {
    switch(*(pos++)) {
#define GEN_CASE(ENUM) case ENUM: return &typePool.get##ENUM##Type();
    EACH_HANDLE_INFO_TYPE(GEN_CASE)
#undef GEN_CASE
    case Array: {
        auto &t = typePool.getArrayTemplate();
        unsigned int size = decodeNum(pos);
        assert(size == 1);
        std::vector<DSType *> elementTypes(size);
        elementTypes[0] = decodeType(typePool, pos, types);
        return &typePool.createReifiedType(t, std::move(elementTypes));
    }
    case Map: {
        auto &t = typePool.getMapTemplate();
        unsigned int size = decodeNum(pos);
        assert(size == 2);
        std::vector<DSType *> elementTypes(size);
        for(unsigned int i = 0; i < size; i++) {
            elementTypes[i] = decodeType(typePool, pos, types);
        }
        return &typePool.createReifiedType(t, std::move(elementTypes));
    }
    case Tuple: {
        unsigned int size = decodeNum(pos);
        if(size == 0) { // variable length type
            size = types->size();
            std::vector<DSType *> elementTypes(size);
            for(unsigned int i = 0; i < size; i++) {
                elementTypes[i] = (*types)[i];
            }
            return &typePool.createTupleType(std::move(elementTypes));
        } else {
            std::vector<DSType *> elementTypes(size);
            for(unsigned int i = 0; i < size; i++) {
                elementTypes[i] = decodeType(typePool, pos, types);
            }
            return &typePool.createTupleType(std::move(elementTypes));
        }
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
        fatal("must be type\n");
        break;
    case T0:
        return (*types)[0];
    case T1:
        return (*types)[1];
    default:
        fatal("broken handle info\n");
    }
    return nullptr;
}

void MethodHandle::init(TypePool &typePool, NativeFuncInfo &info,
                        const std::vector<DSType *> *types) {
    // init return type
    const char *pos = info.handleInfo;
    this->returnType = decodeType(typePool, pos, types);

    /**
     * init param types
     */
    const unsigned int paramSize = decodeNum(pos);
    this->recvType = decodeType(typePool, pos, types);
    for(unsigned int i = 1; i < paramSize; i++) {
        this->paramTypes.push_back(decodeType(typePool, pos,types));
    }
}

bool MethodHandle::isSignal() const {
    return this->isInterfaceMethod() && this->paramTypes.size() == 1 &&
           this->paramTypes[0]->isFuncType() && this->returnType->isVoidType();
}

} // namespace ydsh