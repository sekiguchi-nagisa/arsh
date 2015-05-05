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

#include "DSType.h"
#include "DSObject.h"
#include "TypePool.h"
#include "FieldHandle.h"
#include "../misc/debug.h"
#include <assert.h>

namespace ydsh {
namespace core {

// ####################
// ##     DSType     ##
// ####################

DSType::DSType(bool extendable, DSType *superType, bool isVoid) :
        attributeSet(0), superType(superType) {
    if(extendable) {
        setFlag(this->attributeSet, EXTENDABLE);
    }
    if(isVoid) {
        setFlag(this->attributeSet, VOID_TYPE);
    }
}

DSType::~DSType() {
}

bool DSType::isExtendable() const {
    return hasFlag(this->attributeSet, EXTENDABLE);
}

bool DSType::isVoidType() const {
    return hasFlag(this->attributeSet, VOID_TYPE);
}

bool DSType::isFuncType() const {
    return hasFlag(this->attributeSet, FUNC_TYPE);
}

bool DSType::isInterface() const {
    return hasFlag(this->attributeSet, INTERFACE);
}

bool DSType::isBuiltinType() const {
    return false;
}

DSType *DSType::getSuperType() const {
    return this->superType;
}

MethodHandle *DSType::getConstructorHandle(TypePool *typePool) {
    return 0;
}

MethodRef *DSType::getConstructor() {
    return 0;
}

unsigned int DSType::getFieldSize() {
    return this->superType != 0 ? this->superType->getFieldSize() : 0;
}

unsigned int DSType::getMethodSize() {
    return this->superType != 0 ? this->superType->getMethodSize() : 0;
}

FieldHandle *DSType::lookupFieldHandle(TypePool *typePool, const std::string &fieldName) {
    return 0;
}

MethodHandle *DSType::lookupMethodHandle(TypePool *typePool, const std::string &methodName) {
    return 0;
}

bool DSType::operator==(const DSType &type) {
    return (unsigned long) this == (unsigned long) &type;
}

bool DSType::operator!=(const DSType &type) {
    return (unsigned long) this != (unsigned long) &type;
}

bool DSType::isAssignableFrom(DSType *targetType) {
    if(*this == *targetType) {
        return true;
    }
    DSType *superType = targetType->getSuperType();
    return superType != 0 && this->isAssignableFrom(superType);
}

MethodRef *DSType::getMethodRef(unsigned int methodIndex) {
    return this->superType != 0 ? this->superType->getMethodRef(methodIndex) : 0;
}

void DSType::copyAllMethodRef(std::vector<std::shared_ptr<MethodRef>> &methodTable) {
}

// ##########################
// ##     FunctionType     ##
// ##########################

FunctionType::FunctionType(DSType *superType, DSType *returnType,
                           const std::vector<DSType *> &paramTypes) :
        DSType(false, superType, false),
        returnType(returnType), paramTypes(paramTypes) {
    setFlag(this->attributeSet, FUNC_TYPE);
}

FunctionType::~FunctionType() {
    this->paramTypes.clear();
}

DSType *FunctionType::getReturnType() {
    return this->returnType;
}

const std::vector<DSType *> &FunctionType::getParamTypes() {
    return this->paramTypes;
}

MethodHandle *FunctionType::lookupMethodHandle(TypePool *typePool, const std::string &methodName) {
    return this->superType->lookupMethodHandle(typePool, methodName);
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
class BuiltinType : public DSType {
protected:
    native_type_info_t *info;

    /**
     * may be null, if has no constructor.
     */
    MethodHandle *constructorHandle;

    /**
     * may be null, if has no constructor
     */
    std::shared_ptr<MethodRef> constructor;

    std::unordered_map<std::string, MethodHandle *> methodHandleMap;
    std::vector<std::shared_ptr<MethodRef>> methodTable;

public:
    /**
     * actually superType is BuiltinType.
     */
    BuiltinType(bool extendable, DSType *superType,
                native_type_info_t *info, bool isVoid);

    virtual ~BuiltinType();

    MethodHandle *getConstructorHandle(TypePool *typePool); // override
    MethodRef *getConstructor();   // override.
    MethodHandle *lookupMethodHandle(TypePool *typePool, const std::string &methodName);  // override
    FieldHandle *findHandle(const std::string &fieldName); // override
    bool isBuiltinType() const; // override
    unsigned int getMethodSize(); // override
    MethodRef *getMethodRef(unsigned int methodIndex); // override
    void copyAllMethodRef(std::vector<std::shared_ptr<MethodRef>> &methodTable); // override

private:
    virtual void initMethodHandle(MethodHandle *handle, TypePool *typePool, NativeFuncInfo *info);
};

BuiltinType::BuiltinType(bool extendable, DSType *superType,
                         native_type_info_t *info, bool isVoid) :
        DSType(extendable, superType, isVoid),
        info(info), constructorHandle(), constructor(), methodHandleMap(),
        methodTable(superType != 0 ? superType->getMethodSize() + info->methodSize : info->methodSize) {

    // copy super type methodRef to method table
    if(this->superType != 0) {
        this->superType->copyAllMethodRef(this->methodTable);
    }

    // init method handle
    unsigned int baseIndex = superType != 0 ? superType->getMethodSize() : 0;
    for(unsigned int i = 0; i < info->methodSize; i++) {
        NativeFuncInfo *funcInfo = &info->funcInfos[i];
        unsigned int methodIndex = baseIndex + i;
        auto *handle = new MethodHandle(methodIndex);
        this->methodHandleMap.insert(std::make_pair(std::string(funcInfo->funcName), handle));

        // set to method table
        this->methodTable[methodIndex] = std::make_shared<NativeMethodRef>(this->info->funcInfos[i].func_ptr);
    }
}

BuiltinType::~BuiltinType() {
    delete this->constructorHandle;
    this->constructorHandle = 0;

    for(std::pair<std::string, MethodHandle *> pair : this->methodHandleMap) {
        delete pair.second;
    }
    this->methodHandleMap.clear();
}

MethodHandle *BuiltinType::getConstructorHandle(TypePool *typePool) {
    if(this->constructorHandle == 0 && this->info->initInfo != 0) {
        this->constructorHandle = new MethodHandle(0);
        this->initMethodHandle(this->constructorHandle, typePool, this->info->initInfo);
        this->constructor.reset(new NativeMethodRef(this->info->initInfo->func_ptr));
    }
    return this->constructorHandle;
}

MethodRef *BuiltinType::getConstructor() {
    return this->constructor.get();
}

MethodHandle *BuiltinType::lookupMethodHandle(TypePool *typePool, const std::string &methodName) {
    auto iter = this->methodHandleMap.find(methodName);
    if(iter == this->methodHandleMap.end()) {
        return this->superType != 0 ? this->superType->lookupMethodHandle(typePool, methodName) : 0;
    }

    MethodHandle *handle = iter->second;
    if(!handle->initalized()) { // init handle
        unsigned int baseIndex = this->superType != 0 ? this->superType->getMethodSize() : 0;
        unsigned int infoIndex = handle->getMethodIndex() - baseIndex;
        this->initMethodHandle(handle, typePool, &this->info->funcInfos[infoIndex]);
    }
    return handle;
}

FieldHandle *BuiltinType::findHandle(const std::string &fieldName) { // override
//    auto iter = this->methodHandleMap.find(fieldName);
//    if(iter != this->methodHandleMap.end()) {
//        return iter->second;
//    }
    return this->superType != 0 ? this->superType->findHandle(fieldName) : 0;
}

bool BuiltinType::isBuiltinType() const {
    return true;
}

unsigned int BuiltinType::getMethodSize() {
    if(this->superType != 0) {
        return this->superType->getMethodSize() + this->methodHandleMap.size();
    }
    return this->methodHandleMap.size();
}

MethodRef *BuiltinType::getMethodRef(unsigned int methodIndex) {
    return this->methodTable[methodIndex].get();
}

void BuiltinType::copyAllMethodRef(std::vector<std::shared_ptr<MethodRef>> &methodTable) {
    unsigned int size = this->getMethodSize();
    assert(size <= methodTable.size());

    for(unsigned int i = 0; i < size; i++) {
        methodTable[i] = this->methodTable[i];
    }
}

void BuiltinType::initMethodHandle(MethodHandle *handle, TypePool *typePool, NativeFuncInfo *info) {
    handle->init(typePool, info);
}

// #########################
// ##     ReifiedType     ##
// #########################

/**
 * not support override.
 */
class ReifiedType : public BuiltinType {
private:
    /**
     * size is 1 or 2.
     */
    std::vector<DSType *> elementTypes;

public:
    ReifiedType(native_type_info_t *info, DSType *superType, const std::vector<DSType *> &elementTypes);

    ~ReifiedType();

    std::string getTypeName() const; // override
    bool equals(DSType *targetType); // override

private:
    void initMethodHandle(MethodHandle *handle, TypePool *typePool, NativeFuncInfo *info); // override
};

ReifiedType::ReifiedType(native_type_info_t *info, DSType *superType,
                         const std::vector<DSType *> &elementTypes) :
        BuiltinType(false, superType, info, false), elementTypes(elementTypes) {
}

ReifiedType::~ReifiedType() {
}

void ReifiedType::initMethodHandle(MethodHandle *handle, TypePool *typePool, NativeFuncInfo *info) {
    switch(this->elementTypes.size()) {
    case 1:
        handle->init(typePool, info, this->elementTypes[0]);
        break;
    case 2:
        handle->init(typePool, info, this->elementTypes[0], this->elementTypes[1]);
        break;
    default:
        fatal("element size must be 1 or 2");
    }
}

DSType *newBuiltinType(bool extendable,
                       DSType *superType, native_type_info_t *info, bool isVoid) {
    return new BuiltinType(extendable, superType, info, isVoid);
}

DSType *newReifiedType(native_type_info_t *info,
                       DSType *superType, const std::vector<DSType *> &elementTypes) {
    return new ReifiedType(info, superType, elementTypes);
}

// #######################
// ##     TupleType     ##
// #######################

TupleType::TupleType(DSType *superType, const std::vector<DSType *> &types) :
        DSType(false, superType, false), types(types),
        fieldHandleMap(), constructorHandle() {
    unsigned int size = this->types.size();
    unsigned int baseIndex = this->superType->getFieldSize();
    for(unsigned int i = 0; i < size; i++) {
        FieldHandle *handle = new FieldHandle(this->types[i], i + baseIndex, false);
        this->fieldHandleMap.insert(std::make_pair("_" + std::to_string(i), handle));
    }
}

TupleType::~TupleType() {
    for(auto pair : this->fieldHandleMap) {
        delete pair.second;
    }
    this->fieldHandleMap.clear();

    delete this->constructorHandle;
    this->constructorHandle = 0;
}

MethodHandle *TupleType::getConstructorHandle(TypePool *typePool) {
    if(this->types.size() == 1 && this->constructorHandle == 0) {
        this->constructorHandle = new MethodHandle(0);
        this->constructorHandle->init(typePool, funcInfo, this->types[0]);
    }
    return this->constructorHandle;
}

MethodRef *TupleType::getConstructor() {
    return initRef.get();
}

bool TupleType::isBuiltinType() const {
    return true;
}

unsigned int TupleType::getFieldSize() {
    return this->types.size();
}

FieldHandle *TupleType::lookupFieldHandle(TypePool *typePool, const std::string &fieldName) {
    auto iter = this->fieldHandleMap.find(fieldName);
    if(iter == this->fieldHandleMap.end()) {
        return this->superType->lookupFieldHandle(typePool, fieldName);
    }
    return iter->second;
}

FieldHandle *TupleType::findHandle(const std::string &fieldName) {
    auto iter = this->fieldHandleMap.find(fieldName);
    if(iter == this->fieldHandleMap.end()) {
        return this->superType->findHandle(fieldName);
    }
    return iter->second;
}

NativeFuncInfo *TupleType::funcInfo = 0;
std::shared_ptr<MethodRef> TupleType::initRef;

void TupleType::registerFuncInfo(NativeFuncInfo *info) {
    if(funcInfo == 0) {
        funcInfo = info;
        initRef.reset(new NativeMethodRef(info->func_ptr));
    }
}

DSType *newTupleType(DSType *superType, const std::vector<DSType *> &elementTypes) {
    return new TupleType(superType, elementTypes);
}

// ###########################
// ##     InterfaceType     ##
// ###########################

InterfaceType::InterfaceType(DSType *superType) :
        DSType(false, superType, false), fieldHandleMap(), methodHandleMap() {
    setFlag(this->attributeSet, INTERFACE);
}

InterfaceType::~InterfaceType() {
    for(auto &pair : this->fieldHandleMap) {
        delete pair.second;
    }
    this->fieldHandleMap.clear();

    for(auto &pair : this->methodHandleMap) {
        delete pair.second;
    }
    this->methodHandleMap.clear();
}

FieldHandle *InterfaceType::newFieldHandle(const std::string &fieldName, DSType *fieldType, bool readOnly) {
    // field index is always 0.
    FieldHandle *handle = new FieldHandle(fieldType, 0, readOnly);
    handle->setAttribute(FieldHandle::INTERFACE);
    auto pair = this->fieldHandleMap.insert(std::make_pair(fieldName, handle));
    if(pair.second) {
        return handle;
    } else {
        delete handle;
        return nullptr;
    }
}

MethodHandle *InterfaceType::newMethodHandle(const std::string &methodName) {
    MethodHandle *handle = new MethodHandle(0);
    handle->setAttribute(MethodHandle::INTERFACE);
    auto pair = this->methodHandleMap.insert(std::make_pair(methodName, handle));
    if(!pair.second) {
        handle->setNext(pair.first->second);
        pair.first->second = handle;
    }
    return handle;
}

unsigned int InterfaceType::getFieldSize() {
    return this->superType->getFieldSize() + this->fieldHandleMap.size();
}

unsigned int InterfaceType::getMethodSize() {
    return this->superType->getMethodSize() + this->methodHandleMap.size();
}

FieldHandle *InterfaceType::lookupFieldHandle(TypePool *typePool, const std::string &fieldName) {
    auto iter = this->fieldHandleMap.find(fieldName);
    if(iter == this->fieldHandleMap.end()) {
        return this->superType->lookupFieldHandle(typePool, fieldName);
    }
    return iter->second;
}

MethodHandle *InterfaceType::lookupMethodHandle(TypePool *typePool, const std::string &methodName) {
    auto iter = this->methodHandleMap.find(methodName);
    if(iter == this->methodHandleMap.end()) {
        return this->superType->lookupMethodHandle(typePool, methodName);
    }

    //FIXME:
    return iter->second;
}

FieldHandle *InterfaceType::findHandle(const std::string &fieldName) {
    return this->superType->findHandle(fieldName);
}

// #######################
// ##     ErrorType     ##
// #######################

ErrorType::ErrorType(DSType *superType) :
    DSType(true, superType, false), constructorHandle() {
}

ErrorType::~ErrorType() {
    delete this->constructorHandle;
    this->constructorHandle = 0;
}

NativeFuncInfo *ErrorType::funcInfo = 0;
std::shared_ptr<MethodRef> ErrorType::initRef;

MethodHandle *ErrorType::getConstructorHandle(TypePool *typePool) {
    if(this->constructorHandle == 0) {
        this->constructorHandle = new MethodHandle(0);
        this->constructorHandle->init(typePool, funcInfo);
        this->constructorHandle->setRecvType(this);
    }
    return this->constructorHandle;
}

MethodRef *ErrorType::getConstructor() {
    return initRef.get();
}

bool ErrorType::isBuiltinType() const {
    return true;
}

unsigned int ErrorType::getFieldSize() {
    return this->superType->getFieldSize();
}

FieldHandle *ErrorType::lookupFieldHandle(TypePool *typePool, const std::string &fieldName) {
    return this->superType->lookupFieldHandle(typePool, fieldName);
}

MethodHandle *ErrorType::lookupMethodHandle(TypePool *typePool, const std::string &methodName) {
    return this->superType->lookupMethodHandle(typePool, methodName);
}

FieldHandle *ErrorType::findHandle(const std::string &fieldName) {
    return this->superType->findHandle(fieldName);
}

/**
 * call only once.
 */
void ErrorType::registerFuncInfo(NativeFuncInfo *info) {
    if(funcInfo == 0) {
        funcInfo = info;
        initRef.reset(new NativeMethodRef(info->func_ptr));
    }
}

} // namespace core
} // namespace ydsh