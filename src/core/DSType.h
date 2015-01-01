/*
 * DSType.h
 *
 *  Created on: 2014/12/31
 *      Author: skgchxngsxyz-osx
 */

#ifndef CORE_DSTYPE_H_
#define CORE_DSTYPE_H_

//#include <unordered_map>
#include "CalleeHandle.h"

class DSType {
public:
	DSType();
	virtual ~DSType();

	/**
	 * string representation of this type
	 */
	std::string getTypeName() = 0;

	/**
	 * if true, can extend this type
	 */
	bool isExtendable() = 0;

	/**
	 * get super type of this type.
	 * return null, if has no super type.
	 */
	DSType *getSuperType() = 0;

	/**
	 * get size of field.
	 */
	int getFieldSize() = 0;

	bool equals(DSType *targetType);

	/**
	 * check inheritance of target type.
	 * if this type is equivalent to target type or
	 * the super type of target type, return true.
	 */
	bool isAssignableFrom(DSType *targetType);
};

class ClassType : public DSType {	//TODO: add field index map, read only bitmap
private:
	DSType *superType;

	/**
	 * string representation of this class.
	 */
	std::string className;

	/**
	 * if true, can extend this class.
	 */
	bool extendable;

	/**
	 * may be null, if has no constructor.
	 */
	ConstructorHandle *constructorHandle;

	/**
	 * size of field handle table size
	 */
	int handleSize;

	/**
	 * may be null if has no field (handleSize == 0)
	 */
	FieldHandle **handleTable;

public:
	ClassType(std::string className, bool extendable, DSType *superType);
	~ClassType();

	/**
	 * may be null, if has no constructor
	 */
	ConstructorHandle *getConstructorHandle();
	void setConstructorHandle(ConstructorHandle *handle);

	std::string getTypeName();	// override
	bool isExtendable();	// override
	DSType *getSuperType();	// override
	int getFieldSize();	// override
};

class FunctionType : public DSType {
private:
	DSType *returnType;

	/**
	 * may be 0, if has no parameter
	 */
	int paramSize;

	/**
	 * may be null, if has no parameter
	 */
	DSType **paramTypes;

public:
	FunctionType(DSType *returnType, int paramSize, DSType **paramTypes);
	~FunctionType();

	DSType *getReturnType();
	int getParamSize();

	/**
	 * may be null, if has no parameter (getParamSize() == 0)
	 */
	DSType **getParamTypes();

	std::string getTypeName();	// override
	bool isExtendable();	// override
	DSType *getSuperType();	// override
	int getFieldSize();	// override
};

std::string toFunctionTypeName(DSType *returnType, int paramSize, DSType **paramTypes);


#endif /* CORE_DSTYPE_H_ */
