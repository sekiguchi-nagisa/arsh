/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#ifndef YDSH_CORE_TYPE_H
#define YDSH_CORE_TYPE_H

#include <unordered_map>
#include <vector>
#include <string>
#include <utility>
#include <memory>

#include "../misc/flag_util.hpp"
#include "handle_info.h"
#include "type_pool.h"

namespace ydsh {

using namespace ydsh::misc;

struct TypeVisitor;

class FieldHandle;
class MethodHandle;

class RuntimeContext;
typedef bool (*native_func_t)(RuntimeContext &);

/**
 * reference of method. for method call, constructor call.
 */
class MethodRef {
private:
    native_func_t func_ptr;

public:
    MethodRef() : func_ptr(nullptr) { }
    explicit MethodRef(native_func_t func_ptr) : func_ptr(func_ptr) { }
    ~MethodRef() = default;

    explicit operator bool() const {
        return this->func_ptr != nullptr;
    }

    bool invoke(RuntimeContext &ctx) const {
        return this->func_ptr(ctx);
    }
};

class DSType {
protected:
    /**
     * if this type is Void or Any type, superType is null
     */
    DSType *superType;

    flag8_set_t attributeSet;

public:
    static constexpr flag8_t EXTENDABLE  = 1 << 0;
    static constexpr flag8_t VOID_TYPE   = 1 << 1;  // Void
    static constexpr flag8_t FUNC_TYPE   = 1 << 2;  // function type
    static constexpr flag8_t IFACE_TYPE  = 1 << 3;  // interface
    static constexpr flag8_t RECORD_TYPE = 1 << 4;  // indicate user defined type
    static constexpr flag8_t BOTTOM_TYPE = 1 << 5;  // Bottom

    NON_COPYABLE(DSType);

    /**
     * not directly call it.
     */
    DSType(DSType *superType, flag8_set_t attribute);

    virtual ~DSType() = default;

