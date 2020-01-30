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

#ifndef YDSH_TYPE_H
#define YDSH_TYPE_H

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <utility>
#include <memory>
#include <cassert>

#include "misc/flag_util.hpp"
#include "misc/noncopyable.h"
#include "handle_info.h"

struct DSState;

namespace ydsh {

class FieldHandle;
class DSValue;
class DSCode;
using native_func_t = DSValue (*)(DSState &);

class TypePool;
class SymbolTable;

enum class TYPE : unsigned int {
    _Root, // pseudo top type of all throwable type(except for option types)

    Any,
    Void,
    Nothing,

    _Value,    // super type of value type(int, float, bool, string). not directly used it.

    Int32,
    Int64,
    Float,
    Boolean,
    String,

    Regex,
    Signal,
    Signals,
    Error,
    Job,
    Func,
    StringIter,
    UnixFD,     // for Unix file descriptor
    StringArray,    // for command argument

    ArithmeticError,
    OutOfRangeError,
    KeyNotFoundError,
    TypeCastError,
    SystemError,    // for errno
    StackOverflowError,
    RegexSyntaxError,
    UnwrappingError,

    /**
     * for internal status reporting.
     * they are pseudo type, so must not use it from shell
     */
    _InternalStatus,   // base type
    _ShellExit,
    _AssertFail,
};

enum class TypeAttr : unsigned char {
    EXTENDIBLE   = 1u << 0u,
    FUNC_TYPE    = 1u << 1u,    // function type
    RECORD_TYPE  = 1u << 2u,    // indicate user defined type
    REIFIED_TYPE = 1u << 3u,    // reified type (Array, Map, Tuple, Option)
    OPTION_TYPE  = 1u << 4u,    // Option<T>
    MODULE_TYPE  = 1u << 5u,    // Module type
};

template <> struct allow_enum_bitop<TypeAttr> : std::true_type {};

class DSType {
protected:
    const unsigned int id;

    /**
     * if this type is Void or Any type, superType is null
     */
    DSType *superType;

    const TypeAttr attributeSet;

public:
    NON_COPYABLE(DSType);

    /**
     * not directly call it.
     */
    DSType(unsigned int id, DSType *superType, TypeAttr attribute) :
            id(id), superType(superType), attributeSet(attribute) { }

    virtual ~DSType() = default;

    unsigned int getTypeID() const {
        return this->id;
    }

    bool is(TYPE type) const {
        return this->id == static_cast<unsigned int>(type);
    }

    TypeAttr attr() const {
        return this->attributeSet;
    }

    /**
     * if true, can extend this type
     */
    bool isExtendible() const {
        return hasFlag(this->attr(), TypeAttr::EXTENDIBLE);
    }

    /**
     * if this type is VoidType, return true.
     */
    bool isVoidType() const {
        return this->is(TYPE::Void);
    }

    /**
     * if this type is FunctionType, return true.
     */
    bool isFuncType() const {
        return hasFlag(this->attr(), TypeAttr::FUNC_TYPE);
    }

    bool isRecordType() const {
        return hasFlag(this->attr(), TypeAttr::RECORD_TYPE);
    }

    bool isNothingType() const {
        return this->is(TYPE::Nothing);
    }

    bool isReifiedType() const {
        return hasFlag(this->attr(), TypeAttr::REIFIED_TYPE);
    }

    bool isOptionType() const {
        return hasFlag(this->attr(), TypeAttr::OPTION_TYPE);
    }

    bool isModType() const {
        return hasFlag(this->attr(), TypeAttr::MODULE_TYPE);
    }

    /**
     * get super type of this type.
     * return null, if has no super type(ex. AnyType, VoidType).
     */
    DSType *getSuperType() const {
        return this->superType;
    }

    /**
     * return null, if has no constructor
     */
    virtual const DSCode *getConstructor() const;

    /**
     * get size of the all fields(include superType fieldSize).
     */
    virtual unsigned int getFieldSize() const;

