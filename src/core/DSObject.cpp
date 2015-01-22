/*
 * Copyright (C) 2015 Nagisa Sekiguchi
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
    if(this->fieldSize > 0) {
        this->fieldTable = new DSObject*[this->fieldSize];
    }
}

BaseObject::~BaseObject() {
    if(this->fieldTable != 0) {
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
    this->funcNode = 0;
}

FunctionNode *UserFuncObject::getFuncNode() {
    return this->funcNode;
}


// ###############################
// ##     BuiltinFuncObject     ##
// ###############################

BuiltinFuncObject::BuiltinFuncObject(FunctionType *funcType, int paramSize, void *func_ptr) :
        FuncObject(funcType), paramSize(paramSize), func_ptr(func_ptr) {
}

BuiltinFuncObject::~BuiltinFuncObject() {
}

int BuiltinFuncObject::getParamSize() {
    return this->paramSize;
}

void *BuiltinFuncObject::getFuncPointer() {
    return this->func_ptr;
}
