/*
 * DSType.h
 *
 *  Created on: 2014/12/31
 *      Author: skgchxngsxyz-osx
 */

#ifndef CORE_DSTYPE_H_
#define CORE_DSTYPE_H_

#include <unordered_map>
#include <vector>
#include <string>
#include <utility>

#include "CalleeHandle.h"

class DSType {
public:
    DSType();
    virtual ~DSType();

    /**
     * string representation of this type
     */
    virtual std::string getTypeName() = 0;	// must not reference value

    /**
     * if true, can extend this type
     */
    virtual bool isExtendable() = 0;

    /**
     * get super type of this type.
     * return null, if has no super type(ex. AnyType, VoidType).
     */
    virtual DSType *getSuperType() = 0;

    /**
     * return null, if has no constructor
     */
    virtual ConstructorHandle *getConstructorHandle() = 0;

    /**
     * get size of the all fields(include superType fieldSize).
     */
    virtual unsigned int getFieldSize() = 0;

    /**
     * return -1, if has no field
     */
    virtual int getFieldIndex(const std::string &fieldName) = 0;

    /**
     * return true, found field
     * equivalent to getFieldIndex() != -1
     */
    bool hasField(const std::string &fieldName);

    /**
     * return null, if index < -1 && index >= getFieldSize()
     */
    virtual FieldHandle *lookupFieldHandle(int fieldIndex) = 0;

    /**
     * equivalent to lookupFieldHandle(getFieldIndex())
     */
    FieldHandle *lookupFieldHandle(const std::string &fieldName);

    /**
     * return true if read only
     */
    virtual bool isReadOnly(int fieldIndex) = 0;

    /**
     * equivalent to isReadOnly(getFieldIndex())
     */
    bool isReadOnly(const std::string &fieldName);

    /**
     * check equality
     */
    virtual bool equals(DSType *targetType) = 0;

    /**
     * check inheritance of target type.
     * if this type is equivalent to target type or
     * the super type of target type, return true.
     */
    virtual bool isAssignableFrom(DSType *targetType);
};

class ClassType: public DSType {	//TODO: add field index map, read only bitmap
private:
    DSType *superType;

    /**
     * handleTable base index
     */
    const int baseIndex;

    /**
     * string representation of this class.
     */
    const std::string className;

    /**
     * if true, can extend this class.
     */
    const bool extendable;

    /**
     * may be null, if has no constructor.
     */
    ConstructorHandle *constructorHandle;

    /**
     * contains fieldName and handleTableIndex pair
     */
    std::unordered_map<std::string, int> fieldIndexMap;

    std::vector<FieldHandle*> handleTable;

    /**
     * if field is read only, flag is true
     */
    std::vector<bool> handleFlags;

public:
    /**
     * superType may be null (Any or Void Type)
     */
    ClassType(std::string &&className, bool extendable, DSType *superType);
    ~ClassType();

    std::string getTypeName();	// override
    bool isExtendable();	// override
    DSType *getSuperType();	// override
    ConstructorHandle *getConstructorHandle();	// override
    void setConstructorHandle(ConstructorHandle *handle);
    unsigned int getFieldSize();	// override
    int getFieldIndex(const std::string &fieldName);	// override
    FieldHandle *lookupFieldHandle(int fieldIndex);	// override
    bool isReadOnly(int fieldIndex);	// override
    bool equals(DSType *targetType);	// override

    /**
     * return false, found duplicated field.
     */
    bool addFieldHandle(const std::string &fieldName, bool readOnly, FieldHandle *handle);

    static DSType *anyType;
    static DSType *voidType;
};

class FunctionType: public DSType {
private:
    DSType *returnType;

    /**
     * may be 0, if has no parameter
     */
    unsigned int paramSize;

    /**
     * may be null, if has no parameter
     */
    DSType **paramTypes;

public:
    FunctionType(DSType *returnType, unsigned int paramSize, DSType **paramTypes);
    ~FunctionType();

    DSType *getReturnType();
    unsigned int getParamSize();

    /**
     * may be null, if has no parameter (getParamSize() == 0)
     */
    DSType **getParamTypes();

    std::string getTypeName();	// override
    bool isExtendable();	// override

    /**
     * return always anyType
     */
    DSType *getSuperType();	// override

    /**
     * return always null
     */
    ConstructorHandle *getConstructorHandle();	// override

    unsigned int getFieldSize();	// override
    int getFieldIndex(const std::string &fieldName);	// override

    /**
     * return always null
     */
    FieldHandle *lookupFieldHandle(int fieldIndex);	// override

    bool isReadOnly(int fieldIndex);	// override

    bool equals(DSType *targetType);	// override
};

/**
 * create reified type name
 */
std::string toReifiedTypeName(DSType *templateType, int elementSize, DSType **elementTypes);

/**
 * create function type name
 */
std::string toFunctionTypeName(DSType *returnType, int paramSize, DSType **paramTypes);

#endif /* CORE_DSTYPE_H_ */