    /**
     * get size of the all methods(include superType method size)
     */
    virtual unsigned int getMethodSize() const;

    /**
     * return null, if has no field
     */
    virtual const FieldHandle *lookupFieldHandle(SymbolTable &symbolTable, const std::string &fieldName) const;

    bool operator==(const DSType &type) const {
        return (unsigned long) this == (unsigned long) &type;
    }

    bool operator!=(const DSType &type) const {
        return (unsigned long) this != (unsigned long) &type;
    }

    /**
     * check inheritance of target type.
     * if this type is equivalent to target type or
     * the super type of target type, return true.
     */
    bool isSameOrBaseTypeOf(const DSType &targetType) const;

    virtual const DSCode *getMethodRef(unsigned int methodIndex) const;

    static constexpr int INT64_PRECISION = 50;
    static constexpr int INT32_PRECISION = 40;
    static constexpr int INVALID_PRECISION = 1;

    /**
     * get integer precision. if type is not int type, return INVALID_PRECISION.
     */
    int getIntPrecision() const;

    /**
     * if type is not number type, return -1.
     */
    int getNumTypeIndex() const {
        static_assert(static_cast<unsigned int>(TYPE::Int32) + 1 == static_cast<unsigned int>(TYPE::Int64), "");
        static_assert(static_cast<unsigned int>(TYPE::Int64) + 1 == static_cast<unsigned int>(TYPE::Float), "");
        if(this->id >= static_cast<unsigned int>(TYPE::Int32) && this->id <= static_cast<unsigned int>(TYPE::Float)) {
            return this->id - static_cast<unsigned int>(TYPE::Int32);
        }
        return -1;
    }

    virtual void copyAllMethodRef(std::vector<const DSCode *> &methodTable) const;
};

class FunctionType : public DSType {
private:
    DSType *returnType;

    /**
     * may be empty vector, if has no parameter
     */
    std::vector<DSType *> paramTypes;

public:
    FunctionType(unsigned int id, DSType *superType, DSType *returnType, std::vector<DSType *> &&paramTypes) :
            DSType(id, superType, TypeAttr::FUNC_TYPE),
            returnType(returnType), paramTypes(std::move(paramTypes)) {}

    ~FunctionType() override = default;

    DSType *getReturnType() const {
        return this->returnType;
    }

    /**
     * may be empty vector, if has no parameter (getParamSize() == 0)
     */
    const std::vector<DSType *> &getParamTypes() const {
        return this->paramTypes;
    }
};

/**
 * for method handle creation.
 */
struct NativeFuncInfo {
    /**
     * if empty string, treat as constructor.
     */
    const char *funcName;

    /**
     * serialized function handle
     */
    const HandleInfo handleInfo[30];

    /**
     * bool func(RuntimeContext &ctx)
     */
    const native_func_t func_ptr;

    const bool hasRet;
};

struct native_type_info_t {
    unsigned short offset;

    /**
     * may be 0.
     */
    unsigned char constructorSize;

    unsigned char methodSize;


    const NativeFuncInfo &getMethodInfo(unsigned int index) const;

    /**
     * not call it if constructorSize is 0
     */
    const NativeFuncInfo &getInitInfo() const;
};

/**
 * builtin type(any, void, value ...)
 * not support override. (if override method, must override DSObject's method)
 * so this->getFieldSize is equivalent to superType->getFieldSize() + infoSize
 */
class BuiltinType : public DSType {
protected:
    const native_type_info_t info;

    /**
     * may be null, if has no constructor
     */
    const DSCode *constructor{nullptr};

    std::vector<const DSCode *> methodTable;

public:
    BuiltinType(unsigned int id, DSType *superType, native_type_info_t info, TypeAttr attribute);

    ~BuiltinType() = default;

    const DSCode *getConstructor() const override;

    unsigned int getMethodSize() const override;
    const DSCode *getMethodRef(unsigned int methodIndex) const override;

    native_type_info_t getNativeTypeInfo() const {
        return this->info;
    }

