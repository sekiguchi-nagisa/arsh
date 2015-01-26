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
#include <utility>
#include <vector>
#include <unordered_map>

#include <core/TypePool.h>

class DSType;
class FunctionType;

/**
 * represent for class field. field type may be function type.
 */
class FieldHandle {
protected:
    DSType *fieldType;

private:
    /**
     * if index is -1, this handle dose not belong to handle table
     */
    int fieldIndex;

    bool readOnly;

public:
    FieldHandle(DSType *fieldType, int fieldIndex, bool readOnly);
    virtual ~FieldHandle();

    virtual DSType *getFieldType(TypePool *typePool);

    /**
     * return -1, if this handle dose not belong to handle table
     */
    int getFieldIndex();

    bool isReadOnly();
};

/**
 * represent for method or function. used from DSType or SymbolTable.
 * function handle belongs to DSType is always treated as method.
 * function handle belongs to global scope is always treated as function.
 */
class FunctionHandle: public FieldHandle {
private:
    DSType *returnType;
    std::vector<DSType*> paramTypes;

    /**
     * contains parameter name and parameter index pair
     */
    std::unordered_map<std::string, int> paramIndexMap;

    /**
     * if true, has default value
     */
    std::vector<bool> defaultValues;

public:
    FunctionHandle(DSType *returnType, const std::vector<DSType*> &paramTypes);
    FunctionHandle(DSType *returnType, const std::vector<DSType*> &paramTypes, int fieldIndex);
    virtual ~FunctionHandle();

    virtual DSType *getFieldType(TypePool *typePool);   // override

    FunctionType *getFuncType(TypePool *typePool);
    DSType *getReturnType(TypePool *typePool);
    const std::vector<DSType*> &getParamTypes(TypePool *typePool);

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
    bool hasDefaultValue(int paramIndex);
};

class ConstructorHandle : public FunctionHandle {
public:
    ConstructorHandle(const std::vector<DSType*> &paramTypes);
    ~ConstructorHandle();

    /**
     * return always null
     */
    DSType *getFieldType(TypePool *typePool);   // override
};

#endif /* CORE_FIELDHANDLE_H_ */
