/*
 * DSType.h
 *
 *  Created on: 2014/12/31
 *      Author: skgchxngsxyz-osx
 */

#ifndef CORE_DSTYPE_H_
#define CORE_DSTYPE_H_

//#include <unordered_map>
#include <vector>
#include <string>
#include "CalleeHandle.h"

struct DSType {
public:
	DSType();
	virtual ~DSType();

	/**
	 * string representation of this type
	 */
	virtual std::string getTypeName() = 0;

	/**
	 * if true, can extend this type
	 */
	virtual bool isExtendable() = 0;

	/**
	 * get super type of this type.
	 * return null, if has no super type.
	 */
	virtual DSType *getSuperType() = 0;

	/**
	 * get size of field.
	 */
	virtual int getFieldSize() = 0;

	bool equals(DSType *targetType);

	/**
	 * check inheritance of target type.
	 * if this type is equivalent to target type or
	 * the super type of target type, return true.
	 */
	virtual bool isAssignableFrom(DSType *targetType);
};


/**
 * represent for parsed type.
 */
class UnresolvedType : public DSType {
public:
	virtual std::string getTypeName() = 0;

	/**
	 * return always false
	 */
	bool isExtendable();

	/**
	 * return always null
	 */
	DSType *getSuperType();

	/**
	 * return always 0
	 */
	int getFieldSize();

	virtual DSType *toType() = 0;	//TODO: add TypePool to parameter
};


class UnresolvedClassType : public UnresolvedType {
private:
	std::string typeName;

public:
	UnresolvedClassType(std::string typeName);

	std::string getTypeName();	// override
	DSType *toType();	// override
};


class UnresolvedReifiedType : public UnresolvedType {
private:
	UnresolvedType *templateType;
	std::vector<UnresolvedType*> elementTypes;

public:
	UnresolvedReifiedType(UnresolvedType *templateType);
	~UnresolvedReifiedType();

	std::string getTypeName();	// override
	void addElementType(UnresolvedType *type);
	std::vector<UnresolvedType*> getElementTypes();
	//TODO: add TypePool to parameter
	DSType *toType();	// override
};


/**
 * create reified type name
 */
std::string toReifiedTypeName(DSType *templateType, int elementSize, DSType **elementTypes);

/**
 * create function type name
 */
std::string toFunctionTypeName(DSType *returnType, int paramSize, DSType **paramTypes);


class UnresolvedFuncType : public UnresolvedType {
private:
	/**
	 * may be null, if has return type annotation (return void)
	 */
	UnresolvedType *returnType;

	/**
	 * may be empty vector, if has no parameter
	 */
	std::vector<UnresolvedType *> paramTypes;

public:
	UnresolvedFuncType();
	~UnresolvedFuncType();

	std::string getTypeName();	// override
	void setReturnType(UnresolvedType *type);
	UnresolvedType *getReturnType();
	void addParamType(UnresolvedType *type);
	std::vector<UnresolvedType*> getParamTypes();
	//TODO: add TypePool to parameter
	DSType *toType();	// override
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


#endif /* CORE_DSTYPE_H_ */
