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

FunctionHandle::FunctionHandle(DSType *returnType, const std::vector<DSType*> paramTypes) :
        FunctionHandle(returnType, paramTypes, -1) {
}

FunctionHandle::FunctionHandle(DSType *returnType, const std::vector<DSType*> paramTypes, int fieldIndex) :
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

DSType *FunctionHandle::getReturnType() {
    return this->returnType;
}

const std::vector<DSType*> &FunctionHandle::getParamTypes() {
    return this->paramTypes;
}

DSType *FunctionHandle::getFirstParamType() {
    return this->paramTypes.size() > 0 ? this->paramTypes[0] : 0;
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
    return paramIndex > -1 && paramIndex < this->defaultValues.size()
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
