/*
 * DSType.cpp
 *
 *  Created on: 2014/12/31
 *      Author: skgchxngsxyz-osx
 */


#include "DSType.h"
#include "DSObject.h"

// ####################
// ##     DSType     ##
// ####################

DSType::DSType() {
}

DSType::~DSType() {
}

bool DSType::hasField(const std::string &fieldName) {
    return this->lookupFieldHandle(fieldName) != 0;
}

FunctionHandle *DSType::lookupFunctionHandle(const std::string &funcName) {
    FieldHandle *handle = this->lookupFieldHandle(funcName);
    return handle != 0 ? dynamic_cast<FunctionHandle*>(handle) : 0;
}

FunctionHandle *DSType::lookupMethodHandle(const std::string &methodName) {
    FunctionHandle *handle = this->lookupFunctionHandle(methodName);
    if(handle != 0) {
        DSType *recvType = handle->getFirstParamType();
        return recvType != 0 && recvType->isAssignableFrom(this) ? handle : 0;
    }
    return 0;
}

bool DSType::isAssignableFrom(DSType *targetType) {
    if(this->equals(targetType)) {
        return true;
    }
    DSType *superType = targetType->getSuperType();
    return superType != 0 && this->isAssignableFrom(superType);
}


// #######################
// ##     ClassType     ##
// #######################

ClassType::ClassType(std::string &&className, bool extendable, DSType *superType) :
        superType(superType), baseIndex(superType != 0 ? superType->getFieldSize() : 0),
        className(std::move(className)), extendable(extendable), constructorHandle(0),
        handleMap(), fieldTable() {
}

ClassType::~ClassType() {
    delete this->constructorHandle;
    this->constructorHandle = 0;

    for(std::pair<std::string, FieldHandle*> pair : this->handleMap) {
        delete pair.second;
    }
    this->handleMap.clear();
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

unsigned int ClassType::getFieldSize() {
    return this->handleMap.size() + this->baseIndex;
}

FieldHandle *ClassType::lookupFieldHandle(const std::string &fieldName) {
    FieldHandle *handle = this->handleMap[fieldName];
    if(handle != 0) {
        return handle;
    }
    return this->superType != 0 ? this->superType->lookupFieldHandle(fieldName) : 0;
}

bool ClassType::equals(DSType *targetType) {
    ClassType *t = dynamic_cast<ClassType*>(targetType);
    return t != 0 && this->className == t->className;
}

bool ClassType::addNewFieldHandle(const std::string &fieldName, bool readOnly, DSType *fieldType) {
    if(this->hasField(fieldName)) {
        return false;
    }
    FieldHandle *handle = new FieldHandle(fieldType, this->getFieldSize(), readOnly);
    this->handleMap[fieldName] = handle;
    return true;
}

FunctionHandle *ClassType::addNewFunctionHandle(const std::string &funcName,
        DSType *returnType, const std::vector<DSType*> &paramTypes) {
    if(this->hasField(funcName)) {
        return 0;
    }
    FunctionHandle *handle = new FunctionHandle(returnType, paramTypes, this->getFieldSize());
    this->handleMap[funcName] = handle;
    return handle;
}

ConstructorHandle *ClassType::setNewConstructorHandle(const std::vector<DSType*> &paramTypes) {
    if(this->constructorHandle != 0) {
        delete this->constructorHandle;
    }
    ConstructorHandle *handle = new ConstructorHandle(paramTypes);
    this->constructorHandle = handle;
    return handle;
}

void ClassType::addFunction(FuncObject *func) {
    //TODO:
}

void ClassType::setConstructor(FuncObject *func) {
    //TODO:
}

DSType *ClassType::anyType = new ClassType("Any", false, 0);
DSType *ClassType::voidType = new ClassType("Void", false, 0);


// ##########################
// ##     FunctionType     ##
// ##########################

FunctionType::FunctionType(DSType *returnType, const std::vector<DSType*> &paramTypes) :
        returnType(returnType), paramSize(paramTypes.size()), paramTypes(new DSType*[paramTypes.size()]) {
    for(unsigned int i = 0; i < this->paramSize; i++) {
        this->paramTypes[i] = paramTypes[i];
    }
}

FunctionType::~FunctionType() {
    delete[] this->paramTypes;
    this->paramTypes = 0;
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

DSType *FunctionType::getFirstParamType() {
    return this->paramSize > 0 ? this->paramTypes[0] : 0;
}

std::string FunctionType::getTypeName() {
    std::vector<DSType*> types(this->paramSize);
    for(unsigned int i = 0; i < this->paramSize; i++) {
        types.push_back(this->paramTypes[i]);
    }
    return toFunctionTypeName(this->returnType, types);
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

FieldHandle *FunctionType::lookupFieldHandle(const std::string &fieldName) {
    return 0;
}

bool FunctionType::equals(DSType *targetType) {
    FunctionType *t = dynamic_cast<FunctionType*>(targetType);
    if(t == 0) {
        return false;
    }

    // check return type
    if(!this->returnType->equals(t)) {
        return false;
    }

    // check param size
    unsigned int size = this->paramSize;
    if(size != t->paramSize) {
        return false;
    }

    // check each param type
    for(unsigned int i = 0; i < size; i++) {
        if(!this->paramTypes[i]->equals(t->paramTypes[i])) {
            return false;
        }
    }
    return true;
}

std::string toReifiedTypeName(DSType *templateType, const std::vector<DSType*> &elementTypes) {
    int elementSize = elementTypes.size();
    std::string reifiedTypeName = templateType->getTypeName() + "<";
    for(int i = 0; i < elementSize; i++) {
        if(i > 0) {
            reifiedTypeName += ",";
        }
        reifiedTypeName += elementTypes[i]->getTypeName();
    }
    reifiedTypeName += ">";
    return reifiedTypeName;
}

std::string toFunctionTypeName(DSType *returnType, const std::vector<DSType*> &paramTypes) {
    int paramSize = paramTypes.size();
    std::string funcTypeName = "Func<" + returnType->getTypeName();
    for(int i = 0; i < paramSize; i++) {
        if(i == 0) {
            funcTypeName += ",[";
        }
        if(i > 0) {
            funcTypeName += ",";
        }
        funcTypeName += paramTypes[i]->getTypeName();
        if(i == paramSize - 1) {
            funcTypeName += "]";
        }
    }
    funcTypeName += ">";
    return funcTypeName;
}
