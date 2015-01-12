/*
 * DSObject.cpp
 *
 *  Created on: 2014/12/31
 *      Author: skgchxngsxyz-osx
 */

#include "DSObject.h"
#include "../ast/Node.h"
#include <stdio.h>

// ######################
// ##     DSObject     ##
// ######################

DSObject::DSObject() {
}

DSObject::~DSObject() {
}

// ########################
// ##     BaseObject     ##
// ########################

BaseObject::BaseObject(DSType *type) :	//TODO: add field to table
        type(type), fieldSize(type->getFieldSize()), fieldTable(0) {
    if (this->fieldSize > 0) {
        this->fieldTable = new DSObject*[this->fieldSize];
    }
}

BaseObject::~BaseObject() {
    if (this->fieldTable != 0) {
        delete[] this->fieldTable;
    }
}

DSType *BaseObject::getType() {
    return this->type;
}

int BaseObject::getFieldSize() {
    return this->fieldSize;
}

DSObject *BaseObject::lookupField(int fieldIndex) {
    return this->fieldTable[fieldIndex];
}

// ##########################
// ##     Int64_Object     ##
// ##########################

Int64_Object::Int64_Object(DSType *type, long value) :
        BaseObject(type), value(value) {
}

long Int64_Object::getValue() {
    return this->value;
}

// ##########################
// ##     Float_Object     ##
// ##########################

Float_Object::Float_Object(DSType *type, double value) :
        BaseObject(type), value(value) {
}

double Float_Object::getValue() {
    return this->value;
}

// ############################
// ##     Boolean_Object     ##
// ############################

Boolean_Object::Boolean_Object(DSType *type, bool value) :
        BaseObject(type), value(value) {
}

bool Boolean_Object::getValue() {
    return this->value;
}

// ###########################
// ##     String_Object     ##
// ###########################

String_Object::String_Object(DSType *type, std::string &&value) :
        BaseObject(type), value(std::move(value)) {
}

const std::string &String_Object::getValue() {
    return this->value;
}

// ########################
// ##     FuncObject     ##
// ########################

FuncObject::FuncObject(FunctionType *funcType) :
        funcType(funcType) {
}

DSType *FuncObject::getType() {
    return this->getFuncType();
}

int FuncObject::getFieldSize() {
    return 0;
}

DSObject *FuncObject::lookupField(int fieldIndex) {
    return 0;
}

FunctionType *FuncObject::getFuncType() {
    return this->funcType;
}

// ############################
// ##     UserFuncObject     ##
// ############################

UserFuncObject::UserFuncObject(FunctionType *funcType, FunctionNode *funcNode) :
        FuncObject(funcType), funcNode(funcNode) {
}

UserFuncObject::~UserFuncObject() {
    delete this->funcNode;
}

FunctionNode *UserFuncObject::getFuncNode() {
    return this->funcNode;
}
