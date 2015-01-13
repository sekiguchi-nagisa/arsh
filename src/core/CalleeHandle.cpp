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

FieldHandle::FieldHandle(DSType *fieldType) :
        fieldType(fieldType) {
}

DSType *FieldHandle::getFieldType() {
    return this->fieldType;
}

// ############################
// ##     FunctionHandle     ##
// ############################

FunctionHandle::FunctionHandle(FunctionType *funcType) :
        FieldHandle(funcType), paramIndexMap(), defaultValues() {
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
    try {
        return this->defaultValues.at(paramIndex);
    } catch(const std::out_of_range &e) {
        return false;
    }
}

// ###############################
// ##     ConstructorHandle     ##
// ###############################

ConstructorHandle::ConstructorHandle(unsigned int paramSize, DSType **paramTypes) :
        paramSize(paramSize), paramTypes(paramTypes) {
}

ConstructorHandle::~ConstructorHandle() {
    if(this->paramTypes != 0) {
        delete[] this->paramTypes;
    }
}

unsigned int ConstructorHandle::getParamSize() {
    return this->paramSize;
}

DSType **ConstructorHandle::getParamTypes() {
    return this->paramTypes;
}
