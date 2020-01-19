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
#include <vector>
#include <cassert>

#include "misc/flag_util.hpp"
#include "misc/noncopyable.h"

namespace ydsh {

class DSType;
struct NativeFuncInfo;

#define EACH_FIELD_ATTR(OP) \
    OP(READ_ONLY  , (1u << 0u)) \
    OP(GLOBAL     , (1u << 1u)) \
    OP(ENV        , (1u << 2u)) \
    OP(FUNC_HANDLE, (1u << 3u)) \
    OP(RANDOM     , (1u << 4u)) \
    OP(SECONDS    , (1u << 5u)) \
    OP(BUILTIN    , (1u << 6u))

enum class FieldAttribute : unsigned short {
#define GEN_ENUM(E, V) E = (V),
    EACH_FIELD_ATTR(GEN_ENUM)
#undef GEN_ENUM
};

std::string toString(FieldAttribute attr);

template <> struct allow_enum_bitop<FieldAttribute> : std::true_type {};

/**
 * represent for class field or variable. field type may be function type.
 */
class FieldHandle {
private:
    DSType *type;

    unsigned int index;

    FieldAttribute attribute;

    /**
     * if global module, id is 0.
     */
    unsigned short modID;

public:
    FieldHandle() : FieldHandle(nullptr, 0, FieldAttribute()) {}

    FieldHandle(DSType *fieldType, unsigned int fieldIndex, FieldAttribute attribute, unsigned short modID = 0) :
            type(fieldType), index(fieldIndex), attribute(attribute), modID(modID) {}

    ~FieldHandle() = default;

    const DSType &getType() const {
        return *this->type;
    }

    unsigned int getIndex() const {
        return this->index;
    }

    FieldAttribute attr() const {
        return this->attribute;
    }

    explicit operator bool() const {
        return this->type != nullptr;
    }

    unsigned short getModID() const {
        return this->modID;
    }
};

class TypePool;

class MethodHandle {
private:
    friend class TypePool;

    unsigned int methodIndex;

    unsigned int paramSize;

    const DSType *returnType;

    const DSType *recvType;

    /**
     * not contains receiver type
     */
    const DSType *paramTypes[];

    MethodHandle(const DSType *recv, unsigned int index, const DSType *ret, unsigned int paramSize) :
            methodIndex(index), paramSize(paramSize), returnType(ret), recvType(recv) {}

    static MethodHandle *alloc(const DSType *recv, unsigned int index, const DSType *ret, unsigned int paramSize) {
        void *ptr = malloc(sizeof(MethodHandle) + sizeof(const DSType *) * paramSize);
        return new(ptr) MethodHandle(recv, index, ret, paramSize);
    }

public:
    NON_COPYABLE(MethodHandle);

    static MethodHandle *create(TypePool &pool, const DSType &recv, const std::string &name, unsigned int index);

    static void operator delete(void *ptr) noexcept {   //NOLINT
        free(ptr);
    }

    unsigned int getMethodIndex() const {
        return this->methodIndex;
    }

    const DSType &getReturnType() const {
        return *this->returnType;
    }

    const DSType &getRecvType() const {
        return *this->recvType;
    }

    unsigned short getParamSize() const {
        return this->paramSize;
    }

    const DSType &getParamTypeAt(unsigned int index) const {
        assert(index < this->getParamSize());
        return *this->paramTypes[index];
    }
};

} // namespace ydsh

#endif //YDSH_HANDLE_H
