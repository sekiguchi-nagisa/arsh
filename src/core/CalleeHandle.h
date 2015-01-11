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

class FieldHandle {	//TODO: access level
private:
	DSType *fieldType;

public:
	FieldHandle(DSType *fieldType);

	DSType *getFieldType();
};


class FunctionHandle : public FieldHandle {	//TODO: named parameter, default parameter
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


class ConstructorHandle {	//TODO: named parameter. default parameter
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
