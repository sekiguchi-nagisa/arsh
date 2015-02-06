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

#include <core/DSType.h>
#include <core/DSObject.h>

// ####################
// ##     DSType     ##
// ####################

DSType::DSType() {
}

DSType::~DSType() {
}

FunctionHandle *DSType::lookupMethodHandle(TypePool *typePool, const std::string &funcName) {
    FieldHandle *handle = this->lookupFieldHandle(typePool, funcName);
    return handle != 0 ? dynamic_cast<FunctionHandle*>(handle) : 0;
}

bool DSType::isAssignableFrom(DSType *targetType) {
    if(this->equals(targetType)) {
        return true;
    }
    DSType *superType = targetType->getSuperType();
    return superType != 0 && this->isAssignableFrom(superType);
}

// ######################
// ##     BaseType     ##
// ######################

BaseType::BaseType(std::string &&typeName, bool extendable, DSType *superType) :
        typeName(std::move(typeName)), extendable(extendable), superType(superType) {
}

BaseType::~BaseType() {
}

std::string BaseType::getTypeName() {
    return this->typeName;
}

bool BaseType::isExtendable() {
    return this->extendable;
}

DSType *BaseType::getSuperType() {
    return this->superType;
}

bool BaseType::equals(DSType *targetType) {
    BaseType *baseType = dynamic_cast<BaseType*>(targetType);
    return baseType != 0 && this->typeName == baseType->typeName;
}

// #######################
// ##     ClassType     ##
// #######################

ClassType::ClassType(std::string &&className, bool extendable, DSType *superType) :
        BaseType(std::move(className), extendable, superType),
        baseIndex(superType != 0 ? superType->getFieldSize() : 0),
        constructorHandle(0),
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

FunctionHandle *ClassType::getConstructorHandle(TypePool *typePool) {
    return this->constructorHandle;
}

unsigned int ClassType::getFieldSize() {
    return this->handleMap.size() + this->baseIndex;
}

FieldHandle *ClassType::lookupFieldHandle(TypePool *typePool, const std::string &fieldName) {
    auto iter = this->handleMap.find(fieldName);
    if(iter != this->handleMap.end()) {
        return iter->second;
    }
    return this->superType != 0 ? this->superType->lookupFieldHandle(typePool, fieldName) : 0;
}

FieldHandle *ClassType::findHandle(const std::string &fieldName) {
    auto iter = this->handleMap.find(fieldName);
    if(iter != this->handleMap.end()) {
        return iter->second;
    }
    return this->superType != 0 ? superType->findHandle(fieldName) : 0;
}

bool ClassType::addNewFieldHandle(const std::string &fieldName, bool readOnly, DSType *fieldType) {
    if(this->findHandle(fieldName) != 0) {
        return false;
    }
    FieldHandle *handle = new FieldHandle(fieldType, this->getFieldSize(), readOnly);
    this->handleMap[fieldName] = handle;
    return true;
}

FunctionHandle *ClassType::addNewFunctionHandle(const std::string &funcName,
        DSType *returnType, const std::vector<DSType*> &paramTypes) {   //TODO: method override
    if(this->findHandle(funcName) != 0) {
        return 0;
    }
    FunctionHandle *handle = new FunctionHandle(returnType, paramTypes, this->getFieldSize());
    this->handleMap[funcName] = handle;
    return handle;
}

FunctionHandle *ClassType::setNewConstructorHandle(const std::vector<DSType*> &paramTypes) {
    if(this->constructorHandle != 0) {
        delete this->constructorHandle;
    }
    FunctionHandle *handle = new FunctionHandle(0, paramTypes);
    this->constructorHandle = handle;
    return handle;
}

void ClassType::addFunction(FuncObject *func) {
    //TODO:
}

void ClassType::setConstructor(FuncObject *func) {
    //TODO:
}


// ##########################
// ##     FunctionType     ##
// ##########################

FunctionType::FunctionType(DSType *superType, DSType *returnType, const std::vector<DSType*> &paramTypes) :
        superType(superType), returnType(returnType), paramTypes(paramTypes) {
}

FunctionType::~FunctionType() {
    this->paramTypes.clear();
}

