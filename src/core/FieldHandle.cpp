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

// #########################
// ##     FieldHandle     ##
// #########################

FieldHandle::FieldHandle(DSType *fieldType, int fieldIndex, bool readOnly) :
        fieldType(fieldType), fieldIndex(fieldIndex), readOnly(readOnly) {
}

FieldHandle::~FieldHandle() {
}

DSType *FieldHandle::getFieldType(TypePool *typePool) {
    return this->fieldType;
}

int FieldHandle::getFieldIndex() {
    return this->fieldIndex;
}

bool FieldHandle::isReadOnly() {
    return this->readOnly;
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

DSType *FunctionHandle::getReturnType(TypePool *typePool) {
    return this->returnType;
}

const std::vector<DSType*> &FunctionHandle::getParamTypes(TypePool *typePool) {
    return this->paramTypes;
}



bool FunctionHandle::addParamName(const std::string &paramName, bool defaultValue) {
    unsigned int size = this->paramIndexMap.size();
    if(size >= this->paramTypes.size()) {
        return false;
    }

    if(!this->paramIndexMap.insert(std::pair<std::string, int>(paramName, static_cast<int>(size))).second) {
        return false;
    }
    this->defaultValues.push_back(defaultValue);

    return true;
}

int FunctionHandle::getParamIndex(const std::string &paramName) {
    try {
        return this->paramIndexMap.at(paramName);
    } catch(const std::out_of_range &e) {
        return -1;
    }
}

bool FunctionHandle::hasDefaultValue(int paramIndex) {
    return (unsigned int) paramIndex < this->defaultValues.size()
            && this->defaultValues[paramIndex];
}


// ###############################
// ##     ConstructorHandle     ##
// ###############################

ConstructorHandle::ConstructorHandle(const std::vector<DSType*> &paramTypes) :
        FunctionHandle(0, paramTypes) {
}

ConstructorHandle::~ConstructorHandle() {
}

DSType *ConstructorHandle::getFieldType(TypePool *typePool) {
    return 0;
}
