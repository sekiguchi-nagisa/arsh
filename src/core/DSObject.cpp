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

#include <core/DSObject.h>
#include <ast/Node.h>

#include <assert.h>
#include <util/debug.h>

// ######################
// ##     DSObject     ##
// ######################

DSObject::DSObject() {
}

DSObject::~DSObject() {
}

void DSObject::setType(DSType *type) {  // do nothing.
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


// ########################
// ##     Int_Object     ##
// ########################

Int_Object::Int_Object(DSType *type, int value) :
        BaseObject(type), value(value) {
}

int Int_Object::getValue() {
    return this->value;
}

std::string Int_Object::toString() {
    return std::to_string(this->value);
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

std::string Float_Object::toString() {
    return std::to_string(this->value);
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

std::string Boolean_Object::toString() {
    return this->value ? "true" : "false";
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

std::string String_Object::toString() {
    return this->value;
}

void String_Object::append(const String_Object &obj) {
    this->value += obj.value;
}

// ##########################
// ##     Array_Object     ##
// ##########################

Array_Object::Array_Object(DSType *type) :
        BaseObject(type), values() {
}

const std::vector<std::shared_ptr<DSObject>> &Array_Object::getValues() {
    return this->values;
}

std::string Array_Object::toString() {
    std::string str("[");
    unsigned int size = this->values.size();
    for(unsigned int i = 0; i  < size; i++) {
        if(i > 0) {
            str += ", ";
        }
        str += this->values[i]->toString();
    }
    str += "]";
    return str;
}

void Array_Object::append(std::shared_ptr<DSObject> obj) {
    this->values.push_back(obj);
}

// ##########################
// ##     Tuple_Object     ##
// ##########################

Tuple_Object::Tuple_Object(DSType *type, unsigned int size) :
        BaseObject(type), values(size) {
}

const std::vector<std::shared_ptr<DSObject>> &Tuple_Object::getValues() {
    return this->values;
}

std::string Tuple_Object::toString() {
    std::string str("(");
    unsigned int size = this->values.size();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            str += ", ";
        }
        str += this->values[i]->toString();
    }
    str += ")";
    return str;
}

void Tuple_Object::set(unsigned int index, std::shared_ptr<DSObject> obj) {
    this->values[index] = obj;
}


// ########################
// ##     FuncObject     ##
// ########################

FuncObject::FuncObject() :
        funcType(0) {
}

FuncObject::~FuncObject() {
}

DSType *FuncObject::getType() {
    return this->getFuncType();
}

void FuncObject::setType(DSType *type) {
    FunctionType *funcType = dynamic_cast<FunctionType*>(type);
    assert(funcType != 0);
    this->funcType = funcType;
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

UserFuncObject::UserFuncObject(FunctionNode *funcNode) :
        FuncObject(), funcNode(funcNode) {
}

UserFuncObject::~UserFuncObject() {
    delete this->funcNode;
    this->funcNode = 0;
}

FunctionNode *UserFuncObject::getFuncNode() {
    return this->funcNode;
}

std::string UserFuncObject::toString() {
    std::string str("function(");
    str += this->funcNode->getFuncName();
    str += ")";
    return str;
}

bool UserFuncObject::invoke(RuntimeContext &ctx) {  //TODO: default param
    // change stackTopIndex
    ctx.stackTopIndex = ctx.localVarOffset + this->funcNode->getMaxVarNum();

    EvalStatus s = this->funcNode->getBlockNode()->eval(ctx);
    switch(s) {
    case EVAL_RETURN:
        return true;
    case EVAL_THROW:
        return false;
    default:
        fatal("illegal eval status: %d\n", s);
        return false;
    }
}


// ###############################
// ##     BuiltinFuncObject     ##
// ###############################

BuiltinFuncObject::BuiltinFuncObject(int paramSize, void *func_ptr) :
        FuncObject(), paramSize(paramSize), func_ptr(func_ptr) {
}

BuiltinFuncObject::~BuiltinFuncObject() {
}

int BuiltinFuncObject::getParamSize() {
    return this->paramSize;
}

void *BuiltinFuncObject::getFuncPointer() {
    return this->func_ptr;
}

std::string BuiltinFuncObject::toString() {
    std::string str("function(");
    str += std::to_string((long)this->func_ptr);
    str += ")";
    return str;
}

bool BuiltinFuncObject::invoke(RuntimeContext &ctx) {
    fatal("unimplemented function invocation\n");
    return false;
}
