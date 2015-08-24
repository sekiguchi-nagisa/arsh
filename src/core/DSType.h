/*
 * Copyright (C) 2015 Nagisa Sekiguchi
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

#ifndef CORE_DSTYPE_H_
#define CORE_DSTYPE_H_

#include <unordered_map>
#include <vector>
#include <string>
#include <utility>
#include <memory>

#include "../misc/flag_util.hpp"
#include "handle_info.h"
#include "TypePool.h"

namespace ydsh {
namespace core {

using namespace ydsh::misc;

struct TypeVisitor;

struct MethodRef;

class FieldHandle;
class MethodHandle;

class RuntimeContext;
typedef bool (*native_func_t)(RuntimeContext &);

class DSType {
protected:
    flag8_set_t attributeSet;

    /**
     * if this type is Void or Any type, superType is null
     */
    DSType *superType;

public:
    const static flag8_t EXTENDABLE = 1 << 0;
    const static flag8_t VOID_TYPE  = 1 << 1;
    const static flag8_t FUNC_TYPE  = 1 << 2;
    const static flag8_t INTERFACE  = 1 << 3;

    DSType(bool extendable, DSType *superType, bool isVoid = false);

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
        return hasFlag(this->attributeSet, INTERFACE);
    }

    /**
     * return true, if type is builtin type, reified type, tuple type, error type.
     */
    virtual bool isBuiltinType() const;

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
    virtual MethodHandle *getConstructorHandle(TypePool *typePool);

    /**
     * return null, if has no constructor
     */
    virtual MethodRef *getConstructor();

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
    virtual FieldHandle *lookupFieldHandle(TypePool *typePool, const std::string &fieldName);

    /**
     * return null, if has no field.
     */
    virtual MethodHandle *lookupMethodHandle(TypePool *typePool, const std::string &methodName);

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
    virtual bool isSameOrBaseTypeOf(DSType *targetType);

    virtual MethodRef *getMethodRef(unsigned int methodIndex);
    virtual void copyAllMethodRef(std::vector<std::shared_ptr<MethodRef>> &methodTable);
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
            DSType(false, superType, false),
            returnType(returnType), paramTypes(std::move(paramTypes)) {
        setFlag(this->attributeSet, FUNC_TYPE);
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
    MethodHandle *lookupMethodHandle(TypePool *typePool, const std::string &methodName);    // override

    FieldHandle *findHandle(const std::string &fieldName);  // override
    void accept(TypeVisitor *visitor); // override
};

/**
 * for function handle(method handle or constructor handle) creation.
 */
struct NativeFuncInfo {
    /**
     * if empty string, treat as constructor.
     */
    const char *funcName;

    /**
     * serialized function handle
     */
    char handleInfo[32];

    const char *paramNames[8];

    /**
     * bool func(RuntimeContext &ctx)
     */
    native_func_t func_ptr;

    /**
     * if arg1, arg3, arg4 has default value, then (00001101).
     * support up to 8 arguments.
     */
    unsigned char defaultValueFlag;
};

struct native_type_info_t {
    /**
     * may be null, if has no constructor.
     */
    NativeFuncInfo *initInfo;

    /**
     * up to 24
     */
    const unsigned int methodSize;

    NativeFuncInfo funcInfos[24];
};

/**
 * builtin type(any, void, value ...)
 * not support override. (if override method, must override DSObject's method)
 * so this->getFieldSize is equivalent to superType->getFieldSize() + infoSize
 */
class BuiltinType : public DSType {
protected:
    native_type_info_t *info;

    /**
     * may be null, if has no constructor.
     */
    MethodHandle *constructorHandle;

    /**
     * may be null, if has no constructor
     */
    std::shared_ptr<MethodRef> constructor;

    std::unordered_map<std::string, MethodHandle *> methodHandleMap;
    std::vector<std::shared_ptr<MethodRef>> methodTable;

public:
    /**
     * actually superType is BuiltinType.
     */
    BuiltinType(bool extendable, DSType *superType,
                native_type_info_t *info, bool isVoid);

    virtual ~BuiltinType();

    virtual MethodHandle *getConstructorHandle(TypePool *typePool); // override
    virtual MethodRef *getConstructor();   // override.
    MethodHandle *lookupMethodHandle(TypePool *typePool, const std::string &methodName);  // override
    virtual FieldHandle *findHandle(const std::string &fieldName); // override
    virtual void accept(TypeVisitor *visitor); // override
    bool isBuiltinType() const; // override
    unsigned int getMethodSize(); // override
    MethodRef *getMethodRef(unsigned int methodIndex); // override
    void copyAllMethodRef(std::vector<std::shared_ptr<MethodRef>> &methodTable); // override

private:
    virtual void initMethodHandle(MethodHandle *handle, TypePool *typePool, NativeFuncInfo *info);
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
    ReifiedType(native_type_info_t *info, DSType *superType, std::vector<DSType *> &&elementTypes) :
            BuiltinType(false, superType, info, false), elementTypes(std::move(elementTypes)) { }

