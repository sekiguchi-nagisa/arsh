/*
 * DSType.cpp
 *
 *  Created on: 2014/12/31
 *      Author: skgchxngsxyz-osx
 */

#include <stdexcept>

#include "DSType.h"

// ####################
// ##     DSType     ##
// ####################

DSType::DSType() {
}

DSType::~DSType() {
}

bool DSType::hasField(const std::string &fieldName) {
    return this->getFieldIndex(fieldName) != -1;
}

bool DSType::isReadOnly(const std::string &fieldName) {
    return this->isReadOnly(this->getFieldIndex(fieldName));
}

FieldHandle *DSType::lookupFieldHandle(const std::string &fieldName) {
    return this->lookupFieldHandle(this->getFieldIndex(fieldName));
}

bool DSType::isAssignableFrom(DSType *targetType) {
    if (this->equals(targetType)) {
        return true;
    }
    DSType *superType = targetType->getSuperType();
    return superType != 0 && this->isAssignableFrom(superType);
}


// #######################
// ##     ClassType     ##
// #######################

ClassType::ClassType(std::string &&className, bool extendable,
        DSType *superType) :
        superType(superType), baseIndex(
                superType != 0 ? superType->getFieldSize() : 0), className(
                std::move(className)), extendable(extendable), constructorHandle(
                0), fieldIndexMap(), handleTable(), handleFlags() {
}

ClassType::~ClassType() {
}

std::string ClassType::getTypeName() {
    return this->className;
}

bool ClassType::isExtendable() {
    return this->extendable;
}

DSType *ClassType::getSuperType() {
    return this->superType;
}

ConstructorHandle *ClassType::getConstructorHandle() {
    return this->constructorHandle;
}

void ClassType::setConstructorHandle(ConstructorHandle *handle) {
    this->constructorHandle = handle;
}

unsigned int ClassType::getFieldSize() {
    return this->handleTable.size() + this->baseIndex;
}

int ClassType::getFieldIndex(const std::string &fieldName) {
    try {
        return this->fieldIndexMap.at(fieldName);
    } catch (const std::out_of_range &e) {
        return this->superType != 0 ?
                this->superType->getFieldIndex(fieldName) : -1;
    }
}

FieldHandle *ClassType::lookupFieldHandle(int fieldIndex) {
    int actualIndex = fieldIndex - this->baseIndex;
    try {
        return this->handleTable.at(actualIndex);
    } catch (const std::out_of_range &e) {
        return 0;
    }
}

bool ClassType::isReadOnly(int fieldIndex) {
    int actualIndex = fieldIndex - this->baseIndex;
    try {
        return this->handleFlags.at(actualIndex);
    } catch (const std::out_of_range &e) {
        return false;
    }
}

bool ClassType::equals(DSType *targetType) {
    ClassType *t = dynamic_cast<ClassType*>(targetType);
    return t != 0 && this->className == t->className;
}

bool ClassType::addFieldHandle(const std::string &fieldName, bool readOnly,
        FieldHandle *handle) {
    // return false, found duplicated field
    if (this->hasField(fieldName)) {
        return false;
    }

    // add actual index to index map
    int actualIndex = this->handleTable.size() + this->baseIndex;
    this->fieldIndexMap[fieldName] = actualIndex;

    // add handle and flag
    this->handleTable.push_back(handle);
    this->handleFlags.push_back(readOnly);

    return true;
}

DSType *ClassType::anyType = new ClassType("Any", false, 0);
DSType *ClassType::voidType = new ClassType("Void", false, 0);

// ##########################
// ##     FunctionType     ##
// ##########################

FunctionType::FunctionType(DSType *returnType, unsigned int paramSize,
        DSType **paramTypes) :
        returnType(returnType), paramSize(paramSize), paramTypes(paramTypes) {
}

FunctionType::~FunctionType() {
    if (this->paramTypes != 0) {
        delete[] this->paramTypes;
    }
}

DSType *FunctionType::getReturnType() {
    return this->returnType;
}

unsigned int FunctionType::getParamSize() {
    return this->paramSize;
}

DSType **FunctionType::getParamTypes() {
    return this->paramTypes;
}

std::string FunctionType::getTypeName() {
    return toFunctionTypeName(this->returnType, this->paramSize,
            this->paramTypes);
}

bool FunctionType::isExtendable() {
    return false;
}

DSType *FunctionType::getSuperType() {
    return ClassType::anyType;
}

ConstructorHandle *FunctionType::getConstructorHandle() {
    return 0;
}

unsigned int FunctionType::getFieldSize() {
    return 0;
}

int FunctionType::getFieldIndex(const std::string &fieldName) {
    return -1;
}

FieldHandle *FunctionType::lookupFieldHandle(int fieldIndex) {
    return 0;
}

bool FunctionType::isReadOnly(int fieldIndex) {
    return false;
}

bool FunctionType::equals(DSType *targetType) {
    FunctionType *t = dynamic_cast<FunctionType*>(targetType);
    if (t == 0) {
        return false;
    }

    // check return type
    if (!this->returnType->equals(t)) {
        return false;
    }

    // check param size
    unsigned int size = this->paramSize;
    if (size != t->paramSize) {
        return false;
    }

    // check each param type
    for (unsigned int i = 0; i < size; i++) {
        if (!this->paramTypes[i]->equals(t->paramTypes[i])) {
            return false;
        }
    }
    return true;
}

std::string toReifiedTypeName(DSType *templateType, int elementSize,
        DSType **elementTypes) {
    std::string reifiedTypeName = templateType->getTypeName() + "<";
    for (int i = 0; i < elementSize; i++) {
        if (i > 0) {
            reifiedTypeName += ",";
        }
        reifiedTypeName += elementTypes[i]->getTypeName();
    }
    reifiedTypeName += ">";
    return reifiedTypeName;
}

std::string toFunctionTypeName(DSType *returnType, int paramSize,
        DSType **paramTypes) {
    std::string funcTypeName = "Func<" + returnType->getTypeName();
    for (int i = 0; i < paramSize; i++) {
        if (i == 0) {
            funcTypeName += ",[";
        }
        if (i > 0) {
            funcTypeName += ",";
        }
        funcTypeName += paramTypes[i]->getTypeName();
        if (i == paramSize - 1) {
            funcTypeName += "]";
        }
    }
    funcTypeName += ">";
    return funcTypeName;
}