    /**
     * if true, can extend this type
     */
    bool isExtendable() const {
        return hasFlag(this->attributeSet, EXTENDABLE);
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

    bool isBottomType() const {
        return hasFlag(this->attributeSet, BOTTOM_TYPE);
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
    virtual MethodHandle *getConstructorHandle(TypePool &typePool);

    /**
     * return null, if has no constructor
     */
    virtual const MethodRef *getConstructor();

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
    virtual FieldHandle *lookupFieldHandle(TypePool &typePool, const std::string &fieldName);

    /**
     * return null, if has no field.
     */
    virtual MethodHandle *lookupMethodHandle(TypePool &typePool, const std::string &methodName);

    /**
     * return null if handle not found.
     * not directly use it
     */
    virtual FieldHandle *findHandle(const std::string &fieldName) = 0;

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
    virtual bool isSameOrBaseTypeOf(const DSType &targetType) const;

    virtual MethodRef *getMethodRef(unsigned int methodIndex);
    virtual void copyAllMethodRef(std::vector<MethodRef> &methodTable);
};

class FunctionType : public DSType {
private:
    DSType *returnType;

    /**
     * may be empty vector, if has no parameter
     */
    std::vector<DSType *> paramTypes;

public:
    FunctionType(DSType *superType,
                 DSType *returnType, std::vector<DSType *> &&paramTypes) :
            DSType(superType, DSType::FUNC_TYPE),
            returnType(returnType), paramTypes(std::move(paramTypes)) {
    }

    ~FunctionType() = default;

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
    MethodHandle *lookupMethodHandle(TypePool &typePool, const std::string &methodName) override;

    FieldHandle *findHandle(const std::string &fieldName) override;
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


    NativeFuncInfo &getMethodInfo(unsigned int index);

    /**
     * not call it if constructorSize is 0
     */
    NativeFuncInfo &getInitInfo();
};

/**
 * builtin type(any, void, value ...)
 * not support override. (if override method, must override DSObject's method)
 * so this->getFieldSize is equivalent to superType->getFieldSize() + infoSize
 */
class BuiltinType : public DSType {
protected:
    native_type_info_t info;

    /**
     * may be null, if has no constructor.
     */
    MethodHandle *constructorHandle;

    /**
     * may be null, if has no constructor
     */
    MethodRef constructor;

    std::unordered_map<std::string, MethodHandle *> methodHandleMap;
    std::vector<MethodRef> methodTable;

public:
    BuiltinType(DSType *superType, native_type_info_t info, flag8_set_t attribute);

    virtual ~BuiltinType();

    virtual MethodHandle *getConstructorHandle(TypePool &typePool) override;
    const MethodRef *getConstructor() override;
    MethodHandle *lookupMethodHandle(TypePool &typePool, const std::string &methodName) override;
    virtual FieldHandle *findHandle(const std::string &fieldName) override;
    virtual void accept(TypeVisitor *visitor) override;
    unsigned int getMethodSize() override;
    MethodRef *getMethodRef(unsigned int methodIndex) override;
    void copyAllMethodRef(std::vector<MethodRef> &methodTable) override;

protected:
    virtual void initMethodHandle(MethodHandle *handle, TypePool &typePool, NativeFuncInfo &info);
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
    ReifiedType(native_type_info_t info, DSType *superType, std::vector<DSType *> &&elementTypes) :
            BuiltinType(superType, info, 0), elementTypes(std::move(elementTypes)) { }

    virtual ~ReifiedType() = default;

    const std::vector<DSType *> &getElementTypes() const {
        return this->elementTypes;
    }

    virtual void accept(TypeVisitor *visitor) override;
protected:
    void initMethodHandle(MethodHandle *handle, TypePool &typePool, NativeFuncInfo &info) override;
};


class TupleType : public ReifiedType {
private:
    std::unordered_map<std::string, FieldHandle *> fieldHandleMap;

public:
    /**
     * superType is AnyType ot VariantType
     */
    TupleType(native_type_info_t info, DSType *superType, std::vector<DSType *> &&types);
    ~TupleType();

    MethodHandle *getConstructorHandle(TypePool &typePool) override;

    /**
     * return types.size()
     */
    unsigned int getFieldSize() override;

    FieldHandle *lookupFieldHandle(TypePool &typePool, const std::string &fieldName) override;
    FieldHandle *findHandle(const std::string &fieldName) override;
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
    explicit InterfaceType(DSType *superType) :
            DSType(superType, DSType::IFACE_TYPE), fieldHandleMap(), methodHandleMap() { }

    ~InterfaceType();

    /**
     * return null, if found duplicated field
     */
    FieldHandle *newFieldHandle(const std::string &fieldName, DSType &fieldType, bool readOnly);

    MethodHandle *newMethodHandle(const std::string &methodName);

    unsigned int getFieldSize() override;
    unsigned int getMethodSize() override;
    FieldHandle *lookupFieldHandle(TypePool &typePool, const std::string &fieldName) override;
    MethodHandle *lookupMethodHandle(TypePool &typePool, const std::string &methodName) override;
    FieldHandle *findHandle(const std::string &fieldName) override;
    void accept(TypeVisitor *visitor) override;
};

class ErrorType : public DSType {
private:
    MethodHandle *constructorHandle;

    static NativeFuncInfo *funcInfo;
    static MethodRef initRef;

public:
    explicit ErrorType(DSType *superType) :
            DSType(superType, DSType::EXTENDABLE),
            constructorHandle() { }

    ~ErrorType();

    MethodHandle *getConstructorHandle(TypePool &typePool) override;
    const MethodRef *getConstructor() override;

    /**
     * return types.size()
     */
    unsigned int getFieldSize() override;

    FieldHandle *lookupFieldHandle(TypePool &typePool, const std::string &fieldName) override;
    MethodHandle *lookupMethodHandle(TypePool &typePool, const std::string &methodName) override;
    FieldHandle *findHandle(const std::string &fieldName) override;
    void accept(TypeVisitor *visitor) override;
    /**
     * call only once.
     */
    static void registerFuncInfo(NativeFuncInfo &info);
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

#endif //YDSH_CORE_TYPE_H
