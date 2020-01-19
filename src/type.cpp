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

const NativeCode *getNativeCode(unsigned int index);

// ####################
// ##     DSType     ##
// ####################

MethodHandle *DSType::getConstructorHandle(TypePool &) {
    return nullptr;
}

const DSCode *DSType::getConstructor() const {
    return nullptr;
}

unsigned int DSType::getFieldSize() const {
    return this->superType != nullptr ? this->superType->getFieldSize() : 0;
}

unsigned int DSType::getMethodSize() const {
    return this->superType != nullptr ? this->superType->getMethodSize() : 0;
}

FieldHandle *DSType::lookupFieldHandle(SymbolTable &, const std::string &) {
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

const DSCode *DSType::getMethodRef(unsigned int methodIndex) const {
    return this->superType != nullptr ? this->superType->getMethodRef(methodIndex) : nullptr;
}

void DSType::copyAllMethodRef(std::vector<const DSCode *> &) {
}

int DSType::getIntPrecision() const {
    switch(this->getTypeID()) {
    case static_cast<unsigned int>(TYPE::Int64):
        return INT64_PRECISION;
    case static_cast<unsigned int>(TYPE::Int32):
        return INT32_PRECISION;
    default:
        return INVALID_PRECISION;
    }
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

BuiltinType::BuiltinType(unsigned int id, DSType *superType, native_type_info_t info, TypeAttr attribute) :
        DSType(id, superType, attribute), info(info), methodTable(this->getMethodSize()) {

    // copy super type methodRef to method table
    if(this->superType != nullptr) {
        this->superType->copyAllMethodRef(this->methodTable);
    }

    // set to method table
    unsigned int baseIndex = this->getBaseIndex();
    for(unsigned int i = 0; i < info.methodSize; i++) {
        unsigned int methodIndex = baseIndex + i;
        this->methodTable[methodIndex] = getCode(this->info, i);
    }

    // init constructor handle
    if(this->info.constructorSize != 0) {
        this->constructor = getCode(this->info);
    }
}

BuiltinType::~BuiltinType() {
    delete this->constructorHandle;
}

MethodHandle *BuiltinType::getConstructorHandle(TypePool &pool) {
    if(this->constructorHandle == nullptr && this->info.constructorSize != 0) {
        this->constructorHandle = new MethodHandle(0);
        if(!this->initMethodHandle(this->constructorHandle, pool, this->info.getInitInfo())) {
            return nullptr;
        }
    }
    return this->constructorHandle;
}

const DSCode *BuiltinType::getConstructor() const {
    return this->constructor;
}

unsigned int BuiltinType::getMethodSize() const {
    return this->info.methodSize + (this->superType != nullptr ? this->superType->getMethodSize() : 0);
}

const DSCode *BuiltinType::getMethodRef(unsigned int methodIndex) const {
    return this->methodTable[methodIndex];
}

void BuiltinType::copyAllMethodRef(std::vector<const DSCode *> &methodTable) {
    unsigned int size = this->getMethodSize();
    assert(size <= methodTable.size());

    for(unsigned int i = 0; i < size; i++) {
        methodTable[i] = this->methodTable[i];
    }
}

bool BuiltinType::initMethodHandle(MethodHandle *handle, TypePool &pool, const NativeFuncInfo &info) {
    return handle->init(pool, info);
}

// #########################
// ##     ReifiedType     ##
// #########################

bool ReifiedType::initMethodHandle(MethodHandle *handle, TypePool &pool, const NativeFuncInfo &info) {
    return handle->init(pool, info, &this->elementTypes);
}

// #######################
// ##     TupleType     ##
// #######################

TupleType::TupleType(unsigned int id, native_type_info_t info, DSType *superType, std::vector<DSType *> &&types) :
        ReifiedType(id, info, superType, std::move(types)) {
    const unsigned int size = this->elementTypes.size();
    const unsigned int baseIndex = this->superType->getFieldSize();
    for(unsigned int i = 0; i < size; i++) {
        auto *handle = new FieldHandle(this->elementTypes[i], i + baseIndex, FieldAttribute());
        this->fieldHandleMap.insert(std::make_pair("_" + std::to_string(i), handle));
    }
}

TupleType::~TupleType() {
    for(auto &pair : this->fieldHandleMap) {
        delete pair.second;
    }
}

unsigned int TupleType::getFieldSize() const {
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

MethodHandle *ErrorType::getConstructorHandle(TypePool &pool) {
    if(this->constructorHandle == nullptr) {
        this->constructorHandle = new MethodHandle(0);
        auto info = static_cast<BuiltinType *>(this->superType)->getNativeTypeInfo().getInitInfo();
        this->constructorHandle->init(pool, info);
        this->constructorHandle->setRecvType(*this);
    }
    return this->constructorHandle;
}

const DSCode *ErrorType::getConstructor() const {
    return this->superType->getConstructor();
}

unsigned int ErrorType::getFieldSize() const {
    return this->superType->getFieldSize();
}

FieldHandle *ErrorType::lookupFieldHandle(SymbolTable &symbolTable, const std::string &fieldName) {
    return this->superType->lookupFieldHandle(symbolTable, fieldName);
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
