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

#include "../misc/flag_util.h"
#include "handle_info.h"
#include "TypePool.h"

namespace ydsh {
namespace core {

struct DSObject;
struct FuncObject;
class MethodRef;

class FieldHandle;
class MethodHandle;
class InterfaceMethodHandle;

struct RuntimeContext;
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

    virtual ~DSType();

    /**
     * if true, can extend this type
     */
    bool isExtendable() const;

    /**
     * if this type is VoidType, return true.
     */
    bool isVoidType() const;

    /**
     * if this type is FunctionType, return true.
     */
    bool isFuncType() const;

    /**
     * if this type is InterfaceType, return true.
     */
    bool isInterface() const;

    /**
     * return true, if type is builtin type, reified type, tuple type, error type.
     */
    virtual bool isBuiltinType() const;

    /**
     * get super type of this type.
     * return null, if has no super type(ex. AnyType, VoidType).
     */
    DSType *getSuperType() const;

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

    bool operator==(const DSType &type);
    bool operator!=(const DSType &type);

    /**
     * check inheritance of target type.
     * if this type is equivalent to target type or
     * the super type of target type, return true.
     */
    virtual bool isAssignableFrom(DSType *targetType);

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
                 DSType *returnType, const std::vector<DSType *> &paramTypes);

    ~FunctionType();

    DSType *getReturnType();

    /**
     * may be empty vector, if has no parameter (getParamSize() == 0)
     */
    const std::vector<DSType *> &getParamTypes();

    /**
     * lookup from super type
     */
    MethodHandle *lookupMethodHandle(TypePool *typePool, const std::string &methodName);    // override

    FieldHandle *findHandle(const std::string &fieldName);  // override
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
 * for BuiltinType creation.
 */
DSType *newBuiltinType(bool extendable, DSType *superType,
                       native_type_info_t *info, bool isVoid = false);

/**
 * for ReifiedType creation.
 * reified type is not public class.
 */
DSType *newReifiedType(native_type_info_t *info, DSType *superType,
                       const std::vector<DSType *> &elementTypes);


class TupleType : public DSType {
private:
    std::vector<DSType *> types;
    std::unordered_map<std::string, FieldHandle *> fieldHandleMap;

    /**
     * may be null, if has more than two element.
     */
    MethodHandle *constructorHandle;

    static NativeFuncInfo *funcInfo;
    static std::shared_ptr<MethodRef> initRef;

public:
    /**
     * superType is always AnyType
     */
    TupleType(DSType *superType, const std::vector<DSType *> &types);
    ~TupleType();

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
    FieldHandle *findHandle(const std::string &fieldName); // override

    /**
     * call only once.
     */
    static void registerFuncInfo(NativeFuncInfo *info);
};

/**
 * for TupleType creation
 */
DSType *newTupleType(DSType *superType, const std::vector<DSType *> &elementTypes);

/**
 * for D-Bus interface
 */
class InterfaceType : public DSType {
private:
    std::unordered_map<std::string, FieldHandle *> fieldHandleMap;
    std::unordered_map<std::string, InterfaceMethodHandle *> methodHandleMap;

public:
    /**
     * superType is always AnyType.
     */
    InterfaceType(DSType *superType);

    ~InterfaceType();

    /**
     * return null, if found duplicated field
     */
    FieldHandle *newFieldHandle(const std::string &fieldName, DSType *fieldType, bool readOnly);

    InterfaceMethodHandle *newMethodHandle(const std::string &methodName);

    unsigned int getFieldSize();    // override
    unsigned int getMethodSize();   // override
    FieldHandle *lookupFieldHandle(TypePool *typePool, const std::string &fieldName);   // override
    MethodHandle *lookupMethodHandle(TypePool *typePool, const std::string &methodName); // override
    FieldHandle *findHandle(const std::string &fieldName); // overrdie
};

class ErrorType : public DSType {
private:
    MethodHandle *constructorHandle;

    static NativeFuncInfo *funcInfo;
    static std::shared_ptr<MethodRef> initRef;

public:
    ErrorType(DSType *superType);
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

    /**
     * call only once.
     */
    static void registerFuncInfo(NativeFuncInfo *info);
};

} // namespace core
} // namespace ydsh

#endif /* CORE_DSTYPE_H_ */
