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
#include "tcerror.h"
#include "core.h"

namespace ydsh {

// ####################
// ##     DSType     ##
// ####################

unsigned int DSType::getFieldSize() const {
    return this->superType != nullptr ? this->superType->getFieldSize() : 0;
}

const FieldHandle *DSType::lookupFieldHandle(SymbolTable &, const std::string &) const {
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

const FieldHandle *TupleType::lookupFieldHandle(SymbolTable &symbolTable, const std::string &fieldName) const {
    auto iter = this->fieldHandleMap.find(fieldName);
    if(iter == this->fieldHandleMap.end()) {
        return this->superType->lookupFieldHandle(symbolTable, fieldName);
    }
    return iter->second;
}

// #######################
// ##     ErrorType     ##
// #######################

unsigned int ErrorType::getFieldSize() const {
    return this->superType->getFieldSize();
}

const FieldHandle *ErrorType::lookupFieldHandle(SymbolTable &symbolTable, const std::string &fieldName) const {
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

std::string toString(FieldAttribute attr) {
    const char *table[] = {
#define GEN_STR(E, V) #E,
            EACH_FIELD_ATTR(GEN_STR)
#undef GEN_STR
    };

    std::string value;
    for(unsigned int i = 0; i < arraySize(table); i++) {
        if(hasFlag(attr, static_cast<FieldAttribute>(1u << i))) {
            if(!value.empty()) {
                value += " | ";
            }
            value += table[i];
        }
    }
    return value;
}

} // namespace ydsh
