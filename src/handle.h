/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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

#ifndef YDSH_HANDLE_H
#define YDSH_HANDLE_H

#include <string>
#include <utility>
#include <vector>
#include <unordered_map>

#include "misc/flag_util.hpp"
#include "misc/noncopyable.h"

namespace ydsh {

class SymbolTable;
class DSType;
class FunctionType;
struct NativeFuncInfo;

#define EACH_FIELD_ATTR(OP) \
    OP(READ_ONLY  , (1 << 0)) \
    OP(GLOBAL     , (1 << 1)) \
    OP(ENV        , (1 << 2)) \
    OP(FUNC_HANDLE, (1 << 3)) \
    OP(INTERFACE  , (1 << 4)) \
    OP(RANDOM     , (1 << 5)) \
    OP(SECONDS    , (1 << 6))

enum class FieldAttribute : unsigned int {
#define GEN_ENUM(E, V) E = (V),
    EACH_FIELD_ATTR(GEN_ENUM)
#undef GEN_ENUM
};

class FieldAttributes {
private:
    unsigned int value_{0};

private:
    FieldAttributes(unsigned int value) : value_(value) {}

public:
    FieldAttributes() = default;
    FieldAttributes(FieldAttribute attr) : value_(static_cast<unsigned int>(attr)) {}
    ~FieldAttributes() = default;

    friend inline FieldAttributes operator|(FieldAttribute x, FieldAttribute y);

    FieldAttributes operator|(FieldAttribute attr) const {
        return this->value_ | static_cast<unsigned int>(attr);
    }

    void set(FieldAttribute attr) {
        setFlag(this->value_, static_cast<unsigned int>(attr));
    }

    bool has(FieldAttribute attr) const {
        return hasFlag(this->value_, static_cast<unsigned int>(attr));
    }

    std::string str() const;
};

inline FieldAttributes operator|(FieldAttribute x, FieldAttribute y) {
    return static_cast<unsigned int>(x) | static_cast<unsigned int>(y);
}

/**
 * represent for class field or variable. field type may be function type.
 */
class FieldHandle {
protected:
    DSType *fieldType;

private:
    unsigned int fieldIndex;

    FieldAttributes attribute;

public:
    NON_COPYABLE(FieldHandle);

    FieldHandle(DSType *fieldType, unsigned int fieldIndex, FieldAttributes attribute) :
            fieldType(fieldType), fieldIndex(fieldIndex), attribute(attribute) {}

    virtual ~FieldHandle() = default;

    virtual DSType *getFieldType(SymbolTable &symbolTable);

    unsigned int getFieldIndex() const {
        return this->fieldIndex;
    }

    FieldAttributes attr() const {
        return this->attribute;
    }
};

/**
 * represent for function. used from SymbolTable.
 */
class FunctionHandle : public FieldHandle {
protected:
    DSType *returnType;
    std::vector<DSType *> paramTypes;

public:
    FunctionHandle(DSType *returnType, std::vector<DSType *> paramTypes, unsigned int fieldIndex) :
            FieldHandle(nullptr, fieldIndex, FieldAttribute::READ_ONLY | FieldAttribute::FUNC_HANDLE | FieldAttribute::GLOBAL),
            returnType(returnType), paramTypes(std::move(paramTypes)) { }

    ~FunctionHandle() override = default;

    DSType *getFieldType(SymbolTable &symbolTable) override;

    DSType *getReturnType() const {
        return this->returnType;
    }

    const std::vector<DSType *> &getParamTypes() const;
};

class MethodHandle {
protected:
    unsigned int methodIndex;
    flag8_set_t attributeSet;

    DSType *returnType;

    DSType *recvType;

    /**
     * not contains receiver type
     */
    std::vector<DSType *> paramTypes;

    /**
     * may be null, if has no overloaded method.
     */
    MethodHandle *next;

public:
    NON_COPYABLE(MethodHandle);

    explicit MethodHandle(unsigned int methodIndex) :
            methodIndex(methodIndex), attributeSet(),
            returnType(), recvType(), next() { }

    ~MethodHandle();

    unsigned int getMethodIndex() const {
        return this->methodIndex;
    }

    void setReturnType(DSType &type) {
        this->returnType = &type;
    }

    DSType *getReturnType() const {
        return this->returnType;
    }

    void setRecvType(DSType &type) {
        this->recvType = &type;
    }

    DSType *getRecvType() const {
        return this->recvType;
    }

    void addParamType(DSType &type);

    const std::vector<DSType *> &getParamTypes() const {
        return this->paramTypes;
    }

    /**
     * initialize internal types.
     */
    bool init(SymbolTable &symbolTable, const NativeFuncInfo &info,
              const std::vector<DSType *> *types = nullptr);

    /**
     * return always true, after call init().
     */
    bool initialized() const {
        return this->returnType != nullptr;
    }

    void setNext(MethodHandle *handle) {
        this->next = handle;
    }

    MethodHandle *getNext() const {
        return this->next;
    }

    void setAttribute(flag8_t attribute) {
        setFlag(this->attributeSet, attribute);
    }

    bool isInterfaceMethod() const {
        return hasFlag(this->attributeSet, INTERFACE);
    }

    bool hasMultipleReturnType() const {
        return hasFlag(this->attributeSet, MULTI_RETURN);
    }

    bool isSignal() const;

    static constexpr flag8_t INTERFACE    = 1 << 0;
    static constexpr flag8_t MULTI_RETURN = 1 << 1;
};

} // namespace ydsh

#endif //YDSH_HANDLE_H
