/*
 * CalleeHandle.cpp
 *
 *  Created on: 2015/01/01
 *      Author: skgchxngsxyz-osx
 */

#include "CalleeHandle.h"
#include "DSType.h"

// #########################
// ##     FieldHandle     ##
// #########################

FieldHandle::FieldHandle(DSType *fieldType, int fieldIndex, bool readOnly) :
        fieldType(fieldType), fieldIndex(fieldIndex), readOnly(readOnly) {
}

FieldHandle::~FieldHandle() {
}

DSType *FieldHandle::getFieldType() {
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

FunctionHandle::FunctionHandle(FunctionType *funcType) :
        FunctionHandle(funcType, -1) {
}

FunctionHandle::FunctionHandle(FunctionType *funcType, int fieldIndex) :
        FieldHandle(funcType, fieldIndex, true), paramIndexMap(), defaultValues() {
}

FunctionHandle::~FunctionHandle() {
}

FunctionType *FunctionHandle::getFuncType() {
    return dynamic_cast<FunctionType*>(this->getFieldType());
}

bool FunctionHandle::addParamName(const std::string &paramName, bool defaultValue) {
    unsigned int size = this->paramIndexMap.size();
    if(size >= this->getFuncType()->getParamSize()) {
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
    return paramIndex > -1 && paramIndex < this->defaultValues.size()
            && this->defaultValues[paramIndex];
}

// ###############################
// ##     ConstructorHandle     ##
// ###############################

ConstructorHandle::ConstructorHandle(unsigned int paramSize, DSType **paramTypes) :
        FunctionHandle(0), paramSize(paramSize), paramTypes(paramTypes) {
}

ConstructorHandle::~ConstructorHandle() {
    delete[] this->paramTypes;
    this->paramTypes = 0;
}

unsigned int ConstructorHandle::getParamSize() {
    return this->paramSize;
}

DSType **ConstructorHandle::getParamTypes() {
    return this->paramTypes;
}
