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

    DSType *getType() const {
        return this->type;
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

class MethodHandle {
protected:
    unsigned int methodIndex;

    DSType *returnType{nullptr};

    DSType *recvType{nullptr};

    /**
     * not contains receiver type
     */
    std::vector<DSType *> paramTypes;

public:
    NON_COPYABLE(MethodHandle);

    explicit MethodHandle(unsigned int methodIndex) : methodIndex(methodIndex) { }

    ~MethodHandle() = default;

    unsigned int getMethodIndex() const {
        return this->methodIndex;
    }

    DSType *getReturnType() const {
        return this->returnType;
    }

    void setRecvType(DSType &type) {
        this->recvType = &type;
    }

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
};

} // namespace ydsh

#endif //YDSH_HANDLE_H
