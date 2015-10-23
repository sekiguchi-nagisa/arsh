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

#ifndef YDSH_FIELDHANDLE_H
#define YDSH_FIELDHANDLE_H

#include <string>
#include <vector>
#include <unordered_map>

#include "../misc/flag_util.hpp"

namespace ydsh {
namespace core {

using namespace ydsh::misc;

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
    unsigned int fieldIndex;

    /**
     * attribute bit map.
     */
    flag8_set_t attributeSet;

public:
    // attribute definition
    static constexpr flag8_t READ_ONLY   = 1 << 0;
    static constexpr flag8_t GLOBAL      = 1 << 1;
    static constexpr flag8_t ENV         = 1 << 2;
    static constexpr flag8_t FUNC_HANDLE = 1 << 3;
    static constexpr flag8_t INTERFACE   = 1 << 4;

    FieldHandle(DSType *fieldType, unsigned int fieldIndex, bool readOnly) :
            fieldType(fieldType), fieldIndex(fieldIndex), attributeSet(0) {
        if(readOnly) {
            this->setAttribute(READ_ONLY);
        }
    }

    virtual ~FieldHandle() = default;

    virtual DSType *getFieldType(TypePool *typePool);

    unsigned int getFieldIndex() const {
        return this->fieldIndex;
    }

    std::string toString() const;
    void setAttribute(flag8_t attribute) {
        setFlag(this->attributeSet, attribute);
    }

    void unsetAttribute(flag8_t attribute) {
        unsetFlag(this->attributeSet, attribute);
    }

    /**
     * if includes targetAttr, return true.
     */
    bool hasAttribute(flag8_t targetAttr) const {
        return hasFlag(this->attributeSet, targetAttr);
    }

    /**
     * equivalent to this->hasAttribute(READ_ONLY).
     */
    bool isReadOnly() const {
        return this->hasAttribute(READ_ONLY);
    }

    /**
     * equivalent to this->hasAttribute(GLOBAL).
     */
    bool isGlobal() const {
        return this->hasAttribute(GLOBAL);
    }

    bool isEnv() const {
        return this->hasAttribute(ENV);
    }

    /**
     * if true, is FunctionHandle, equivalent to dynamic_cast<FunctionHandle*>(handle) != 0
     */
    bool isFuncHandle() const {
        return this->hasAttribute(FUNC_HANDLE);
    }

    bool withinInterface() const {
        return this->hasAttribute(INTERFACE);
    }
};

/**
 * represent for function. used from DSType or SymbolTable.
 * function handle belongs to global scope is always treated as function.
 */
class FunctionHandle : public FieldHandle {  //FIXME:
protected:
    DSType *returnType;
    std::vector<DSType *> paramTypes;

public:
    FunctionHandle(DSType *returnType, const std::vector<DSType *> &paramTypes, unsigned int fieldIndex) :
            FieldHandle(0, fieldIndex, true),
            returnType(returnType), paramTypes(paramTypes) {
        this->setAttribute(FUNC_HANDLE);
    }

    ~FunctionHandle() = default;

    DSType *getFieldType(TypePool *typePool);   // override

    FunctionType *getFuncType(TypePool *typePool);

    DSType *getReturnType() const {
        return this->returnType;
    }

    const std::vector<DSType *> &getParamTypes();
};

class MethodHandle {
protected:
    unsigned int methodIndex;
    flag8_set_t attributeSet;

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
    explicit MethodHandle(int methodIndex) :
            methodIndex(methodIndex), attributeSet(),
            returnType(), recvType(), paramTypes(), next() { }

    ~MethodHandle();

    unsigned int getMethodIndex() const {
        return this->methodIndex;
    }

    void setReturnType(DSType *type) {
        this->returnType = type;
    }

    DSType *getReturnType() const {
        return this->returnType;
    }

    void setRecvType(DSType *type) {
        this->recvType = type;
    }

    DSType *getRecvType() const {
        return this->recvType;
    }

    void addParamType(DSType *type);

    const std::vector<DSType *> &getParamTypes() const {
        return this->paramTypes;
    }

    /**
     * initialize internal types.
     */
    void init(TypePool *typePool, NativeFuncInfo &info,
              const std::vector<DSType *> *types = 0);

    /**
     * return always true, after call init().
     */
    bool initalized() const {
        return this->returnType != 0;
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

    bool isSignal();

    static constexpr flag8_t INTERFACE    = 1 << 0;
    static constexpr flag8_t MULTI_RETURN = 1 << 1;
};

} // namespace core
} // namespace ydsh

#endif //YDSH_FIELDHANDLE_H
