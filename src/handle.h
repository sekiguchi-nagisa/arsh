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

class DSType;
struct NativeFuncInfo;

#define EACH_FIELD_ATTR(OP) \
    OP(READ_ONLY  , (1u << 0)) \
    OP(GLOBAL     , (1u << 1)) \
    OP(ENV        , (1u << 2)) \
    OP(FUNC_HANDLE, (1u << 3)) \
    OP(INTERFACE  , (1u << 4)) \
    OP(RANDOM     , (1u << 5)) \
    OP(SECONDS    , (1u << 6)) \
    OP(BUILTIN    , (1u << 7))

enum class FieldAttribute : unsigned short {
#define GEN_ENUM(E, V) E = (V),
    EACH_FIELD_ATTR(GEN_ENUM)
#undef GEN_ENUM
};

class FieldAttributes {
private:
    unsigned short value_{0};

private:
    FieldAttributes(unsigned short value) : value_(value) {}

public:
    FieldAttributes() = default;
    FieldAttributes(FieldAttribute attr) : value_(static_cast<unsigned short>(attr)) {}
    ~FieldAttributes() = default;

    friend inline FieldAttributes operator|(FieldAttribute x, FieldAttribute y);

    FieldAttributes operator|(FieldAttribute attr) const {
        return this->value_ | static_cast<unsigned short>(attr);
    }

    void set(FieldAttribute attr) {
        setFlag(this->value_, static_cast<unsigned short>(attr));
    }

    bool has(FieldAttribute attr) const {
        return hasFlag(this->value_, static_cast<unsigned short>(attr));
    }

    std::string str() const;
};

inline FieldAttributes operator|(FieldAttribute x, FieldAttribute y) {
    return static_cast<unsigned short>(x) | static_cast<unsigned short>(y);
}

/**
 * represent for class field or variable. field type may be function type.
 */
class FieldHandle {
private:
    DSType *type;

    unsigned int index;

    FieldAttributes attribute;

    /**
     * if global module, id is 0.
     */
    unsigned short modID;

public:
    FieldHandle() : FieldHandle(nullptr, 0, FieldAttributes()) {}

    FieldHandle(DSType *fieldType, unsigned int fieldIndex, FieldAttributes attribute, unsigned short modID = 0) :
            type(fieldType), index(fieldIndex), attribute(attribute), modID(modID) {}

    ~FieldHandle() = default;

    DSType *getType() const {
        return this->type;
    }

    unsigned int getIndex() const {
        return this->index;
    }

    FieldAttributes attr() const {
        return this->attribute;
    }

    explicit operator bool() const {
        return this->type != nullptr;
    }

    unsigned short getModID() const {
        return this->modID;
    }
};

class MethodHandle {
protected:
    unsigned int methodIndex;
    flag8_set_t attributeSet{0};

    DSType *returnType{nullptr};

    DSType *recvType{nullptr};

    /**
     * not contains receiver type
     */
    std::vector<DSType *> paramTypes;

    /**
     * may be null, if has no overloaded method.
     */
    MethodHandle *next{nullptr};

public:
    NON_COPYABLE(MethodHandle);

    explicit MethodHandle(unsigned int methodIndex) : methodIndex(methodIndex) { }

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

    static constexpr flag8_t INTERFACE    = 1u << 0;
    static constexpr flag8_t MULTI_RETURN = 1u << 1;
};

} // namespace ydsh

#endif //YDSH_HANDLE_H
