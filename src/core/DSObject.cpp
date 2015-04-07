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
#include <iostream>

namespace ydsh {
namespace core {

// ######################
// ##     DSObject     ##
// ######################

DSObject::DSObject(DSType *type) :
        type(type), fieldTable(0) {
    if(type != 0) {
        this->fieldTable = new std::shared_ptr<DSObject>[this->type->getFieldSize()];
        this->type->initFieldTable(this->fieldTable);
    }
}

DSObject::~DSObject() {
    delete[] this->fieldTable;
    this->fieldTable = 0;
}

DSType *DSObject::getType() {
    return this->type;
}

void DSObject::setType(DSType *type) {  // do nothing.
}

std::string DSObject::toString() {
    std::string str("DSObject(");
    str += std::to_string((long) this);
    str += ")";
    return str;
}

bool DSObject::equals(const std::shared_ptr<DSObject> &obj) {
    return (long) this == (long) obj.get();
}

std::shared_ptr<String_Object> DSObject::str(RuntimeContext &ctx) {
    return std::make_shared<String_Object>(ctx.pool.getStringType(), this->toString());
}

std::shared_ptr<String_Object> DSObject::interp(RuntimeContext &ctx) {
    return this->str(ctx);
}

std::shared_ptr<DSObject> DSObject::commandArg(RuntimeContext &ctx) {
    return this->str(ctx);
}

size_t DSObject::hash() {
    return (long) this;
}

// ########################
// ##     Int_Object     ##
// ########################

Int_Object::Int_Object(DSType *type, int value) :
        DSObject(type), value(value) {
}

int Int_Object::getValue() {
    return this->value;
}

std::string Int_Object::toString() {
    return std::to_string(this->value);
}

bool Int_Object::equals(const std::shared_ptr<DSObject> &obj) {
    return this->value == TYPE_AS(Int_Object, obj)->value;
}

// ##########################
// ##     Float_Object     ##
// ##########################

Float_Object::Float_Object(DSType *type, double value) :
        DSObject(type), value(value) {
}

double Float_Object::getValue() {
    return this->value;
}

std::string Float_Object::toString() {
    return std::to_string(this->value);
}

bool Float_Object::equals(const std::shared_ptr<DSObject> &obj) {
    return this->value == TYPE_AS(Float_Object, obj)->value;
}


// ############################
// ##     Boolean_Object     ##
// ############################

Boolean_Object::Boolean_Object(DSType *type, bool value) :
        DSObject(type), value(value) {
}

bool Boolean_Object::getValue() {
    return this->value;
}

std::string Boolean_Object::toString() {
    return this->value ? "true" : "false";
}

bool Boolean_Object::equals(const std::shared_ptr<DSObject> &obj) {
    return this->value == TYPE_AS(Boolean_Object, obj)->value;
}


// ###########################
// ##     String_Object     ##
// ###########################

String_Object::String_Object(DSType *type, std::string &&value) :
        DSObject(type), value(std::move(value)) {
}

String_Object::String_Object(DSType *type, const std::string &value) :
        DSObject(type), value(value) {
}

String_Object::String_Object(DSType *type) :
        DSObject(type), value() {
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

void String_Object::append(const std::shared_ptr<String_Object> &obj) {
    this->value += obj->value;
}

bool String_Object::equals(const std::shared_ptr<DSObject> &obj) {
    return this->value == TYPE_AS(String_Object, obj)->value;
}

// ##########################
// ##     Array_Object     ##
// ##########################

Array_Object::Array_Object(DSType *type) :
        DSObject(type), values() {
}

const std::vector<std::shared_ptr<DSObject>> &Array_Object::getValues() {
    return this->values;
}

std::string Array_Object::toString() {
    std::string str("[");
    unsigned int size = this->values.size();
    for(unsigned int i = 0; i < size; i++) {
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

std::shared_ptr<String_Object> Array_Object::interp(RuntimeContext &ctx) {
    if(this->values.size() == 1) {
        return this->values[0]->interp(ctx);
    }

    std::shared_ptr<String_Object> value(new String_Object(ctx.pool.getStringType()));
    unsigned int size = this->values.size();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            value->value += " ";
        }
        value->append(this->values[i]->str(ctx));
    }
    return value;
}

std::shared_ptr<DSObject> Array_Object::commandArg(RuntimeContext &ctx) {
    if(*this->type == *ctx.pool.getStringArrayType()) {
        return std::shared_ptr<DSObject>(this);
    }

    std::shared_ptr<Array_Object> result(new Array_Object(ctx.pool.getStringArrayType()));
    for(const std::shared_ptr<DSObject> &e : this->values) {
        std::shared_ptr<DSObject> temp(e->commandArg(ctx));

        DSType *tempType = temp->type;
        if(*tempType == *ctx.pool.getStringType()) {
            result->values.push_back(std::move(temp));
        } else if(*tempType == *ctx.pool.getStringArrayType()) {
            Array_Object *tempArray = TYPE_AS(Array_Object, temp);
            for(const std::shared_ptr<DSObject> &tempValue : tempArray->values) {
                result->values.push_back(tempValue);
            }
        } else {
            fatal("illegal command argument type: %s\n", ctx.pool.getTypeName(*tempType).c_str());
        }
    }
    return result;
}

// ##########################
// ##     Tuple_Object     ##
// ##########################

Tuple_Object::Tuple_Object(DSType *type) :
        DSObject(type) {
}

std::string Tuple_Object::toString() {
    std::string str("(");
    unsigned int size = this->getElementSize();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            str += ", ";
        }
        str += this->fieldTable[this->getActualIndex(i)]->toString();
    }
    str += ")";
    return str;
}

unsigned int Tuple_Object::getActualIndex(unsigned int elementIndex) {
    return this->type->getSuperType()->getFieldSize() + elementIndex;
}

unsigned int Tuple_Object::getElementSize() {
    return this->type->getFieldSize() - this->type->getSuperType()->getFieldSize();
}

void Tuple_Object::set(unsigned int elementIndex, const std::shared_ptr<DSObject> &obj) {
    this->fieldTable[this->getActualIndex(elementIndex)] = obj;
}

const std::shared_ptr<DSObject> &Tuple_Object::get(unsigned int elementIndex) {
    return this->fieldTable[this->getActualIndex(elementIndex)];
}

std::shared_ptr<String_Object> Tuple_Object::interp(RuntimeContext &ctx) {
    std::shared_ptr<String_Object> value(new String_Object(ctx.pool.getStringType()));

    unsigned int size = this->getElementSize();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            value->value += " ";
        }
        value->append(this->fieldTable[this->getActualIndex(i)]->str(ctx));
    }
    return value;
}

