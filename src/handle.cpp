/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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
#include "misc/size.hpp"
#include "diagnosis.h"

namespace ydsh {

// #############################
// ##     FieldAttributes     ##
// #############################

std::string FieldAttributes::str() const {
    const char *table[] = {
#define GEN_STR(E, V) #E,
            EACH_FIELD_ATTR(GEN_STR)
#undef GEN_STR
    };

    std::string value;
    for(unsigned int i = 0; i < arraySize(table); i++) {
        if(hasFlag(this->value_, 1u << i)) {
            if(!value.empty()) {
                value += " | ";
            }
            value += table[i];
        }
    }

    return value;
}


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

const std::vector<DSType *> &FunctionHandle::getParamTypes() const {
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

class TypeDecoder {
private:
    TypePool &pool;
    const char *pos;
    const std::vector<DSType *> *types;

public:
    TypeDecoder(TypePool &pool, const char *pos, const std::vector<DSType *> *types) :
            pool(pool), pos(pos), types(types) {}
    ~TypeDecoder() = default;

    DSType *decode();

    unsigned int decodeNum() {
        return static_cast<unsigned int>(*(this->pos++) - P_N0);
    }
};

DSType* TypeDecoder::decode() {
    switch(*(this->pos++)) {
#define GEN_CASE(ENUM) case ENUM: return &this->pool.get##ENUM##Type();
    EACH_HANDLE_INFO_TYPE(GEN_CASE)
#undef GEN_CASE
    case Array: {
        auto &t = this->pool.getArrayTemplate();
        unsigned int size = this->decodeNum();
        assert(size == 1);
        std::vector<DSType *> elementTypes(size);
        elementTypes[0] = decode();
        return &this->pool.createReifiedType(t, std::move(elementTypes));
    }
    case Map: {
        auto &t = this->pool.getMapTemplate();
        unsigned int size = this->decodeNum();
        assert(size == 2);
        std::vector<DSType *> elementTypes(size);
        for(unsigned int i = 0; i < size; i++) {
            elementTypes[i] = this->decode();
        }
        return &this->pool.createReifiedType(t, std::move(elementTypes));
    }
    case Tuple: {
        unsigned int size = this->decodeNum();
        if(size == 0) { // variable length type
            size = this->types->size();
            std::vector<DSType *> elementTypes(size);
            for(unsigned int i = 0; i < size; i++) {
                elementTypes[i] = (*this->types)[i];
            }
            return &this->pool.createTupleType(std::move(elementTypes));
        }

        std::vector<DSType *> elementTypes(size);
        for(unsigned int i = 0; i < size; i++) {
            elementTypes[i] = this->decode();
        }
        return &this->pool.createTupleType(std::move(elementTypes));
    }
    case Option: {
        auto &t = this->pool.getOptionTemplate();
        unsigned int size = this->decodeNum();
        assert(size == 1);
        std::vector<DSType *> elementTypes(size);
        elementTypes[0] = this->decode();
        return &this->pool.createReifiedType(t, std::move(elementTypes));
    }
    case Func: {
        auto *retType = this->decode();
        unsigned int size = this->decodeNum();
        std::vector<DSType *> paramTypes(size);
        for(unsigned int i = 0; i < size; i++) {
            paramTypes[i] = this->decode();
        }
        return &this->pool.createFuncType(retType, std::move(paramTypes));
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
    case T0:
        return (*this->types)[0];
    case T1:
        return (*this->types)[1];
    default:
        fatal("broken handle info\n");
    }
}

bool MethodHandle::init(TypePool &typePool, NativeFuncInfo &info,
                        const std::vector<DSType *> *types) {
    try {
        TypeDecoder decoder(typePool, info.handleInfo, types);

        auto *returnType = decoder.decode();    // init return type
        const unsigned int paramSize = decoder.decodeNum();
        auto *recvType = decoder.decode();
        std::vector<DSType *> paramTypes(paramSize - 1);
        for(unsigned int i = 1; i < paramSize; i++) {   // init param types
            paramTypes[i - 1] = decoder.decode();
        }

        this->returnType = returnType;
        this->recvType = recvType;
        this->paramTypes = std::move(paramTypes);
    } catch(const TypeLookupError &) {
        return false;
    }
    return true;
}

bool MethodHandle::isSignal() const {
    return this->isInterfaceMethod() && this->paramTypes.size() == 1 &&
           this->paramTypes[0]->isFuncType() && this->returnType->isVoidType();
}

} // namespace ydsh