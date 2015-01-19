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

class DSType;
class FunctionType;

class FieldHandle {
private:
    DSType *fieldType;

    /**
     * if index is -1, this handle dose not belong to handle table
     */
    int fieldIndex;

    bool readOnly;

public:
    FieldHandle(DSType *fieldType, int fieldIndex, bool readOnly);
    virtual ~FieldHandle();

    DSType *getFieldType();

    /**
     * return -1, if this handle dose not belong to handle table
     */
    int getFieldIndex();

    bool isReadOnly();
};

class FunctionHandle: public FieldHandle {
private:
    /**
     * contains parameter name and parameter index pair
     */
    std::unordered_map<std::string, int> paramIndexMap;

    /**
     * if true, has default value
     */
    std::vector<bool> defaultValues;

public:
    FunctionHandle(FunctionType *funcType);
    FunctionHandle(FunctionType *funcType, int fieldIndex);
    virtual ~FunctionHandle();

    FunctionType *getFuncType();

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

/**
 * getFieldType() or getFuncType() is always null
 */
class ConstructorHandle : public FunctionHandle {
private:
    unsigned int paramSize;

    /**
     * may be null, if has no parameter (paramSize == 0)
     */
    DSType** paramTypes;

public:
    ConstructorHandle(unsigned int paramSize, DSType **paramTypes);
    ~ConstructorHandle();

    unsigned int getParamSize();

    /**
     * may be null, if has no parameter (getParamSize() == 0)
     */
    DSType **getParamTypes();
};

#endif /* CORE_CALLEEHANDLE_H_ */
