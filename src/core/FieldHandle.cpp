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
#include <core/TypePool.h>

// #########################
// ##     FieldHandle     ##
// #########################

FieldHandle::FieldHandle(DSType *fieldType, int fieldIndex, bool readOnly) :
        fieldType(fieldType), fieldIndex(fieldIndex), attributeSet(0) {
    if(readOnly) {
        this->setAttribute(READ_ONLY);
    }
}

FieldHandle::~FieldHandle() {
}

DSType *FieldHandle::getFieldType(TypePool *typePool) {
    return this->fieldType;
}

int FieldHandle::getFieldIndex() {
    return this->fieldIndex;
}

void FieldHandle::setAttribute(flag8_t attribute) {
    setFlag(this->attributeSet, attribute);
}

void FieldHandle::unsetAttribute(flag8_t attribute) {
    unsetFlag(this->attributeSet, attribute);
}

bool FieldHandle::hasAttribute(flag8_t targetAttr) {
    return hasFlag(this->attributeSet, targetAttr);
}

bool FieldHandle::isReadOnly() {
    return this->hasAttribute(READ_ONLY);
}

bool FieldHandle::isGlobal() {
    return this->hasAttribute(GLOBAL);
}

bool FieldHandle::isEnv() {
    return this->hasAttribute(ENV);
}

bool FieldHandle::isFuncHandle() {
    return this->hasAttribute(FUNC_HANDLE);
}


// ############################
// ##     FunctionHandle     ##
// ############################

FunctionHandle::FunctionHandle(DSType *returnType, const std::vector<DSType*> &paramTypes) :
        FunctionHandle(returnType, paramTypes, -1) {
}

FunctionHandle::FunctionHandle(DSType *returnType, const std::vector<DSType*> &paramTypes, int fieldIndex) :
        FieldHandle(0, fieldIndex, true),
        returnType(returnType), paramTypes(paramTypes), paramIndexMap(), defaultValues() {
    this->setAttribute(FUNC_HANDLE);
}

FunctionHandle::FunctionHandle(unsigned int paramSize, int fieldIndex) :
        FieldHandle(0, fieldIndex, true),
        returnType(), paramTypes(), paramIndexMap(), defaultValues() {  //FIXME: paramTyes preallocate
    this->setAttribute(FUNC_HANDLE);
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

