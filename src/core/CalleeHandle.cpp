/*
 * CalleeHandle.cpp
 *
 *  Created on: 2015/01/01
 *      Author: skgchxngsxyz-osx
 */

#include "CalleeHandle.h"
#include "DSType.h"

// #########################
// ##     FieldHandle     ##
// #########################

FieldHandle::FieldHandle(DSType *fieldType):
		fieldType(fieldType) {
}

DSType *FieldHandle::getFieldType() {
	return this->fieldType;
}


// ############################
// ##     FunctionHandle     ##
// ############################

FunctionHandle::FunctionHandle(FunctionType *funcType):
		FieldHandle(funcType) {
}

FunctionType *FunctionHandle::getFuncType() {
	return dynamic_cast<FunctionType*>(this->getFieldType());
}


// ###############################
// ##     ConstructorHandle     ##
// ###############################

ConstructorHandle::ConstructorHandle(int paramSize, DSType **paramTypes):
		paramSize(paramSize), paramTypes(paramTypes) {
}

ConstructorHandle::~ConstructorHandle() {
	if(this->paramTypes != 0) {
		delete[] this->paramTypes;
	}
}

int ConstructorHandle::getParamSize() {
	return this->paramSize;
}

DSType **ConstructorHandle::getParamTypes() {
	return this->paramTypes;
}
