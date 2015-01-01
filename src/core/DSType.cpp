/*
 * DSType.cpp
 *
 *  Created on: 2014/12/31
 *      Author: skgchxngsxyz-osx
 */

#include "DSType.h"

// ####################
// ##     DSType     ##
// ####################

DSType::DSType(){
}

DSType::~DSType() {
}

bool DSType::equals(DSType *targetType) {
	return this->getTypeName() == targetType->getTypeName();
}

bool DSType::isAssignableFrom(DSType *targetType) {
	if(this->equals(targetType)) {
		return true;
	}
	DSType *superType = targetType->getSuperType();
	return superType != 0 && this->isAssignableFrom(superType);
}


// #######################
// ##     ClassType     ##
// #######################

ClassType::ClassType(std::string className, bool extendable, DSType *superType):
		className(className), extendable(extendable), superType(superType),
		constructorHandle(0), handleSize(0), handleTable(0) {
}

ClassType::~ClassType() {
	if(this->handleTable != 0) {
		delete[] this->handleTable;
	}
}

/**
 * may be null, if has no constructor
 */
ConstructorHandle *ClassType::getConstructorHandle() {
	return this->constructorHandle;
}

void ClassType::setConstructorHandle(ConstructorHandle *handle) {
	this->constructorHandle = handle;
}

std::string ClassType::getTypeName() {
	return this->className;
}

bool ClassType::isExtendable() {
	return this->extendable;
}

DSType *ClassType::getSuperType() {
	return this->superType;
}

int ClassType::getFieldSize() {
	return this->handleSize;
}


// ##########################
// ##     FunctionType     ##
// ##########################

FunctionType::FunctionType(DSType *returnType, int paramSize, DSType **paramTypes):
		returnType(returnType), paramSize(paramSize), paramTypes(paramTypes) {
}

FunctionType::~FunctionType() {
	if(this->paramTypes != 0) {
		delete[] this->paramTypes;
	}
}

DSType *FunctionType::getReturnType() {
	return this->returnType;
}

int FunctionType::getParamSize() {
	return this->paramSize;
}

DSType **FunctionType::getParamTypes() {
	return this->paramTypes;
}

std::string FunctionType::getTypeName() {
	return toFunctionTypeName(this->returnType, this->paramSize, this->paramTypes);
}

bool FunctionType::isExtendable() {
	return false;
}

DSType *FunctionType::getSuperType() {
	return 0;	//TODO: super type must be any type
}

int FunctionType::getFieldSize() {
	return 0;
}

/**
 * create function type name
 */
std::string toFunctionTypeName(DSType *returnType, int paramSize, DSType **paramTypes) {
	std::string funcTypeName = "Func<" + returnType->getTypeName();
	for(int i = 0; i < paramSize; i++) {
		if(i == 0) {
			funcTypeName += ",[";
		}
		if(i > 0) {
			funcTypeName += ",";
		}
		funcTypeName += paramTypes[i]->getTypeName();
		if(i == paramSize - 1) {
			funcTypeName += "]";
		}
	}
	funcTypeName += ">";
	return funcTypeName;
}
