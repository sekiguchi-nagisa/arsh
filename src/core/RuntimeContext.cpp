/*
 * RuntimeContext.cpp
 *
 *  Created on: 2015/01/07
 *      Author: skgchxngsxyz-osx
 */

#include "RuntimeContext.h"

// ############################
// ##     RuntimeContext     ##
// ############################

RuntimeContext::RuntimeContext():
		globalVarTable(), thrownObject(0) {
}

RuntimeContext::~RuntimeContext() {
}

void RuntimeContext::addGlobalVar(DSObject *obj) {
	this->globalVarTable.push_back(obj);
}

void RuntimeContext::updateGlobalVar(int varIndex, DSObject *obj) {
	this->globalVarTable[varIndex] = obj;
}

DSObject *RuntimeContext::getGlobalVar(int index) {
	return this->globalVarTable[index];
}

int RuntimeContext::getGlobalVarSize() {
	return this->globalVarTable.size();
}

void RuntimeContext::setThrownObject(DSObject *obj) {
	this->thrownObject = obj;
}

void RuntimeContext::clearThrownObject() {
	this->thrownObject = 0;
}

DSObject *RuntimeContext::getThrownObject() {
	return this->thrownObject;
}

