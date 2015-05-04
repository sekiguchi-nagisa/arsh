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

#ifndef CORE_FIELDHANDLE_H_
#define CORE_FIELDHANDLE_H_

#include <string>
#include <vector>
#include <unordered_map>

#include "../misc/flag_util.h"

namespace ydsh {
namespace core {

class TypePool;

class DSType;

class FunctionType;

struct NativeFuncInfo;

/**
 * represent for class field or variable. field type may be function type.
 */
class FieldHandle {
protected:
    DSType *fieldType;

private:
    /**
     * if index is -1, this handle dose not belong to handle table
     */
    int fieldIndex;

    /**
     * attribute bit map.
     */
    flag8_set_t attributeSet;

public:
    FieldHandle(DSType *fieldType, int fieldIndex, bool readOnly);

    virtual ~FieldHandle();

    virtual DSType *getFieldType(TypePool *typePool);

    /**
     * return -1, if this handle dose not belong to handle table
     */
    int getFieldIndex();

    void setAttribute(flag8_t attribute);

    void unsetAttribute(flag8_t attribute);

    /**
     * if includes targetAttr, return true.
     */
    bool hasAttribute(flag8_t targetAttr) const;

    /**
     * equivalent to this->hasAttribute(READ_ONLY).
     */
    bool isReadOnly() const;

    /**
     * equivalent to this->hasAttribute(GLOBAL).
     */
    bool isGlobal() const;

    bool isEnv() const;

    /**
     * if true, is FunctionHandle, equivalent to dynamic_cast<FunctionHandle*>(handle) != 0
     */
    bool isFuncHandle() const;

    // attribute definition
    const static flag8_t READ_ONLY   = 1 << 0;
    const static flag8_t GLOBAL      = 1 << 1;
    const static flag8_t ENV         = 1 << 2;
    const static flag8_t FUNC_HANDLE = 1 << 3;
    const static flag8_t INTERFACE   = 1 << 4;
};

/**
 * represent for function. used from DSType or SymbolTable.
 * function handle belongs to global scope is always treated as function.
 */
class FunctionHandle : public FieldHandle {  //FIXME:
protected:
    DSType *returnType;
    std::vector<DSType *> paramTypes;

    /**
     * contains parameter name and parameter index pair
     */
    std::unordered_map<std::string, unsigned int> paramIndexMap;

    /**
     * if true, has default value
     */
    std::vector<bool> defaultValues;

public:
    FunctionHandle(DSType *returnType, const std::vector<DSType *> &paramTypes);

    FunctionHandle(DSType *returnType, const std::vector<DSType *> &paramTypes, int fieldIndex);

    FunctionHandle(unsigned int paramSize, int fieldIndex);

    ~FunctionHandle();

    DSType *getFieldType(TypePool *typePool);   // override

    FunctionType *getFuncType(TypePool *typePool);

    DSType *getReturnType();

    const std::vector<DSType *> &getParamTypes();

    /**
     * return true if success, otherwise return false
     */
    bool addParamName(const std::string &paramName, bool defaultValue);

    /**
     * get index of parameter. if has no parameter, return -1
     */
    int getParamIndex(const std::string &paramName);

    /**
     * return true if the parameter of the index has default value, otherwise(not have, out of index) reurn false
     */
    bool hasDefaultValue(unsigned int paramIndex);
};

class MethodHandle {
protected:
    unsigned int methodIndex;

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
    MethodHandle(int methodIndex);
    virtual ~MethodHandle();

    unsigned int getMethodIndex();
    DSType *getReturnType();
    void setRecvType(DSType *type);
    DSType *getRecvType();
    const std::vector<DSType *> &getParamTypes();

    /**
     * initialize internal types.
     */
    void init(TypePool *typePool, NativeFuncInfo *info,
              DSType *elementType0 = 0, DSType *elementType1 = 0);

    /**
     * return always true, after call init().
     */
    bool initalized();

    void setNext(MethodHandle *handle);
    MethodHandle *getNext();

    virtual bool isInterfaceMethod();
};

/**
 * for D-Bus method
 */
class InterfaceMethodHandle : public MethodHandle {
public:
    InterfaceMethodHandle();
    ~InterfaceMethodHandle();

    void setReturnType(DSType *type);
    void addParamType(DSType *type);
    bool isInterfaceMethod(); // override
    bool isSignal();
};

} // namespace core
} // namespace ydsh

#endif /* CORE_FIELDHANDLE_H_ */