    unsigned int getBaseIndex() const {
        return this->superType != nullptr ? this->superType->getMethodSize() : 0;
    }

    void copyAllMethodRef(std::vector<const DSCode *> &methodTable) const override;
};

/**
 * not support override.
 */
class ReifiedType : public BuiltinType {
protected:
    /**
     * size is 1 or 2.
     */
    std::vector<DSType *> elementTypes;

public:
    /**
     * super type is AnyType or VariantType.
     */
    ReifiedType(unsigned int id, native_type_info_t info, DSType *superType,
                std::vector<DSType *> &&elementTypes, TypeAttr attribute = TypeAttr()) :
            BuiltinType(id, superType, info, attribute | TypeAttr::REIFIED_TYPE),
            elementTypes(std::move(elementTypes)) { }

    ~ReifiedType() override = default;

    const std::vector<DSType *> &getElementTypes() const {
        return this->elementTypes;
    }
};


class TupleType : public ReifiedType {
private:
    std::unordered_map<std::string, FieldHandle *> fieldHandleMap;

public:
    /**
     * superType is AnyType ot VariantType
     */
    TupleType(unsigned int id, native_type_info_t info, DSType *superType, std::vector<DSType *> &&types);
    ~TupleType() override;

    /**
     * return types.size()
     */
    unsigned int getFieldSize() const override;

    const FieldHandle *lookupFieldHandle(SymbolTable &symbolTable, const std::string &fieldName) const override;
};

class ErrorType : public DSType {
public:
    ErrorType(unsigned int id, DSType *superType) :
            DSType(id, superType, TypeAttr::EXTENDIBLE) {}

    const DSCode *getConstructor() const override;

    /**
     * return types.size()
     */
    unsigned int getFieldSize() const override;

    const FieldHandle *lookupFieldHandle(SymbolTable &symbolTable, const std::string &fieldName) const override;
};

/**
 * ReifiedType template.
 */
class TypeTemplate {
private:
    std::string name;

    std::vector<DSType*> acceptableTypes;

    native_type_info_t info;

public:
    TypeTemplate() = default;

    TypeTemplate(std::string &&name, std::vector<DSType*> &&elementTypes, native_type_info_t info) :
            name(std::move(name)), acceptableTypes(std::move(elementTypes)), info(info) { }

    ~TypeTemplate() = default;

    bool operator==(const TypeTemplate &o) const {
        return this->name == o.name;
    }

    const std::string &getName() const {
        return this->name;
    }

    unsigned int getElementTypeSize() const {
        return this->acceptableTypes.size();
    }

    native_type_info_t getInfo() const {
        return this->info;
    }

    const std::vector<DSType *> &getAcceptableTypes() const {
        return this->acceptableTypes;
    }
};

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

class MethodHandle {
private:
    unsigned int id;

    unsigned short methodIndex;

    unsigned short paramSize;

    const DSType *returnType;

    const DSType *recvType;

    /**
     * not contains receiver type
     */
    const DSType *paramTypes[];

    MethodHandle(unsigned int id, const DSType *recv, unsigned short index,
                 const DSType *ret, unsigned short paramSize) :
            id(id), methodIndex(index), paramSize(paramSize), returnType(ret), recvType(recv) {}

    static MethodHandle *alloc(unsigned int count, const DSType *recv, unsigned int index,
                               const DSType *ret, unsigned int paramSize) {
        void *ptr = malloc(sizeof(MethodHandle) + sizeof(const DSType *) * paramSize);
        return new(ptr) MethodHandle(count, recv, index, ret, paramSize);
    }

public:
    NON_COPYABLE(MethodHandle);

    static MethodHandle *create(TypePool &pool, const DSType &recv, const std::string &name, unsigned int index);

    static void operator delete(void *ptr) noexcept {   //NOLINT
        free(ptr);
    }

    unsigned int getID() const {
        return this->id;
    }

    unsigned short getMethodIndex() const {
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

#endif //YDSH_TYPE_H
