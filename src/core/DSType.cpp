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
#include <core/native_type_info.h>
#include <util/debug.h>

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
    return toReifiedTypeName(typeTemplate->getName(), elementTypes);
}

std::string toReifiedTypeName(const std::string &name, const std::vector<DSType*> &elementTypes) {
    int elementSize = elementTypes.size();
    std::string reifiedTypeName = name + "<";
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

/**
 * builtin type(any, void, value ...)
 * not support override. (if override method, must override DSObject's method)
 * so this->getFieldSize is equivalent to superType->getFieldSize() + infoSize
 */
class BuiltinType: public BaseType {    //FIXME: fieldTable
protected:
    native_type_info_t *info;

    /**
     * may be null, if has no constructor.
     */
    FunctionHandle *constructorHandle;

    /**
     * actually all of handles are FunctionHandle,
     * but initially handles are FieldHandle.
     */
    std::unordered_map<std::string, FieldHandle*> handleMap;

public:
    /**
     * actually superType is BuiltinType
     */
    BuiltinType(std::string &&typeName, bool extendable, DSType *superType,
            native_type_info_t *info) :
            BaseType(std::move(typeName), extendable, superType),
            info(info), constructorHandle(), handleMap() {
        // init function handle
        unsigned int baseIndex = superType != 0 ? superType->getFieldSize() : 0;
        for(unsigned int i = 0; i < info->methodSize; i++) {
            native_func_info_t *funcInfo = info->funcInfos[i];
            auto *handle = new FieldHandle(0, baseIndex + i, true);
            this->handleMap.insert(std::make_pair(std::string(funcInfo->funcName), handle));
        }
        //TODO: init fieldTable
    }

    virtual ~BuiltinType() {
        delete this->constructorHandle;
        this->constructorHandle = 0;

        for(std::pair<std::string, FieldHandle*> pair : this->handleMap) {
            delete pair.second;
        }
        this->handleMap.clear();
    }

    FunctionHandle *getConstructorHandle(TypePool *typePool) { // override
        if(this->constructorHandle == 0 && this->info->initInfo != 0) {
            this->constructorHandle = this->newFuncHandle(typePool, -1, this->info->initInfo);
        }
        return this->constructorHandle;
    }

    unsigned int getFieldSize() {  // override
        if(this->superType != 0) {
            return this->info->methodSize + this->superType->getFieldSize();
        }
        return this->info->methodSize;
    }

    FieldHandle *lookupFieldHandle(TypePool *typePool, const std::string &fieldName) {  // override
        auto iter = this->handleMap.find(fieldName);
        if(iter == this->handleMap.end()) {
            return this->superType != 0 ? this->superType->lookupFieldHandle(typePool, fieldName) : 0;
        }

        /**
         * initialize handle
         */
        auto *handle = iter->second;
        if(dynamic_cast<FunctionHandle*>(handle) == 0) {
            unsigned int baseIndex = this->superType != 0 ? this->superType->getFieldSize() : 0;
            unsigned int infoIndex = handle->getFieldIndex() - baseIndex;

            int fieldIndex = handle->getFieldIndex();
            delete handle;
            handle = this->newFuncHandle(typePool, fieldIndex, this->info->funcInfos[infoIndex]);
            iter->second = handle;
        }
        return handle;
    }

    FieldHandle *findHandle(const std::string &fieldName) { // override
        auto iter = this->handleMap.find(fieldName);
        if(iter != this->handleMap.end()) {
            return iter->second;
        }
        return this->superType != 0 ? this->superType->findHandle(fieldName) : 0;
    }

private:
    virtual FunctionHandle *newFuncHandle(TypePool *typePool, int fieldIndex, native_func_info_t *info) {
        return decodeToFuncHandle(typePool, fieldIndex, info);
    }
};

// #########################
// ##     ReifiedType     ##
// #########################

/**
 * not support override.
 */
class ReifiedType: public BuiltinType {
private:
    /**
     * size is 1 or 2.
     */
    std::vector<DSType*> elementTypes;

public:
    ReifiedType(TypeTemplate *t, DSType *superType, const std::vector<DSType*> &elementTypes):
                BuiltinType(std::string(t->getName()), false, superType, t->getInfo()),
                elementTypes(elementTypes) {
    }

    ~ReifiedType() {
    }

    bool equals(DSType *targetType) { // override
        ReifiedType *type = dynamic_cast<ReifiedType*>(targetType);
        if(type == 0) {
            return false;
        }

        /**
         * check template
         */
        if(this->typeName != type->typeName) {
            return false;
        }

        /**
         * check element size
         */
        unsigned int size = this->elementTypes.size();
        if(size != type->elementTypes.size()) {
            return false;
        }

        /**
         * check each element type
         */
        for(unsigned int i = 0; i < size; i++) {
            if(!this->elementTypes[i]->equals(type->elementTypes[i])) {
                return false;
            }
        }
        return true;
    }

private:
    FunctionHandle *newFuncHandle(TypePool *typePool, int fieldIndex, native_func_info_t *info) {
        switch(this->elementTypes.size()) {
        case 1:
            return decodeToFuncHandle(typePool, fieldIndex, info, this->elementTypes[0]);
        case 2:
            return decodeToFuncHandle(typePool, fieldIndex, info, this->elementTypes[0], this->elementTypes[1]);
        default:
            fatal("element size must be 1 or 2");
        }
        return 0;
    }
};

DSType *newBuiltinType(std::string &&typeName, bool extendable,
        DSType *superType, native_type_info_t *info) {
    return new BuiltinType(std::move(typeName), extendable, superType, info);
}

DSType *newReifiedType(TypeTemplate *t, DSType *superType, const std::vector<DSType*> &elementTypes) {
    return new ReifiedType(t, superType, elementTypes);
}
