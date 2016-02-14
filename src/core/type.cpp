/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#include <cassert>

#include "type.h"
#include "object.h"
#include "handle.h"

namespace ydsh {
namespace core {

extern NativeFuncInfo *const nativeFuncInfoTable;

// ####################
// ##     DSType     ##
// ####################

DSType::DSType(bool extendible, DSType *superType, bool isVoid) :
        attributeSet(0), superType(superType) {
    if(extendible) {
        setFlag(this->attributeSet, EXTENDIBLE);
    }
    if(isVoid) {
        setFlag(this->attributeSet, VOID_TYPE);
    }
}

MethodHandle *DSType::getConstructorHandle(TypePool &) {
    return nullptr;
}

const MethodRef *DSType::getConstructor() {
    return nullptr;
}

unsigned int DSType::getFieldSize() {
    return this->superType != nullptr ? this->superType->getFieldSize() : 0;
}

unsigned int DSType::getMethodSize() {
    return this->superType != nullptr ? this->superType->getMethodSize() : 0;
}

FieldHandle *DSType::lookupFieldHandle(TypePool &, const std::string &) {
    return nullptr;
}

MethodHandle *DSType::lookupMethodHandle(TypePool &, const std::string &) {
    return nullptr;
}

bool DSType::isSameOrBaseTypeOf(const DSType &targetType) const {
    if(*this == targetType) {
        return true;
    }
    DSType *superType = targetType.getSuperType();
    return superType != nullptr && this->isSameOrBaseTypeOf(*superType);
}

MethodRef *DSType::getMethodRef(unsigned int methodIndex) {
    return this->superType != nullptr ? this->superType->getMethodRef(methodIndex) : 0;
}

void DSType::copyAllMethodRef(std::vector<MethodRef> &) {
}

// ##########################
// ##     FunctionType     ##
// ##########################

MethodHandle *FunctionType::lookupMethodHandle(TypePool &typePool, const std::string &methodName) {
    return this->superType->lookupMethodHandle(typePool, methodName);
}

FieldHandle *FunctionType::findHandle(const std::string &fieldName) {
    return this->superType->findHandle(fieldName);
}

void FunctionType::accept(TypeVisitor *visitor) {
    visitor->visitFunctionType(this);
}

// ################################
// ##     native_type_info_t     ##
// ################################

NativeFuncInfo &native_type_info_t::getMethodInfo(unsigned int index) {
    return nativeFuncInfoTable[this->offset + this->constructorSize + index];
}

/**
 * not call it if constructorSize is 0
 */
NativeFuncInfo &native_type_info_t::getInitInfo() {
    return nativeFuncInfoTable[this->offset];
}


// #########################
// ##     BuiltinType     ##
// #########################

BuiltinType::BuiltinType(bool extendable, DSType *superType,
                         native_type_info_t info, bool isVoid) :
        DSType(extendable, superType, isVoid),
        info(info), constructorHandle(), constructor(), methodHandleMap(),
        methodTable(superType != nullptr ? superType->getMethodSize() + info.methodSize : info.methodSize) {

    // copy super type methodRef to method table
    if(this->superType != nullptr) {
        this->superType->copyAllMethodRef(this->methodTable);
    }

    // init method handle
    unsigned int baseIndex = superType != nullptr ? superType->getMethodSize() : 0;
    for(unsigned int i = 0; i < info.methodSize; i++) {
        NativeFuncInfo *funcInfo = &info.getMethodInfo(i);
        unsigned int methodIndex = baseIndex + i;
        auto *handle = new MethodHandle(methodIndex);
        this->methodHandleMap.insert(std::make_pair(std::string(funcInfo->funcName), handle));

        // set to method table
        this->methodTable[methodIndex] = MethodRef(this->info.getMethodInfo(i).func_ptr);
    }

    setFlag(this->attributeSet, BUILTIN_TYPE);
}

BuiltinType::~BuiltinType() {
    delete this->constructorHandle;

    for(std::pair<std::string, MethodHandle *> pair : this->methodHandleMap) {
        delete pair.second;
    }
}

MethodHandle *BuiltinType::getConstructorHandle(TypePool &typePool) {
    if(this->constructorHandle == nullptr && this->info.constructorSize != 0) {
        this->constructorHandle = new MethodHandle(0);
        this->initMethodHandle(this->constructorHandle, typePool, this->info.getInitInfo());
        this->constructor = MethodRef(this->info.getInitInfo().func_ptr);
    }
    return this->constructorHandle;
}

const MethodRef *BuiltinType::getConstructor() {
    return &this->constructor;
}

MethodHandle *BuiltinType::lookupMethodHandle(TypePool &typePool, const std::string &methodName) {
    auto iter = this->methodHandleMap.find(methodName);
    if(iter == this->methodHandleMap.end()) {
        return this->superType != nullptr ? this->superType->lookupMethodHandle(typePool, methodName) : 0;
    }

    MethodHandle *handle = iter->second;
    if(!handle->initialized()) { // init handle
        unsigned int baseIndex = this->superType != nullptr ? this->superType->getMethodSize() : 0;
        unsigned int infoIndex = handle->getMethodIndex() - baseIndex;
        this->initMethodHandle(handle, typePool, this->info.getMethodInfo(infoIndex));
    }
    return handle;
}

FieldHandle *BuiltinType::findHandle(const std::string &fieldName) { // override
//    auto iter = this->methodHandleMap.find(fieldName);
//    if(iter != this->methodHandleMap.end()) {
//        return iter->second;
//    }
    return this->superType != nullptr ? this->superType->findHandle(fieldName) : nullptr;
}

void BuiltinType::accept(TypeVisitor *visitor) {
    visitor->visitBuiltinType(this);
}

unsigned int BuiltinType::getMethodSize() {
    if(this->superType != nullptr) {
        return this->superType->getMethodSize() + this->methodHandleMap.size();
    }
    return this->methodHandleMap.size();
}

MethodRef *BuiltinType::getMethodRef(unsigned int methodIndex) {
    return &this->methodTable[methodIndex];
}

void BuiltinType::copyAllMethodRef(std::vector<MethodRef> &methodTable) {
    unsigned int size = this->getMethodSize();
    assert(size <= methodTable.size());

    for(unsigned int i = 0; i < size; i++) {
        methodTable[i] = this->methodTable[i];
    }
}

void BuiltinType::initMethodHandle(MethodHandle *handle, TypePool &typePool, NativeFuncInfo &info) {
    handle->init(typePool, info);
}

// #########################
// ##     ReifiedType     ##
// #########################

void ReifiedType::initMethodHandle(MethodHandle *handle, TypePool &typePool, NativeFuncInfo &info) {
    handle->init(typePool, info, &this->elementTypes);
}

void ReifiedType::accept(TypeVisitor *visitor) {
    visitor->visitReifiedType(this);
}

// #######################
// ##     TupleType     ##
// #######################

TupleType::TupleType(native_type_info_t info, DSType *superType, std::vector<DSType *> &&types) :
        ReifiedType(info, superType, std::move(types)), fieldHandleMap() {
    const unsigned int size = this->elementTypes.size();
    const unsigned int baseIndex = this->superType->getFieldSize();
    for(unsigned int i = 0; i < size; i++) {
        FieldHandle *handle = new FieldHandle(this->elementTypes[i], i + baseIndex, false);
        this->fieldHandleMap.insert(std::make_pair("_" + std::to_string(i), handle));
    }
}

TupleType::~TupleType() {
    for(auto pair : this->fieldHandleMap) {
        delete pair.second;
    }
}

MethodHandle *TupleType::getConstructorHandle(TypePool &typePool) {
    if(this->elementTypes.size() == 1 && this->constructorHandle == nullptr) {
        this->constructorHandle = new MethodHandle(0);
        this->initMethodHandle(this->constructorHandle, typePool, this->info.getInitInfo());
        this->constructor = MethodRef(this->info.getInitInfo().func_ptr);
    }
    return this->constructorHandle;
}

unsigned int TupleType::getFieldSize() {
    return this->elementTypes.size();
}

FieldHandle *TupleType::lookupFieldHandle(TypePool &typePool, const std::string &fieldName) {
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

void TupleType::accept(TypeVisitor *visitor) {
    visitor->visitTupleType(this);
}


// ###########################
// ##     InterfaceType     ##
// ###########################

InterfaceType::~InterfaceType() {
    for(auto &pair : this->fieldHandleMap) {
        delete pair.second;
    }

    for(auto &pair : this->methodHandleMap) {
        delete pair.second;
    }
}

FieldHandle *InterfaceType::newFieldHandle(const std::string &fieldName, DSType &fieldType, bool readOnly) {
    // field index is always 0.
    FieldHandle *handle = new FieldHandle(&fieldType, 0, readOnly);
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

FieldHandle *InterfaceType::lookupFieldHandle(TypePool &typePool, const std::string &fieldName) {
    auto iter = this->fieldHandleMap.find(fieldName);
    if(iter == this->fieldHandleMap.end()) {
        return this->superType->lookupFieldHandle(typePool, fieldName);
    }
    return iter->second;
}

MethodHandle *InterfaceType::lookupMethodHandle(TypePool &typePool, const std::string &methodName) {
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

void InterfaceType::accept(TypeVisitor *visitor) {
    visitor->visitInterfaceType(this);
}

// #######################
// ##     ErrorType     ##
// #######################

ErrorType::~ErrorType() {
    delete this->constructorHandle;
}

NativeFuncInfo *ErrorType::funcInfo = nullptr;
MethodRef ErrorType::initRef;

MethodHandle *ErrorType::getConstructorHandle(TypePool &typePool) {
    if(this->constructorHandle == nullptr) {
        this->constructorHandle = new MethodHandle(0);
        this->constructorHandle->init(typePool, *funcInfo);
        this->constructorHandle->setRecvType(*this);
    }
    return this->constructorHandle;
}

const MethodRef *ErrorType::getConstructor() {
    return &initRef;
}

unsigned int ErrorType::getFieldSize() {
    return this->superType->getFieldSize();
}

FieldHandle *ErrorType::lookupFieldHandle(TypePool &typePool, const std::string &fieldName) {
    return this->superType->lookupFieldHandle(typePool, fieldName);
}

MethodHandle *ErrorType::lookupMethodHandle(TypePool &typePool, const std::string &methodName) {
    return this->superType->lookupMethodHandle(typePool, methodName);
}

FieldHandle *ErrorType::findHandle(const std::string &fieldName) {
    return this->superType->findHandle(fieldName);
}

void ErrorType::accept(TypeVisitor *visitor) {
    visitor->visitErrorType(this);
}

/**
 * call only once.
 */
void ErrorType::registerFuncInfo(NativeFuncInfo &info) {
    if(funcInfo == nullptr) {
        funcInfo = &info;
        initRef = MethodRef(info.func_ptr);
    }
}

} // namespace core
} // namespace ydsh