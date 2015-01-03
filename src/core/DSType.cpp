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


// ############################
// ##     UnresolvedType     ##
// ############################

bool UnresolvedType::isExtendable() {
	return false;
}

DSType *UnresolvedType::getSuperType() {
	return 0;
}

int UnresolvedType::getFieldSize() {
	return 0;
}


// #################################
// ##     UnresolvedClassType     ##
// #################################

UnresolvedClassType::UnresolvedClassType(std::string typeName) :
		typeName(typeName) {
}

std::string UnresolvedClassType::getTypeName() {
	return this->typeName;
}

DSType *UnresolvedClassType::toType() {
	return 0;	//TODO:
}


// ###################################
// ##     UnresolvedReifiedType     ##
// ###################################

UnresolvedReifiedType::UnresolvedReifiedType(UnresolvedType *templateType):
	templateType(templateType), elementTypes(2) {
}

UnresolvedReifiedType::~UnresolvedReifiedType() {
	delete this->templateType;
	int size = this->elementTypes.size();
	for(int i = 0; i < size; i++) {
		delete this->elementTypes[i];
	}
	this->elementTypes.clear();
}

std::string UnresolvedReifiedType::getTypeName() {
	int size = this->elementTypes.size();
	DSType **types = new DSType*[size];
	for(int i = 0; i < size; i++) {
		types[i] = this->elementTypes[i];
	}
	return toReifiedTypeName(this->templateType, size, types);
}

void UnresolvedReifiedType::addElementType(UnresolvedType *type) {
	this->elementTypes.push_back(type);
}

std::vector<UnresolvedType*> UnresolvedReifiedType::getElementTypes() {
	return this->elementTypes;
}

DSType *UnresolvedReifiedType::toType() {
	return 0;	//TODO:
}


std::string toReifiedTypeName(DSType *templateType, int elementSize, DSType **elementTypes) {
	std::string reifiedTypeName = templateType->getTypeName() + "<";
	for(int i = 0; i < elementSize; i++) {
		if(i > 0) {
			reifiedTypeName += ",";
		}
		reifiedTypeName += elementTypes[i]->getTypeName();
	}
	reifiedTypeName += ">";
	return reifiedTypeName;
}

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


// ################################
// ##     UnresolvedFuncType     ##
// ################################

UnresolvedFuncType::UnresolvedFuncType():
		returnType(0), paramTypes(2) {
}

UnresolvedFuncType::~UnresolvedFuncType() {
	if(this->returnType != 0) {
		delete this->returnType;
	}
	int size = this->paramTypes.size();
	for(int i = 0; i < size; i++) {
		delete this->paramTypes[i];
	}
	this->paramTypes.clear();
}

std::string UnresolvedFuncType::getTypeName() {	//TODO: complete Void type
	int size = this->paramTypes.size();
	DSType** types = new DSType*[size];
	for(int i = 0; i < size; i++) {
		types[i] = this->paramTypes[i];
	}
	return toFunctionTypeName(this->returnType, size, types);	//TODO: add null check for return type
}

void UnresolvedFuncType::setReturnType(UnresolvedType *type) {
	this->returnType = type;
}

UnresolvedType *UnresolvedFuncType::getReturnType() {
	return this->returnType;	//TODO: add null check
}

void UnresolvedFuncType::addParamType(UnresolvedType *type) {
	this->paramTypes.push_back(type);
}

std::vector<UnresolvedType*> UnresolvedFuncType::getParamTypes() {
	return this->paramTypes;
}

//TODO: add TypePool to parameter
DSType *UnresolvedFuncType::toType() {
	return 0;
}


// #######################
// ##     ClassType     ##
// #######################

ClassType::ClassType(std::string className, bool extendable, DSType *superType):
		superType(superType), className(className), extendable(extendable),
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
