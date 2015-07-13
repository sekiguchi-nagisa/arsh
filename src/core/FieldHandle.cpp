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
#include "../misc/debug.h"

namespace ydsh {
namespace core {

// #########################
// ##     FieldHandle     ##
// #########################

FieldHandle::FieldHandle(DSType *fieldType, unsigned int fieldIndex, bool readOnly) :
        fieldType(fieldType), fieldIndex(fieldIndex), attributeSet(0) {
    if(readOnly) {
        this->setAttribute(READ_ONLY);
    }
}

DSType *FieldHandle::getFieldType(TypePool *typePool) {
    return this->fieldType;
}

unsigned int FieldHandle::getFieldIndex() {
    return this->fieldIndex;
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

void FieldHandle::setAttribute(flag8_t attribute) {
    setFlag(this->attributeSet, attribute);
}

void FieldHandle::unsetAttribute(flag8_t attribute) {
    unsetFlag(this->attributeSet, attribute);
}

bool FieldHandle::hasAttribute(flag8_t targetAttr) const {
    return hasFlag(this->attributeSet, targetAttr);
}

bool FieldHandle::isReadOnly() const {
    return this->hasAttribute(READ_ONLY);
}

bool FieldHandle::isGlobal() const {
    return this->hasAttribute(GLOBAL);
}

bool FieldHandle::isEnv() const {
    return this->hasAttribute(ENV);
}

bool FieldHandle::isFuncHandle() const {
    return this->hasAttribute(FUNC_HANDLE);
}

bool FieldHandle::withinInterface() const {
    return this->hasAttribute(INTERFACE);
}


// ############################
// ##     FunctionHandle     ##
// ############################

FunctionHandle::FunctionHandle(DSType *returnType, const std::vector<DSType *> &paramTypes, unsigned int fieldIndex) :
        FieldHandle(0, fieldIndex, true),
        returnType(returnType), paramTypes(paramTypes), paramIndexMap(), defaultValues() {
    this->setAttribute(FUNC_HANDLE);
}

DSType *FunctionHandle::getFieldType(TypePool *typePool) {
    if(this->fieldType == 0) {
        this->fieldType = typePool->createAndGetFuncTypeIfUndefined(this->returnType, std::move(this->paramTypes));
    }
    return this->fieldType;
}

FunctionType *FunctionHandle::getFuncType(TypePool *typePool) {
    return static_cast<FunctionType *>(this->getFieldType(typePool));
}

DSType *FunctionHandle::getReturnType() {
    return this->returnType;
}

const std::vector<DSType *> &FunctionHandle::getParamTypes() {
    if(this->fieldType != nullptr) {
        return static_cast<FunctionType *>(this->fieldType)->getParamTypes();
    }
    return this->paramTypes;
}

bool FunctionHandle::addParamName(const std::string &paramName, bool defaultValue) {
    unsigned int size = this->paramIndexMap.size();
    if(size >= this->paramTypes.size()) {
        return false;
    }

    if(!this->paramIndexMap.insert(std::make_pair(paramName, size)).second) {
        return false;
    }
    this->defaultValues.push_back(defaultValue);

    return true;
}

int FunctionHandle::getParamIndex(const std::string &paramName) {
    auto iter = this->paramIndexMap.find(paramName);
    return iter != this->paramIndexMap.end() ? iter->second : -1;
}

bool FunctionHandle::hasDefaultValue(unsigned int paramIndex) {
    return paramIndex < this->defaultValues.size()
           && this->defaultValues[paramIndex];
}

// ##########################
// ##     MethodHandle     ##
// ##########################

MethodHandle::MethodHandle(int methodIndex) :
        methodIndex(methodIndex), attributeSet(),
        returnType(), recvType(), paramTypes(), next() {
}

MethodHandle::~MethodHandle() {
    delete this->next;
    this->next = 0;
}

unsigned int MethodHandle::getMethodIndex() {
    return this->methodIndex;
}

void MethodHandle::setReturnType(DSType *type) {
    this->returnType = type;
}

DSType *MethodHandle::getReturnType() {
    return this->returnType;
}

void MethodHandle::setRecvType(DSType *type) {
    this->recvType = type;
}

DSType *MethodHandle::getRecvType() {
    return this->recvType;
}

void MethodHandle::addParamType(DSType *type) {
    this->paramTypes.push_back(type);
}

const std::vector<DSType *> &MethodHandle::getParamTypes() {
    return this->paramTypes;
}

static inline unsigned int decodeNum(const char *&pos) {
    return (unsigned int) (*(pos++) - P_N0);
}

static DSType *decodeType(TypePool *typePool, const char *&pos,
                          DSType *elementType0, DSType *elementType1) {
    switch(*(pos++)) {
#define GEN_CASE(ENUM) case ENUM: return typePool->get##ENUM##Type();
    EACH_HANDLE_INFO_TYPE(GEN_CASE)
#undef GEN_CASE
    case Array: {
        TypeTemplate *t = typePool->getArrayTemplate();
        unsigned int size = decodeNum(pos);
        assert(size == 1);
        std::vector<DSType *> elementTypes(size);
        elementTypes[0] = decodeType(typePool, pos, elementType0, elementType1);
        return typePool->createAndGetReifiedTypeIfUndefined(t, std::move(elementTypes));
    }
    case Map: {
        TypeTemplate *t = typePool->getMapTemplate();
        unsigned int size = decodeNum(pos);
        assert(size == 2);
        std::vector<DSType *> elementTypes(size);
        for(unsigned int i = 0; i < size; i++) {
            elementTypes[i] = decodeType(typePool, pos, elementType0, elementType1);
        }
        return typePool->createAndGetReifiedTypeIfUndefined(t, std::move(elementTypes));
    }
    case Tuple: {
        unsigned int size = decodeNum(pos);
        std::vector<DSType *> elementTypes(size);
        for(unsigned int i = 0; i < size; i++) {
            elementTypes[i] = decodeType(typePool, pos, elementType0, elementType1);
        }
        return typePool->createAndGetTupleTypeIfUndefined(std::move(elementTypes));
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
        return elementType0;
    case T1:
        return elementType1;
    default:
        fatal("broken handle info\n");
    }
    return 0;
}

void MethodHandle::init(TypePool *typePool, NativeFuncInfo *info,
                        DSType *elementType0, DSType *elementType1) {
    // init return type
    const char *pos = info->handleInfo;
    this->returnType = decodeType(typePool, pos, elementType0, elementType1);

    /**
     * init param types
     */
    unsigned int paramSize = decodeNum(pos);
    for(unsigned int i = 0; i < paramSize; i++) {
        if(i == 0) {
            this->recvType = decodeType(typePool, pos, elementType0, elementType1);
        } else {
            this->paramTypes.push_back(decodeType(typePool, pos, elementType0, elementType1));
        }
    }

//    /**
//     * init default value map
//     */
//    for(unsigned int i = 0; i < paramSize; i++) {
//        unsigned int mask = (1 << i);
//        bool defaultValue = ((this->defaultValueFlag & mask) == mask);
//        handle->addParamName(std::string(this->paramNames[i]), defaultValue);
//    }
}

bool MethodHandle::initalized() {
    return this->returnType != 0;
}

void MethodHandle::setNext(MethodHandle *handle) {
    this->next = handle;
}

MethodHandle *MethodHandle::getNext() {
    return this->next;
}

void MethodHandle::setAttribute(flag8_t attribute) {
    setFlag(this->attributeSet, attribute);
}

bool MethodHandle::isInterfaceMethod() {
    return hasFlag(this->attributeSet, INTERFACE);
}

bool MethodHandle::hasMultipleReturnType() {
    return hasFlag(this->attributeSet, MULTI_RETURN);
}

bool MethodHandle::isSignal() {
    return this->isInterfaceMethod() && this->paramTypes.size() == 1 &&
           this->paramTypes[0]->isFuncType() && this->returnType->isVoidType();
}

} // namespace core
} // namespace ydsh