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

DSType::DSType(type_id_t id, bool extendable, bool isVoid) :
        id(id), attributeSet(0) {
    if(extendable) {
        setFlag(this->attributeSet, EXTENDABLE);
    }
    if(isVoid) {
        setFlag(this->attributeSet, VOID_TYPE);
    }
}

DSType::~DSType() {
}

type_id_t DSType::getTypeId() const {
    return this->id;
}

bool DSType::isExtendable() const {
    return hasFlag(this->attributeSet, EXTENDABLE);
}

bool DSType::isVoidType() const {
    return hasFlag(this->attributeSet, VOID_TYPE);
}

FunctionHandle *DSType::lookupMethodHandle(TypePool *typePool, const std::string &funcName) {
    FieldHandle *handle = this->lookupFieldHandle(typePool, funcName);
    return handle != 0 ? dynamic_cast<FunctionHandle*>(handle) : 0;
}

bool DSType::operator==(const DSType &type) {
    return this->id == type.id;
}

bool DSType::isAssignableFrom(DSType *targetType) {
    if(*this == *targetType) {
        return true;
    }
    DSType *superType = targetType->getSuperType();
    return superType != 0 && this->isAssignableFrom(superType);
}

// ######################
// ##     BaseType     ##
// ######################

BaseType::BaseType(type_id_t id, bool extendable, DSType *superType, bool isVoid) :
        DSType(id, extendable, isVoid), superType(superType) {
}

BaseType::~BaseType() {
}

DSType *BaseType::getSuperType() {
    return this->superType;
}

// #######################
// ##     ClassType     ##
// #######################

ClassType::ClassType(type_id_t id, bool extendable, DSType *superType) :
        BaseType(id, extendable, superType, false),
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

FunctionType::FunctionType(type_id_t id, DSType *superType, DSType *returnType, const std::vector<DSType*> &paramTypes) :
        DSType(id, false, false),
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
    BuiltinType(type_id_t id, bool extendable, DSType *superType,
            native_type_info_t *info, bool isVoid);
    virtual ~BuiltinType();

    FunctionHandle *getConstructorHandle(TypePool *typePool); // override
    unsigned int getFieldSize();  // override
    FieldHandle *lookupFieldHandle(TypePool *typePool, const std::string &fieldName);  // override
    FieldHandle *findHandle(const std::string &fieldName); // override

private:
    virtual FunctionHandle *newFuncHandle(TypePool *typePool, int fieldIndex, native_func_info_t *info);
};

BuiltinType::BuiltinType(type_id_t id, bool extendable, DSType *superType,
        native_type_info_t *info, bool isVoid) :
        BaseType(id, extendable, superType, isVoid),
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

BuiltinType::~BuiltinType() {
    delete this->constructorHandle;
    this->constructorHandle = 0;

    for(std::pair<std::string, FieldHandle*> pair : this->handleMap) {
        delete pair.second;
    }
    this->handleMap.clear();
}

FunctionHandle *BuiltinType::getConstructorHandle(TypePool *typePool) {
    if(this->constructorHandle == 0 && this->info->initInfo != 0) {
        this->constructorHandle = this->newFuncHandle(typePool, -1, this->info->initInfo);
    }
    return this->constructorHandle;
}

unsigned int BuiltinType::getFieldSize() {
    if(this->superType != 0) {
        return this->info->methodSize + this->superType->getFieldSize();
    }
    return this->info->methodSize;
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

FieldHandle *BuiltinType::findHandle(const std::string &fieldName) { // override
    auto iter = this->handleMap.find(fieldName);
    if(iter != this->handleMap.end()) {
        return iter->second;
    }
    return this->superType != 0 ? this->superType->findHandle(fieldName) : 0;
}

FunctionHandle *BuiltinType::newFuncHandle(TypePool *typePool, int fieldIndex, native_func_info_t *info) {
    return decodeToFuncHandle(typePool, fieldIndex, info);
}

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
    ReifiedType(type_id_t id, native_type_info_t *info, DSType *superType, const std::vector<DSType*> &elementTypes);
    ~ReifiedType();

    std::string getTypeName() const; // override
    bool equals(DSType *targetType); // override

private:
    FunctionHandle *newFuncHandle(TypePool *typePool, int fieldIndex, native_func_info_t *info); // override
};

ReifiedType::ReifiedType(type_id_t id, native_type_info_t *info, DSType *superType, const std::vector<DSType*> &elementTypes):
        BuiltinType(id, false, superType, info, false), elementTypes(elementTypes) {
}

ReifiedType::~ReifiedType() {
}

FunctionHandle *ReifiedType::newFuncHandle(TypePool *typePool, int fieldIndex, native_func_info_t *info) {
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

DSType *newBuiltinType(type_id_t id, bool extendable,
        DSType *superType, native_type_info_t *info, bool isVoid) {
    return new BuiltinType(id, extendable, superType, info, isVoid);
}

DSType *newReifiedType(type_id_t id, native_type_info_t *info,
        DSType *superType, const std::vector<DSType*> &elementTypes) {
    return new ReifiedType(id, info, superType, elementTypes);
}

// #######################
// ##     TupleType     ##
// #######################

class TupleType: public DSType {
private:
    DSType *superType;
    std::vector<DSType*> types;
    std::unordered_map<std::string, FieldHandle*> handleMap;

public:
    /**
     * superType is always AnyType
     */
    TupleType(type_id_t id, DSType *superType, const std::vector<DSType*> &types);
    ~TupleType();

    /**
     * return always AnyType
     */
    DSType *getSuperType(); // override

    /**
     * return always null
     */
    FunctionHandle *getConstructorHandle(TypePool *typePool); // override

    /**
     * return always type.size() + superType->getFieldSize()
     */
    unsigned int getFieldSize(); // override

    FieldHandle *lookupFieldHandle(TypePool *typePool, const std::string &fieldName); // override
    FieldHandle *findHandle(const std::string &fieldName); // override
    bool equals(DSType *targetType); // override
};

TupleType::TupleType(type_id_t id, DSType *superType, const std::vector<DSType*> &types) :
        DSType(id, false, false), superType(superType), types(types), handleMap() {
    unsigned int size = this->types.size();
    unsigned int baseIndex = this->superType->getFieldSize();
    for(unsigned int i = 0; i < size; i++) {
        FieldHandle *handle = new FieldHandle(this->types[i], i + baseIndex, false);
        this->handleMap.insert(std::make_pair("_" + std::to_string(i), handle));
    }
}

TupleType::~TupleType() {
    for(auto pair : this->handleMap) {
        delete pair.second;
    }
    this->handleMap.clear();
}

DSType *TupleType::getSuperType() {
    return this->superType;
}

FunctionHandle *TupleType::getConstructorHandle(TypePool *typePool) {
    return 0;
}

unsigned int TupleType::getFieldSize() {
    return this->superType->getFieldSize() + this->types.size();
}

FieldHandle *TupleType::lookupFieldHandle(TypePool *typePool, const std::string &fieldName) {
    auto iter = this->handleMap.find(fieldName);
    if(iter == this->handleMap.end()) {
        return this->superType->lookupFieldHandle(typePool, fieldName);
    }
    return iter->second;
}

FieldHandle *TupleType::findHandle(const std::string &fieldName) {
    auto iter = this->handleMap.find(fieldName);
    if(iter == this->handleMap.end()) {
        return this->superType->findHandle(fieldName);
    }
    return iter->second;
}

DSType *newTupleType(type_id_t id, DSType *superType, const std::vector<DSType*> &elementTypes) {
    return new TupleType(id, superType, elementTypes);
}
