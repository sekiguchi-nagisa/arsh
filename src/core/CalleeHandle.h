/*
 * CalleeHandle.h
 *
 *  Created on: 2015/01/01
 *      Author: skgchxngsxyz-osx
 */

#ifndef CORE_CALLEEHANDLE_H_
#define CORE_CALLEEHANDLE_H_

// base class of callee(field, function, constructor)
class CalleeHandle {
public:
	CalleeHandle();
	virtual ~CalleeHandle();
};

struct DSType;

class FieldHandle : public CalleeHandle {
private:
	DSType *fieldType;

public:
	FieldHandle(DSType *fieldType);

	DSType *getFieldType();
};

class FunctionType;

class FunctionHandle : public FieldHandle {	//TODO: named parameter, default parameter
public:
	FunctionHandle(FunctionType *funcType);

	FunctionType *getFuncType();
};


class ConstructorHandle : public CalleeHandle {	//TODO: named parameter. default parameter
private:
	int paramSize;

	/**
	 * may be null, if has no parameter (paramSize == 0)
	 */
	DSType** paramTypes;

public:
	ConstructorHandle(int paramSize, DSType **paramTypes);
	~ConstructorHandle();

	int getParamSize();

	/**
	 * may be null, if has no parameter (getParamSize() == 0)
	 */
	DSType **getParamTypes();
};

#endif /* CORE_CALLEEHANDLE_H_ */
