/*
 * CalleeHandle.h
 *
 *  Created on: 2015/01/01
 *      Author: skgchxngsxyz-osx
 */

#ifndef CORE_CALLEEHANDLE_H_
#define CORE_CALLEEHANDLE_H_

#include <string>
#include <utility>
#include <vector>
#include <unordered_map>

#include "TypePool.h"

class DSType;
class FunctionType;

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
    FunctionHandle(DSType *returnType, const std::vector<DSType*> paramTypes);
    FunctionHandle(DSType *returnType, const std::vector<DSType*> paramTypes, int fieldIndex);
    virtual ~FunctionHandle();

    virtual DSType *getFieldType(TypePool *typePool);   // override

    FunctionType *getFuncType(TypePool *typePool);
    DSType *getReturnType();
    const std::vector<DSType*> &getParamTypes();

    /**
     * return null if has no parameter
     */
    DSType *getFirstParamType();

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

#endif /* CORE_CALLEEHANDLE_H_ */
