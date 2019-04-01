/*
 * Copyright (C) 2015-2018 Nagisa Sekiguchi
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

#include <cstdarg>
#include <array>
#include <cerrno>

#include "type.h"
#include "object.h"
#include "handle.h"
#include "tcerror.h"
#include "core.h"

namespace ydsh {

const NativeFuncInfo *nativeFuncInfoTable();

struct NativeCode;
const NativeCode *getNativeCode(unsigned int index);

// ####################
// ##     DSType     ##
// ####################

MethodHandle *DSType::getConstructorHandle(SymbolTable &) {
    return nullptr;
}

const DSCode *DSType::getConstructor() {
    return nullptr;
}

unsigned int DSType::getFieldSize() {
    return this->superType != nullptr ? this->superType->getFieldSize() : 0;
}

unsigned int DSType::getMethodSize() {
    return this->superType != nullptr ? this->superType->getMethodSize() : 0;
}

FieldHandle *DSType::lookupFieldHandle(SymbolTable &, const std::string &) {
    return nullptr;
}

MethodHandle *DSType::lookupMethodHandle(SymbolTable &, const std::string &) {
    return nullptr;
}

bool DSType::isSameOrBaseTypeOf(const DSType &targetType) const {
    if(*this == targetType) {
        return true;
    }
    if(targetType.isNothingType()) {
        return true;
    }
    if(this->isOptionType()) {
        return static_cast<const ReifiedType *>(this)->getElementTypes()[0]->isSameOrBaseTypeOf(targetType);
    }
    DSType *superType = targetType.getSuperType();
    return superType != nullptr && this->isSameOrBaseTypeOf(*superType);
}

const DSCode *DSType::getMethodRef(unsigned int methodIndex) {
    return this->superType != nullptr ? this->superType->getMethodRef(methodIndex) : nullptr;
}

void DSType::copyAllMethodRef(std::vector<const DSCode *> &) {
}

// ##########################
// ##     FunctionType     ##
// ##########################

MethodHandle *FunctionType::lookupMethodHandle(SymbolTable &symbolTable, const std::string &methodName) {
    return this->superType->lookupMethodHandle(symbolTable, methodName);
}

// ################################
// ##     native_type_info_t     ##
// ################################

const NativeFuncInfo &native_type_info_t::getMethodInfo(unsigned int index) const {
    return nativeFuncInfoTable()[this->offset + this->constructorSize + index];
}

/**
 * not call it if constructorSize is 0
 */
const NativeFuncInfo &native_type_info_t::getInitInfo() const {
    return nativeFuncInfoTable()[this->offset];
}

static const NativeCode *getCode(native_type_info_t info, unsigned int index) {
    return getNativeCode(info.offset + info.constructorSize + index);
}

static const NativeCode *getCode(native_type_info_t info) {
    return getNativeCode(info.offset);
}


// #########################
// ##     BuiltinType     ##
// #########################

BuiltinType::BuiltinType(unsigned int id, DSType *superType, native_type_info_t info, flag8_set_t attribute) :
        DSType(id, superType, attribute),
        info(info), constructorHandle(), constructor(),
        methodTable(superType != nullptr ? superType->getMethodSize() + info.methodSize : info.methodSize) {

    // copy super type methodRef to method table
    if(this->superType != nullptr) {
        this->superType->copyAllMethodRef(this->methodTable);
    }

    // init method handle
    unsigned int baseIndex = superType != nullptr ? superType->getMethodSize() : 0;
    for(unsigned int i = 0; i < info.methodSize; i++) {
        const NativeFuncInfo *funcInfo = &info.getMethodInfo(i);
        unsigned int methodIndex = baseIndex + i;
        auto *handle = new MethodHandle(methodIndex);
        this->methodHandleMap.insert(std::make_pair(std::string(funcInfo->funcName), handle));

        // set to method table
        this->methodTable[methodIndex] = getCode(this->info, i);
    }
}

BuiltinType::~BuiltinType() {
    delete this->constructorHandle;

    for(auto &pair : this->methodHandleMap) {
        delete pair.second;
    }
}

MethodHandle *BuiltinType::getConstructorHandle(SymbolTable &symbolTable) {
    if(this->constructorHandle == nullptr && this->info.constructorSize != 0) {
        this->constructorHandle = new MethodHandle(0);
        if(!this->initMethodHandle(this->constructorHandle, symbolTable, this->info.getInitInfo())) {
            return nullptr;
        }
        this->constructor = getCode(this->info);
    }
    return this->constructorHandle;
}

const DSCode *BuiltinType::getConstructor() {
    return this->constructor;
}

