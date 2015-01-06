/*
 * DSObject.cpp
 *
 *  Created on: 2014/12/31
 *      Author: skgchxngsxyz-osx
 */

#include "DSObject.h"
#include <stdio.h>

// ######################
// ##     DSObject     ##
// ######################

DSObject::DSObject(DSType *type):
		type(type), fieldSize(type->getFieldSize()) {
	if(this->fieldSize > 0) {
		this->fieldTable = new DSObject*[this->fieldSize];
	}
}

DSObject::~DSObject() {
	if(this->fieldTable != 0) {
		delete[] this->fieldTable;
	}
}

DSType *DSObject::getType() {
	return this->type;
}

int DSObject::getFieldSize() {
	return this->fieldSize;
}

DSObject **DSObject::getFieldTable() {
	return this->fieldTable;
}


// ##########################
// ##     Int64_Object     ##
// ##########################

Int64_Object::Int64_Object(DSType *type, long value):
		DSObject(type), value(value) {
}

long Int64_Object::getValue() {
	return this->value;
}


// ##########################
// ##     Float_Object     ##
// ##########################

Float_Object::Float_Object(DSType *type, double value):
		DSObject(type), value(value) {
}

double Float_Object::getValue() {
	return this->value;
}


// ############################
// ##     Boolean_Object     ##
// ############################

Boolean_Object::Boolean_Object(DSType *type, bool value):
		DSObject(type), value(value) {
}

bool Boolean_Object::getValue() {
	return this->value;
}


// ###########################
// ##     String_Object     ##
// ###########################

String_Object::String_Object(DSType *type, std::string value):
		DSObject(type), value(value) {
}

const std::string &String_Object::getValue() {
	return this->value;
}
