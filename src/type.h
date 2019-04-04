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

#include "misc/flag_util.hpp"
#include "misc/noncopyable.h"
#include "handle_info.h"

struct DSState;

namespace ydsh {

class FieldHandle;
class MethodHandle;
class DSValue;
class DSCode;
using native_func_t = DSValue (*)(DSState &);

class SymbolTable;

enum class TYPE : unsigned int {
    _Root, // pseudo top type of all throwable type(except for option types)

    Any,
    Void,
    Nothing,

    _Value,    // super type of value type(int, float, bool, string). not directly used it.

    Byte,       // unsigned int 8
    Int16,
    Uint16,
    Int32,
    Uint32,
    Int64,
    Uint64,
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

class DSType {
protected:
    const unsigned int id;

    /**
     * if this type is Void or Any type, superType is null
     */
    DSType *superType;

    flag8_set_t attributeSet;

public:
    static constexpr flag8_t EXTENDIBLE   = 1u << 0u;
    static constexpr flag8_t FUNC_TYPE    = 1u << 1u;  // function type
    static constexpr flag8_t RECORD_TYPE  = 1u << 2u;  // indicate user defined type
    static constexpr flag8_t OPTION_TYPE  = 1u << 3u;  // Option<T>
    static constexpr flag8_t MODULE_TYPE  = 1u << 4u;  // Module type

    NON_COPYABLE(DSType);

    /**
     * not directly call it.
     */
    DSType(unsigned int id, DSType *superType, flag8_set_t attribute) :
            id(id), superType(superType), attributeSet(attribute) { }

    virtual ~DSType() = default;

    unsigned int getTypeID() const {
        return this->id;
    }

    bool is(TYPE type) const {
        return this->id == static_cast<unsigned int>(type);
    }

    /**
     * if true, can extend this type
     */
    bool isExtendible() const {
        return hasFlag(this->attributeSet, EXTENDIBLE);
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
        return hasFlag(this->attributeSet, FUNC_TYPE);
    }

    bool isRecordType() const {
        return hasFlag(this->attributeSet, RECORD_TYPE);
    }

    bool isNothingType() const {
        return this->is(TYPE::Nothing);
    }

    bool isOptionType() const {
        return hasFlag(this->attributeSet, OPTION_TYPE);
    }

    bool isModType() const {
        return hasFlag(this->attributeSet, MODULE_TYPE);
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
    virtual MethodHandle *getConstructorHandle(SymbolTable &symbolTable);

    /**
     * return null, if has no constructor
     */
    virtual const DSCode *getConstructor();

    /**
     * get size of the all fields(include superType fieldSize).
     */
    virtual unsigned int getFieldSize();

    /**
     * get size of the all methods(include superType method size)
     */
    virtual unsigned int getMethodSize();

    /**
     * return null, if has no field
     */
    virtual FieldHandle *lookupFieldHandle(SymbolTable &symbolTable, const std::string &fieldName);

    /**
     * return null, if has no field.
     */
    virtual MethodHandle *lookupMethodHandle(SymbolTable &symbolTable, const std::string &methodName);

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

    virtual const DSCode *getMethodRef(unsigned int methodIndex);
    virtual void copyAllMethodRef(std::vector<const DSCode *> &methodTable);

    static constexpr int INT64_PRECISION = 50;
    static constexpr int INT32_PRECISION = 40;
    static constexpr int INT16_PRECISION = 30;
    static constexpr int BYTE_PRECISION = 20;
    static constexpr int INVALID_PRECISION = 1;

    /**
     * get integer precision. if type is not int type, return INVALID_PRECISION.
     */
    int getIntPrecision() const;
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
            DSType(id, superType, DSType::FUNC_TYPE),
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

    /**
     * lookup from super type
     */
    MethodHandle *lookupMethodHandle(SymbolTable &symbolTable, const std::string &methodName) override;
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
    char handleInfo[24];

    /**
     * bool func(RuntimeContext &ctx)
     */
    native_func_t func_ptr;
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
     * may be null, if has no constructor.
     */
    MethodHandle *constructorHandle;

    /**
     * may be null, if has no constructor
     */
    const DSCode *constructor;

    std::unordered_map<std::string, MethodHandle *> methodHandleMap;
    std::vector<const DSCode *> methodTable;

public:
    BuiltinType(unsigned int id, DSType *superType, native_type_info_t info, flag8_set_t attribute);

    ~BuiltinType() override;

    MethodHandle *getConstructorHandle(SymbolTable &symbolTable) override;
    const DSCode *getConstructor() override;
    MethodHandle *lookupMethodHandle(SymbolTable &symbolTable, const std::string &methodName) override;

    unsigned int getMethodSize() override;
    const DSCode *getMethodRef(unsigned int methodIndex) override;
    void copyAllMethodRef(std::vector<const DSCode *> &methodTable) override;

protected:
    virtual bool initMethodHandle(MethodHandle *handle, SymbolTable &symbolTable, const NativeFuncInfo &info);
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
                std::vector<DSType *> &&elementTypes, flag8_set_t attribute = 0) :
            BuiltinType(id, superType, info, attribute), elementTypes(std::move(elementTypes)) { }

    ~ReifiedType() override = default;

    const std::vector<DSType *> &getElementTypes() const {
        return this->elementTypes;
    }

protected:
    bool initMethodHandle(MethodHandle *handle, SymbolTable &symbolTable, const NativeFuncInfo &info) override;
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
    unsigned int getFieldSize() override;

    FieldHandle *lookupFieldHandle(SymbolTable &symbolTable, const std::string &fieldName) override;
};

class ErrorType : public DSType {
private:
    MethodHandle *constructorHandle;

    static const NativeFuncInfo *funcInfo;
    static const DSCode *initRef;

public:
    ErrorType(unsigned int id, DSType *superType) :
            DSType(id, superType, DSType::EXTENDIBLE),
            constructorHandle() { }

    ~ErrorType() override;

    MethodHandle *getConstructorHandle(SymbolTable &symbolTable) override;
    const DSCode *getConstructor() override;

    /**
     * return types.size()
     */
    unsigned int getFieldSize() override;

    FieldHandle *lookupFieldHandle(SymbolTable &symbolTable, const std::string &fieldName) override;
    MethodHandle *lookupMethodHandle(SymbolTable &symbolTable, const std::string &methodName) override;

    /**
     * call only once.
     */
    static void registerFuncInfo(native_type_info_t info);
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

} // namespace ydsh

#endif //YDSH_TYPE_H