DSType *FunctionType::getReturnType() {
    return this->returnType;
}

const std::vector<DSType*> &FunctionType::getParamTypes() {
    return this->paramTypes;
}

DSType *FunctionType::getFirstParamType() {
    return this->paramTypes.size() > 0 ? this->paramTypes[0] : 0;
}

bool FunctionType::treatAsMethod(DSType *targetType) {
    DSType *recvType = this->getFirstParamType();
    return recvType != 0 && recvType->isAssignableFrom(targetType);
}

std::string FunctionType::getTypeName() {
    return toFunctionTypeName(this->returnType, this->paramTypes);
}

bool FunctionType::isExtendable() {
    return false;
}

DSType *FunctionType::getSuperType() {
    return this->superType;
}

FunctionHandle *FunctionType::getConstructorHandle(TypePool *typePool) {
    return 0;
}

unsigned int FunctionType::getFieldSize() {
    return this->superType->getFieldSize();
}

FieldHandle *FunctionType::lookupFieldHandle(TypePool *typePool, const std::string &fieldName) {
    return this->superType->lookupFieldHandle(typePool, fieldName);
}

FieldHandle *FunctionType::findHandle(const std::string &fieldName) {
    return this->superType->findHandle(fieldName);
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
    unsigned int size = this->paramTypes.size();
    if(size != t->paramTypes.size()) {
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

std::string toReifiedTypeName(TypeTemplate *typeTemplate, const std::vector<DSType*> &elementTypes) {
    int elementSize = elementTypes.size();
    std::string reifiedTypeName = typeTemplate->getName() + "<";
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

// #########################
// ##     BuiltinType     ##
// #########################

BuiltinType::BuiltinType(std::string &&typeName, bool extendable, DSType *superType,
        unsigned int infoSize, native_func_info_t **infos) :
        BaseType(std::move(typeName), extendable, superType),
        infoSize(infoSize), infos(infos),
        constructorHandle(), handleMap() {
    unsigned int index = superType != 0 ? superType->getFieldSize() : 0;
    for(unsigned int i = 0; i < infoSize; i++) {
        native_func_info_t *info = infos[i];
        if(i == 0 && info->funcName == 0) { // as constructor
            this->constructorHandle = new LazyInitializedFuncHandle(info, -1);
        } else {
            auto *handle = new LazyInitializedFuncHandle(info, index + i);
            this->handleMap.insert(std::make_pair(std::string(info->funcName), handle));
        }
    }
    //TODO: init fieldTable
}

BuiltinType::~BuiltinType() {
    delete this->constructorHandle;
    this->constructorHandle = 0;

    for(std::pair<std::string, LazyInitializedFuncHandle*> pair : this->handleMap) {
        delete pair.second;
    }
    this->handleMap.clear();
}

FunctionHandle *BuiltinType::getConstructorHandle(TypePool *typePool) {
    if(this->constructorHandle != 0) {
        this->constructorHandle->initialize(typePool, this->infos[0]);
    }
    return this->constructorHandle;
}

unsigned int BuiltinType::getFieldSize() {
    if(this->superType != 0) {
        return this->infoSize + this->superType->getFieldSize();
    }
    return this->infoSize;
}

FieldHandle *BuiltinType::lookupFieldHandle(TypePool *typePool, const std::string &fieldName) {
    auto iter = this->handleMap.find(fieldName);
    if(iter == this->handleMap.end()) {
        return this->superType != 0 ? this->superType->lookupFieldHandle(typePool, fieldName) : 0;
    }

    /**
     * initialize handle
     */
    auto *handle = iter->second;
    unsigned int baseIndex = this->superType != 0 ? this->superType->getFieldSize() : 0;
    unsigned int infoIndex =
            handle->getFieldIndex() - baseIndex + (this->constructorHandle != 0 ? 1 : 0);
    handle->initialize(typePool, this->infos[infoIndex]);
    return handle;
}

FieldHandle *BuiltinType::findHandle(const std::string &fieldName) {
    auto iter = this->handleMap.find(fieldName);
    if(iter != this->handleMap.end()) {
        return iter->second;
    }
    return this->superType != 0 ? this->superType->findHandle(fieldName) : 0;
}
