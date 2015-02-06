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

#include <core/FieldHandle.h>
#include <core/DSType.h>
#include <core/native_func_info.h>

#include <assert.h>

// #########################
// ##     FieldHandle     ##
// #########################

FieldHandle::FieldHandle(DSType *fieldType, int fieldIndex, bool readOnly) :
        fieldType(fieldType), fieldIndex(fieldIndex), attributeSet() {

}

FieldHandle::~FieldHandle() {
}

DSType *FieldHandle::getFieldType(TypePool *typePool) {
    return this->fieldType;
}

int FieldHandle::getFieldIndex() {
    return this->fieldIndex;
}

void FieldHandle::setAttribute(unsigned char attribute) {
    this->attributeSet |= attribute;
}

void FieldHandle::unsetAttribute(unsigned char attribute) {
    this->attributeSet &= ~attribute;
}

bool FieldHandle::hasAttribute(unsigned char targetAttr) {
    return (this->attributeSet & targetAttr) == targetAttr;
}

bool FieldHandle::isReadOnly() {
    return this->hasAttribute(READ_ONLY);
}

bool FieldHandle::isGlobal() {
    return this->hasAttribute(GLOBAL);
}


// ############################
// ##     FunctionHandle     ##
// ############################

FunctionHandle::FunctionHandle(DSType *returnType, const std::vector<DSType*> &paramTypes) :
        FunctionHandle(returnType, paramTypes, -1) {
}

FunctionHandle::FunctionHandle(DSType *returnType, const std::vector<DSType*> &paramTypes, int fieldIndex) :
        FieldHandle(0, fieldIndex, true),
        returnType(returnType), paramTypes(paramTypes), paramIndexMap(), defaultValues(paramTypes.size()) {
}

FunctionHandle::FunctionHandle(unsigned int paramSize, int fieldIndex) :
        FieldHandle(0, fieldIndex, true),
        returnType(), paramTypes(paramSize), paramIndexMap(), defaultValues(paramSize) {
}

FunctionHandle::~FunctionHandle() {
}

DSType *FunctionHandle::getFieldType(TypePool *typePool) {
    if(this->fieldType == 0) {
        FunctionType *funcType = typePool->createAndGetFuncTypeIfUndefined(this->returnType, this->paramTypes);
        this->fieldType = funcType;
    }
    return this->fieldType;
}

FunctionType *FunctionHandle::getFuncType(TypePool *typePool) {
    return dynamic_cast<FunctionType*>(this->getFieldType(typePool));
}

DSType *FunctionHandle::getReturnType() {
    return this->returnType;
}

const std::vector<DSType*> &FunctionHandle::getParamTypes() {
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

// #######################################
// ##     LazyInitializedFuncHandle     ##
// #######################################

LazyInitializedFuncHandle::LazyInitializedFuncHandle(native_func_info_t *info, int fieldIndex) :
        FunctionHandle(GET_PARAM_SIZE(info), fieldIndex) {
}

LazyInitializedFuncHandle::~LazyInitializedFuncHandle() {
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
        elementTypes.push_back(decodeType(typePool, pos, elementType0, elementType1));
        return typePool->createAndGetReifiedTypeIfUndefined(t, elementTypes);
    }
    case MAP_T: {
        TypeTemplate *t = typePool->getMapTemplate();
        unsigned int size = decodeNum(pos);
        std::vector<DSType*> elementTypes(size);
        for(unsigned int i = 0; i < size; i++) {
            elementTypes.push_back(decodeType(typePool, pos, elementType0, elementType1));
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
        assert(false);
        break;
    case T0:
        return elementType0;
    case T1:
        return elementType1;
    }
    return 0;
}

void LazyInitializedFuncHandle::initialize(TypePool *typePool, native_func_info_t *info,
        DSType *elementType0, DSType *elementType1) {
    if(this->returnType != 0) {
        return;
    }

    /**
     * init paramIndexMap
     */
    unsigned int paramSize = this->paramTypes.size();
    for(unsigned int i = 0; i < paramSize; i++) {
        this->paramIndexMap.insert(std::make_pair(std::string(info->paramNames[i]), i));
    }

    /**
     * init return type
     */
    char *pos = info->typeInfo;
    this->returnType = decodeType(typePool, pos, elementType0, elementType1);

    /**
     * init param types
     */
    pos++;  // skip param size
    for(unsigned int i = 0; i < paramSize; i++) {
        this->paramTypes[i] = decodeType(typePool, pos, elementType0, elementType1);
    }

    /**
     * init default value map
     */
    for(unsigned int i = 0; i < paramSize; i++) {
        unsigned int mask = (1 << i);
        this->defaultValues[i] = ((info->defaultValueFlag & mask) == mask);
    }
}