    virtual ~ReifiedType() = default;

    const std::vector<DSType *> &getElementTypes() const {
        return this->elementTypes;
    }

    virtual void accept(TypeVisitor *visitor); // override

protected:
    void initMethodHandle(MethodHandle *handle, TypePool *typePool, NativeFuncInfo *info); // override
};


class TupleType : public ReifiedType {
private:
    std::unordered_map<std::string, FieldHandle *> fieldHandleMap;

public:
    /**
     * superType is AnyType ot VariantType
     */
    TupleType(native_type_info_t *info, DSType *superType, std::vector<DSType *> &&types);
    ~TupleType();

    MethodHandle *getConstructorHandle(TypePool *typePool); // override

    /**
     * return types.size()
     */
    unsigned int getFieldSize(); // override

    FieldHandle *lookupFieldHandle(TypePool *typePool, const std::string &fieldName); // override
    FieldHandle *findHandle(const std::string &fieldName); // override
    void accept(TypeVisitor *visitor); // override
};

/**
 * pseudo base type of Array, Map, Tuple type.
 * for command argument type checking.
 */
class CollectionType : public DSType {
public:
    /**
     * super type is always Any type
     */
    explicit CollectionType(DSType *superType) : DSType(false, superType, false) { }
    ~CollectionType() = default;

    FieldHandle *findHandle(const std::string &fieldName) { // override
        return nullptr;
    }

    void accept(TypeVisitor *visitor);  // override

    /**
     * if targetType is Array, Map, Tupple type, return true.
     */
    bool isSameOrBaseTypeOf(DSType *targetType); // override
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
            DSType(false, superType, false), fieldHandleMap(), methodHandleMap() {
        setFlag(this->attributeSet, INTERFACE);
    }

    ~InterfaceType();

    /**
     * return null, if found duplicated field
     */
    FieldHandle *newFieldHandle(const std::string &fieldName, DSType *fieldType, bool readOnly);

    MethodHandle *newMethodHandle(const std::string &methodName);

    unsigned int getFieldSize();    // override
    unsigned int getMethodSize();   // override
    FieldHandle *lookupFieldHandle(TypePool *typePool, const std::string &fieldName);   // override
    MethodHandle *lookupMethodHandle(TypePool *typePool, const std::string &methodName); // override
    FieldHandle *findHandle(const std::string &fieldName); // overrdie
    void accept(TypeVisitor *visitor); // override
};

class ErrorType : public DSType {
private:
    MethodHandle *constructorHandle;

    static NativeFuncInfo *funcInfo;
    static std::shared_ptr<MethodRef> initRef;

public:
    explicit ErrorType(DSType *superType) :
            DSType(true, superType, false), constructorHandle() { }

    ~ErrorType();

    MethodHandle *getConstructorHandle(TypePool *typePool); // override
    MethodRef *getConstructor();    // override

    /**
     * return always true.
     */
    bool isBuiltinType() const; // override

    /**
     * return types.size()
     */
    unsigned int getFieldSize(); // override

    FieldHandle *lookupFieldHandle(TypePool *typePool, const std::string &fieldName); // override
    MethodHandle *lookupMethodHandle(TypePool *typePool, const std::string &methodName);    // override
    FieldHandle *findHandle(const std::string &fieldName); // override
    void accept(TypeVisitor *visitor); // override

    /**
     * call only once.
     */
    static void registerFuncInfo(NativeFuncInfo *info);
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

    /**
     * may be null, if Tuple template
     */
    native_type_info_t *info;
public:
    TypeTemplate(std::string &&name, std::vector<DSType*> &&elementTypes, native_type_info_t *info) :
            name(std::move(name)), acceptableTypes(std::move(elementTypes)), info(info) { }

    ~TypeTemplate() = default;

    const std::string &getName() const {
        return this->name;
    }

    unsigned int getElementTypeSize() const {
        return this->acceptableTypes.size();
    }

    native_type_info_t *getInfo() const {
        return this->info;
    }

    const std::vector<DSType *> &getAcceptableTypes() const {
        return this->acceptableTypes;
    }
};

} // namespace core
} // namespace ydsh

#endif /* CORE_DSTYPE_H_ */
