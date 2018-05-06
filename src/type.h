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

struct TypeVisitor;
class FieldHandle;
class MethodHandle;
class DSValue;
class DSCode;
typedef DSValue (*native_func_t)(DSState &);

class SymbolTable;

class DSType {
protected:
    const unsigned int id;

    /**
     * if this type is Void or Any type, superType is null
     */
    DSType *superType;

    flag8_set_t attributeSet;

public:
    static constexpr flag8_t EXTENDIBLE   = 1 << 0;
    static constexpr flag8_t VOID_TYPE    = 1 << 1;  // Void
    static constexpr flag8_t FUNC_TYPE    = 1 << 2;  // function type
    static constexpr flag8_t IFACE_TYPE   = 1 << 3;  // interface
    static constexpr flag8_t RECORD_TYPE  = 1 << 4;  // indicate user defined type
    static constexpr flag8_t NOTHING_TYPE = 1 << 5;  // Nothing (a.k.a. Bottom type)
    static constexpr flag8_t OPTION_TYPE  = 1 << 6;  // Option<T>
    static constexpr flag8_t MODULE_TYPE  = 1 << 7;  // Module type

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
        return hasFlag(this->attributeSet, VOID_TYPE);
    }

    /**
     * if this type is FunctionType, return true.
     */
    bool isFuncType() const {
        return hasFlag(this->attributeSet, FUNC_TYPE);
    }

    /**
     * if this type is InterfaceType, return true.
     */
    bool isInterface() const {
        return hasFlag(this->attributeSet, IFACE_TYPE);
    }

    bool isRecordType() const {
        return hasFlag(this->attributeSet, RECORD_TYPE);
    }

    bool isNothingType() const {
        return hasFlag(this->attributeSet, NOTHING_TYPE);
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

    virtual void accept(TypeVisitor *visitor) = 0;

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

    void accept(TypeVisitor *visitor) override;
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
    const unsigned short offset;

    /**
     * may be 0.
     */
    const unsigned char constructorSize;

    const unsigned char methodSize;


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

    void accept(TypeVisitor *visitor) override;
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

    void accept(TypeVisitor *visitor) override;
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
    void accept(TypeVisitor *visitor) override;
};


/**
 * for D-Bus interface
 */
class InterfaceType : public DSType {
private:
    std::unordered_map<std::string, FieldHandle *> fieldHandleMap;
    std::unordered_map<std::string, MethodHandle *> methodHandleMap;

public:
    /**
     * superType is always AnyType.
     */
    InterfaceType(unsigned int id, DSType *superType) :
            DSType(id, superType, DSType::IFACE_TYPE) { }

    ~InterfaceType() override;

    /**
     * return null, if found duplicated field
     */
    FieldHandle *newFieldHandle(const std::string &fieldName, DSType &fieldType, bool readOnly);

    MethodHandle *newMethodHandle(const std::string &methodName);

    unsigned int getFieldSize() override;
    unsigned int getMethodSize() override;
    FieldHandle *lookupFieldHandle(SymbolTable &symbolTable, const std::string &fieldName) override;
    MethodHandle *lookupMethodHandle(SymbolTable &symbolTable, const std::string &methodName) override;
    void accept(TypeVisitor *visitor) override;
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
    void accept(TypeVisitor *visitor) override;
    /**
     * call only once.
     */
    static void registerFuncInfo(native_type_info_t info);
};

struct TypeVisitor {
    virtual ~TypeVisitor() = default;

    virtual void visitFunctionType(FunctionType *type) = 0;
    virtual void visitBuiltinType(BuiltinType *type) = 0;
    virtual void visitReifiedType(ReifiedType *type) = 0;
    virtual void visitTupleType(TupleType *type) = 0;
    virtual void visitInterfaceType(InterfaceType *type) = 0;
    virtual void visitErrorType(ErrorType *type) = 0;
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