MethodHandle *BuiltinType::lookupMethodHandle(SymbolTable &symbolTable, const std::string &methodName) {
    auto iter = this->methodHandleMap.find(methodName);
    if(iter == this->methodHandleMap.end()) {
        return this->superType != nullptr ? this->superType->lookupMethodHandle(symbolTable, methodName) : nullptr;
    }

    MethodHandle *handle = iter->second;
    if(!handle->initialized()) { // init handle
        unsigned int baseIndex = this->superType != nullptr ? this->superType->getMethodSize() : 0;
        unsigned int infoIndex = handle->getMethodIndex() - baseIndex;
        if(!this->initMethodHandle(handle, symbolTable, this->info.getMethodInfo(infoIndex))) {
            return nullptr;
        }
    }
    return handle;
}

unsigned int BuiltinType::getMethodSize() {
    if(this->superType != nullptr) {
        return this->superType->getMethodSize() + this->methodHandleMap.size();
    }
    return this->methodHandleMap.size();
}

const DSCode *BuiltinType::getMethodRef(unsigned int methodIndex) {
    return this->methodTable[methodIndex];
}

void BuiltinType::copyAllMethodRef(std::vector<const DSCode *> &methodTable) {
    unsigned int size = this->getMethodSize();
    assert(size <= methodTable.size());

    for(unsigned int i = 0; i < size; i++) {
        methodTable[i] = this->methodTable[i];
    }
}

bool BuiltinType::initMethodHandle(MethodHandle *handle, SymbolTable &symbolTable, const NativeFuncInfo &info) {
    return handle->init(symbolTable, info);
}

// #########################
// ##     ReifiedType     ##
// #########################

bool ReifiedType::initMethodHandle(MethodHandle *handle, SymbolTable &symbolTable, const NativeFuncInfo &info) {
    return handle->init(symbolTable, info, &this->elementTypes);
}

// #######################
// ##     TupleType     ##
// #######################

TupleType::TupleType(unsigned int id, native_type_info_t info, DSType *superType, std::vector<DSType *> &&types) :
        ReifiedType(id, info, superType, std::move(types)) {
    const unsigned int size = this->elementTypes.size();
    const unsigned int baseIndex = this->superType->getFieldSize();
    for(unsigned int i = 0; i < size; i++) {
        FieldHandle *handle = new FieldHandle(this->elementTypes[i], i + baseIndex, FieldAttributes());
        this->fieldHandleMap.insert(std::make_pair("_" + std::to_string(i), handle));
    }
}

TupleType::~TupleType() {
    for(auto &pair : this->fieldHandleMap) {
        delete pair.second;
    }
}

unsigned int TupleType::getFieldSize() {
    return this->elementTypes.size();
}

FieldHandle *TupleType::lookupFieldHandle(SymbolTable &symbolTable, const std::string &fieldName) {
    auto iter = this->fieldHandleMap.find(fieldName);
    if(iter == this->fieldHandleMap.end()) {
        return this->superType->lookupFieldHandle(symbolTable, fieldName);
    }
    return iter->second;
}

// #######################
// ##     ErrorType     ##
// #######################

ErrorType::~ErrorType() {
    delete this->constructorHandle;
}

const NativeFuncInfo *ErrorType::funcInfo = nullptr;
const DSCode *ErrorType::initRef;

MethodHandle *ErrorType::getConstructorHandle(SymbolTable &symbolTable) {
    if(this->constructorHandle == nullptr) {
        this->constructorHandle = new MethodHandle(0);
        this->constructorHandle->init(symbolTable, *funcInfo);
        this->constructorHandle->setRecvType(*this);
    }
    return this->constructorHandle;
}

const DSCode *ErrorType::getConstructor() {
    return initRef;
}

unsigned int ErrorType::getFieldSize() {
    return this->superType->getFieldSize();
}

FieldHandle *ErrorType::lookupFieldHandle(SymbolTable &symbolTable, const std::string &fieldName) {
    return this->superType->lookupFieldHandle(symbolTable, fieldName);
}

MethodHandle *ErrorType::lookupMethodHandle(SymbolTable &symbolTable, const std::string &methodName) {
    return this->superType->lookupMethodHandle(symbolTable, methodName);
}

/**
 * call only once.
 */
void ErrorType::registerFuncInfo(native_type_info_t info) {
    if(funcInfo == nullptr) {
        funcInfo = &info.getInitInfo();
        initRef = getCode(info);
    }
}

std::unique_ptr<TypeLookupError> createTLErrorImpl(const char *kind, const char *fmt, ...) {
    va_list arg;

    va_start(arg, fmt);
    char *str = nullptr;
    if(vasprintf(&str, fmt, arg) == -1) { abort(); }
    va_end(arg);

    auto error = std::make_unique<TypeLookupError>(kind, str);
    free(str);
    return error;
}

TypeCheckError createTCErrorImpl(const Node &node, const char *kind, const char *fmt, ...) {
    va_list arg;

    va_start(arg, fmt);
    char *str = nullptr;
    if(vasprintf(&str, fmt, arg) == -1) { abort(); }
    va_end(arg);

    TypeCheckError error(node.getToken(), kind, str);
    free(str);
    return error;
}

} // namespace ydsh
