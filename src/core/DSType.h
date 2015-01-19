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
     * return true, found field
     * equivalent to lookupFieldHandle() != 0
     */
    bool hasField(const std::string &fieldName);

    /**
     * return null, if has no field
     */
    virtual FieldHandle *lookupFieldHandle(const std::string &fieldName) = 0;

    /**
     * equivalent to dynamic_cast<FunctionHandle*>(lookupFieldHandle())
     */
    FunctionHandle *lookupFunctionHandle(const std::string &funcName);

    /**
     * first parameter type is equivalent to this type
     */
    FunctionHandle *lookupMethodHandle(const std::string &methodName);

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

    std::unordered_map<std::string, FieldHandle*> handleMap;

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
    unsigned int getFieldSize();	// override
    FieldHandle *lookupFieldHandle(const std::string &fieldName);	// override
    bool equals(DSType *targetType);	// override

    /**
     * return false, found duplicated field.
     */
    bool addNewFieldHandle(const std::string &fieldName, bool readOnly, DSType *fieldType);

    /**
     * return created function handle.
     * return null, found duplicated field.
     */
    FunctionHandle *addNewFunctionHandle(const std::string &funcName, FunctionType *funcType);

    /**
     * return created constructor handle
     */
    ConstructorHandle *setNewConstructorHandle(unsigned int paramSize, DSType **paramTypes);

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
    FunctionType(DSType *returnType, const std::vector<DSType*> &paramTypes);
    ~FunctionType();

    DSType *getReturnType();
    unsigned int getParamSize();

    /**
     * may be null, if has no parameter (getParamSize() == 0)
     */
    DSType **getParamTypes();

    /**
     * may be null, if has no parameter
     */
    DSType *getFirstParamType();

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

    /**
     * return always null
     */
    FieldHandle *lookupFieldHandle(const std::string &fieldName);	// override

    bool equals(DSType *targetType);	// override
};

/**
 * create reified type name
 */
std::string toReifiedTypeName(DSType *templateType, const std::vector<DSType*> &elementTypes);

/**
 * create function type name
 */
std::string toFunctionTypeName(DSType *returnType, const std::vector<DSType*> &paramTypes);

#endif /* CORE_DSTYPE_H_ */