std::shared_ptr<DSObject> Tuple_Object::commandArg(RuntimeContext &ctx) {
    std::shared_ptr<Array_Object> result(new Array_Object(ctx.pool.getStringArrayType()));
    unsigned int size = this->getElementSize();
    for(unsigned int i = 0; i < size; i++) {
        std::shared_ptr<DSObject> temp(this->fieldTable[this->getActualIndex(i)]->commandArg(ctx));

        DSType *tempType = temp->type;
        if(*tempType == *ctx.pool.getStringType()) {
            result->values.push_back(std::move(temp));
        } else if(*tempType == *ctx.pool.getStringArrayType()) {
            Array_Object *tempArray = TYPE_AS(Array_Object, temp);
            for(const std::shared_ptr<DSObject> &tempValue : tempArray->values) {
                result->values.push_back(tempValue);
            }
        } else {
            fatal("illegal command argument type: %s\n", ctx.pool.getTypeName(*tempType).c_str());
        }
    }
    return result;
}

// ##########################
// ##     Error_Object     ##
// ##########################

Error_Object::Error_Object(DSType *type, const std::shared_ptr<DSObject> &message) :
        DSObject(type), message(message), stackTrace() {
}

Error_Object::Error_Object(DSType *type, std::shared_ptr<DSObject> &&message) :
        DSObject(type), message(message), stackTrace() {
}

Error_Object::~Error_Object() {
}

std::string Error_Object::toString() {
    std::string str("Error(");
    str += std::to_string((long) this);
    str += ", ";
    str += TYPE_AS(String_Object, this->message)->value;
    str += ")";
    return str;
}

void Error_Object::createStackTrace(RuntimeContext &ctx) {
    //TODO:
}

void Error_Object::printStackTrace(RuntimeContext &ctx) {
    // print header
    std::cerr << ctx.pool.getTypeName(*this->type) << ": "
    << TYPE_AS(String_Object, this->message)->value << std::endl;

    // print stack trace
    for(const std::string &s : this->stackTrace) {
        std::cerr << "    " << s << std::endl;
    }
}

Error_Object *Error_Object::newError(RuntimeContext &ctx, DSType *type,
                                     const std::shared_ptr<DSObject> &message) {
    Error_Object *obj = new Error_Object(type, message);
    obj->createStackTrace(ctx);
    return obj;
}

Error_Object *Error_Object::newError(RuntimeContext &ctx, DSType *type,
                              std::shared_ptr<DSObject> &&message) {
    auto *obj = new Error_Object(type, std::move(message));
    obj->createStackTrace(ctx);
    return obj;
}


// ########################
// ##     FuncObject     ##
// ########################

FuncObject::FuncObject() :
        DSObject(0) {
}

FuncObject::~FuncObject() {
}

void FuncObject::setType(DSType *type) {
    if(this->type == 0) {
        assert(dynamic_cast<FunctionType *>(type) != 0);
        this->type = type;
        this->fieldTable = new std::shared_ptr<DSObject>[this->type->getFieldSize()];
        this->type->initFieldTable(this->fieldTable);
    }
}

FunctionType *FuncObject::getFuncType() {
    return (FunctionType *) this->type;
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

BuiltinFuncObject::BuiltinFuncObject(native_func_t func_ptr) :
        FuncObject(), func_ptr(func_ptr) {
}

BuiltinFuncObject::~BuiltinFuncObject() {
}

native_func_t BuiltinFuncObject::getFuncPointer() {
    return this->func_ptr;
}

std::string BuiltinFuncObject::toString() {
    std::string str("function(");
    str += std::to_string((long) this->func_ptr);
    str += ")";
    return str;
}

bool BuiltinFuncObject::invoke(RuntimeContext &ctx) {
    return this->func_ptr(ctx);
}

std::shared_ptr<DSObject> BuiltinFuncObject::newFuncObject(native_func_t func_ptr) {
    return std::make_shared<BuiltinFuncObject>(func_ptr);
}

} // namespace core
} // namespace ydsh