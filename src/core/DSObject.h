/*
 * DSObject.h
 *
 *  Created on: 2014/12/31
 *      Author: skgchxngsxyz-osx
 */

#ifndef CORE_DSOBJECT_H_
#define CORE_DSOBJECT_H_

#include "DSType.h"

class FunctionNode;
class RuntimeContext;

class DSObject {
public:
	DSObject();
	virtual ~DSObject();

	/**
	 * get object type
	 */
	virtual DSType *getType() = 0;

	/**
	 * retunr 0, if has no field
	 */
	virtual int getFieldSize() = 0;

	/**
	 * for field(or method) lookup
	 * fieldIndex > -1 && fieldIndex < getFieldSize()
	 * this method is not type-safe.
	 */
	virtual DSObject *lookupField(int fieldIndex) = 0;
};


class BaseObject : public DSObject {
protected:
	DSType *type;

	int fieldSize;

	/**
	 * may be null, if has no field. (fieldSize == 0)
	 */
	DSObject **fieldTable;

public:
	BaseObject(DSType *type);
	~BaseObject();

	DSType *getType();	// override
	int getFieldSize();	// override
	DSObject *lookupField(int fieldIndex);	// override
};


class Int64_Object : public BaseObject {
private:
	long value;

public:
	Int64_Object(DSType *type, long value);

	long getValue();
};


class Float_Object : public BaseObject {
private:
	double value;

public:
	Float_Object(DSType *type, double value);

	double getValue();
};


class Boolean_Object : public BaseObject {
private:
	bool value;

public:
	Boolean_Object(DSType *type, bool value);

	bool getValue();
};


class String_Object : public BaseObject {
private:
	std::string value;

public:
	String_Object(DSType *type, std::string &&value);

	const std::string &getValue();
};


class FuncObject : public DSObject {
private:
	/**
	 * may be null, but finally must be not null
	 */
	FunctionType *funcType;

public:
	FuncObject(FunctionType *funcType);

	DSType *getType();	// override

	/**
	 * return always 0
	 */
	int getFieldSize();	// override

	/**
	 * return always null
	 */
	DSObject *lookupField(int fieldIndex);	// override
//
//	/**
//	 * set function type. must be called only once when function type is resolved.
//	 */
//	void setFuncType(FunctionType *funcType);

	/**
	 * equivalent to getType()
	 */
	FunctionType *getFuncType();
};


/*
 * for user defined function
 */
class UserFuncObject : public FuncObject {
private:
	FunctionNode *funcNode;

public:
	UserFuncObject(FunctionType *funcType, FunctionNode *funcNode);
	~UserFuncObject();

	FunctionNode *getFuncNode();
};


/**
 * for builtin(native) function
 */
class BuiltinFuncObject : public FuncObject {
};

#endif /* CORE_DSOBJECT_H_ */
