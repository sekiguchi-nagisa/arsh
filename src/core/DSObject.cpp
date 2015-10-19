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

#include <cassert>

#include "DSObject.h"
#include "RuntimeContext.h"
#include "../misc/unused.h"

#ifdef USE_DBUS
#include "../dbus/DBusBind.h"
#endif

namespace ydsh {
namespace core {

using namespace ydsh::ast;

// ######################
// ##     DSObject     ##
// ######################

void DSObject::setType(DSType *type) {  // do nothing.
    UNUSED(type);
}

DSValue *DSObject::getFieldTable() {
    return nullptr;
}

std::string DSObject::toString(RuntimeContext &ctx) {
    UNUSED(ctx);

    std::string str("DSObject(");
    str += std::to_string((long) this);
    str += ")";
    return str;
}

bool DSObject::equals(const DSValue &obj) {
    return (long) this == (long) obj.get();
}

DSValue DSObject::str(RuntimeContext &ctx) {
    return DSValue::create<String_Object>(ctx.getPool().getStringType(), this->toString(ctx));
}

DSValue DSObject::interp(RuntimeContext &ctx) {
    return this->str(ctx);
}

DSValue DSObject::commandArg(RuntimeContext &ctx) {
    return this->str(ctx);
}

size_t DSObject::hash() {
    return std::hash<long>()((long) this);
}

bool DSObject::introspect(RuntimeContext &ctx, DSType *targetType) {
    UNUSED(ctx);
    return targetType->isSameOrBaseTypeOf(this->type);
}

// ########################
// ##     Int_Object     ##
// ########################

std::string Int_Object::toString(RuntimeContext &ctx) {
    if(*this->type == *ctx.getPool().getUint32Type()) {
        return std::to_string((unsigned int) this->value);
    }
    if(*this->type == *ctx.getPool().getInt16Type()) {
        return std::to_string((short) this->value);
    }
    if(*this->type == *ctx.getPool().getUint16Type()) {
        return std::to_string((unsigned short) this->value);
    }
    if(*this->type == *ctx.getPool().getByteType()) {
        return std::to_string((unsigned char) this->value);
    }
    return std::to_string(this->value);
}

bool Int_Object::equals(const DSValue &obj) {
    return this->value == typeAs<Int_Object>(obj)->value;
}

size_t Int_Object::hash() {
    return std::hash<int>()(this->value);
}

// #########################
// ##     Long_Object     ##
// #########################

std::string Long_Object::toString(RuntimeContext &ctx) {
    if(*this->type == *ctx.getPool().getUint64Type()) {
        return std::to_string((unsigned long) this->value);
    }
    return std::to_string(this->value);
}

bool Long_Object::equals(const DSValue &obj) {
    return this->value == typeAs<Long_Object>(obj)->value;
}

size_t Long_Object::hash() {
    return std::hash<long>()(this->value);
}

// ##########################
// ##     Float_Object     ##
// ##########################

std::string Float_Object::toString(RuntimeContext &ctx) {
    UNUSED(ctx);
    return std::to_string(this->value);
}

bool Float_Object::equals(const DSValue &obj) {
    return this->value == typeAs<Float_Object>(obj)->value;
}

size_t Float_Object::hash() {
    return std::hash<double>()(this->value);
}


// ############################
// ##     Boolean_Object     ##
// ############################

std::string Boolean_Object::toString(RuntimeContext &ctx) {
    UNUSED(ctx);
    return this->value ? "true" : "false";
}

bool Boolean_Object::equals(const DSValue &obj) {
    return this->value == typeAs<Boolean_Object>(obj)->value;
}

size_t Boolean_Object::hash() {
    return std::hash<bool>()(this->value);
}

// ###########################
// ##     String_Object     ##
// ###########################

std::string String_Object::toString(RuntimeContext &ctx) {
    UNUSED(ctx);
    return this->value;
}

bool String_Object::equals(const DSValue &obj) {
    return this->value == typeAs<String_Object>(obj)->value;
}

size_t String_Object::hash() {
    return std::hash<std::string>()(this->value);
}

// ##########################
// ##     Array_Object     ##
// ##########################

std::string Array_Object::toString(RuntimeContext &ctx) {
    std::string str("[");
    unsigned int size = this->values.size();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            str += ", ";
        }
        str += this->values[i]->toString(ctx);
    }
    str += "]";
    return str;
}

void Array_Object::append(DSValue &&obj) {
    this->values.push_back(std::move(obj));
}

void Array_Object::append(const DSValue &obj) {
    this->values.push_back(obj);
}

void Array_Object::set(unsigned int index, const DSValue &obj) {
    this->values[index] = obj;
}

const DSValue &Array_Object::nextElement() {
    unsigned int index = this->curIndex++;
    assert(index < this->values.size());
    return this->values[index];
}

DSValue Array_Object::interp(RuntimeContext &ctx) {
    if(this->values.size() == 1) {
        return this->values[0]->interp(ctx);
    }

    std::string str;
    unsigned int size = this->values.size();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            str += " ";
        }
        str += typeAs<String_Object>(this->values[i]->interp(ctx))->getValue();
    }
    return DSValue::create<String_Object>(ctx.getPool().getStringType(), std::move(str));
}

DSValue Array_Object::commandArg(RuntimeContext &ctx) {
    DSValue result(new Array_Object(ctx.getPool().getStringArrayType()));
    for(auto &e : this->values) {
        DSValue temp(e->commandArg(ctx));

        DSType *tempType = temp->getType();
        if(*tempType == *ctx.getPool().getStringType()) {
            typeAs<Array_Object>(result)->values.push_back(std::move(temp));
        } else if(*tempType == *ctx.getPool().getStringArrayType()) {
            Array_Object *tempArray = typeAs<Array_Object>(temp);
            for(auto &tempValue : tempArray->values) {
                typeAs<Array_Object>(result)->values.push_back(tempValue);
            }
        } else {
            fatal("illegal command argument type: %s\n", ctx.getPool().getTypeName(*tempType).c_str());
        }
    }
    return result;
}

bool KeyCompare::operator()(const DSValue &x, const DSValue &y) const {
    return x->equals(y);
}

std::size_t GenHash::operator()(const DSValue &key) const {
    return key->hash();
}

// ########################
// ##     Map_Object     ##
// ########################

void Map_Object::set(const DSValue &key, const DSValue &value) {
    this->valueMap[key] = value;
}

void Map_Object::add(std::pair<DSValue, DSValue> &&entry) {
    this->valueMap.insert(std::move(entry));
}

void Map_Object::initIterator() {
    this->iter = this->valueMap.cbegin();
}

DSValue Map_Object::nextElement(RuntimeContext &ctx) {
    std::vector<DSType *> types(2);
    types[0] = this->iter->first->getType();
    types[1] = this->iter->second->getType();

    DSValue entry(
            new Tuple_Object(ctx.getPool().createAndGetTupleTypeIfUndefined(std::move(types))));
    typeAs<Tuple_Object>(entry)->set(0, this->iter->first);
    typeAs<Tuple_Object>(entry)->set(1, this->iter->second);
    this->iter++;

    return std::move(entry);
}

bool Map_Object::hasNext() {
    return this->iter != this->valueMap.cend();
}

std::string Map_Object::toString(RuntimeContext &ctx) {
    std::string str("[");
    unsigned int count = 0;
    for(auto &iter : this->valueMap) {
        if(count++ > 0) {
            str += ", ";
        }
        str += iter.first->toString(ctx);
        str += " : ";
        str += iter.second->toString(ctx);
    }
    str += "]";
    return str;
}

// ########################
// ##     BaseObject     ##
// ########################

BaseObject::~BaseObject() {
    delete[] this->fieldTable;
    this->fieldTable = nullptr;
}

DSValue *BaseObject::getFieldTable() {
    return this->fieldTable;
}

// ##########################
// ##     Tuple_Object     ##
// ##########################

std::string Tuple_Object::toString(RuntimeContext &ctx) {
    std::string str("(");
    unsigned int size = this->getElementSize();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            str += ", ";
        }
        str += this->fieldTable[i]->toString(ctx);
    }
    str += ")";
    return str;
}

void Tuple_Object::set(unsigned int elementIndex, const DSValue &obj) {
    this->fieldTable[elementIndex] = obj;
}

const DSValue &Tuple_Object::get(unsigned int elementIndex) {
    return this->fieldTable[elementIndex];
}

DSValue Tuple_Object::interp(RuntimeContext &ctx) {
    std::string str;
    unsigned int size = this->getElementSize();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            str += " ";
        }
        str += typeAs<String_Object>(this->fieldTable[i]->str(ctx))->getValue();
    }
    return DSValue::create<String_Object>(ctx.getPool().getStringType(), std::move(str));
}

DSValue Tuple_Object::commandArg(RuntimeContext &ctx) {
    DSValue result(new Array_Object(ctx.getPool().getStringArrayType()));
    unsigned int size = this->getElementSize();
    for(unsigned int i = 0; i < size; i++) {
        DSValue temp(this->fieldTable[i]->commandArg(ctx));

        DSType *tempType = temp->getType();
        if(*tempType == *ctx.getPool().getStringType()) {
            typeAs<Array_Object>(result)->append(std::move(temp));
        } else if(*tempType == *ctx.getPool().getStringArrayType()) {
            Array_Object *tempArray = typeAs<Array_Object>(temp);
            for(auto &tempValue : tempArray->getValues()) {
                typeAs<Array_Object>(result)->append(tempValue);
            }
        } else {
            fatal("illegal command argument type: %s\n", ctx.getPool().getTypeName(*tempType).c_str());
        }
    }
    return result;
}

// ###############################
// ##     StackTraceElement     ##
// ###############################

std::ostream &operator<<(std::ostream &stream, const StackTraceElement &e) {
    return stream << "from " << e.getSourceName() << ":"
           << e.getLineNum() << " '" << e.getCallerName() << "()'";
}

unsigned int getOccuredLineNum(const std::vector<StackTraceElement> &elements) {
    return elements.front().getLineNum();
}


// ##########################
// ##     Error_Object     ##
// ##########################

std::string Error_Object::toString(RuntimeContext &ctx) {
    std::string str(ctx.getPool().getTypeName(*this->type));
    str += ": ";
    str += typeAs<String_Object>(this->message)->getValue();
    return str;
}

void Error_Object::printStackTrace(RuntimeContext &ctx) {
    // print header
    std::cerr << this->toString(ctx) << std::endl;

    // print stack trace
    for(auto &s : this->stackTrace) {
        std::cerr << "    " << s << std::endl;
    }
}

const DSValue &Error_Object::getName(RuntimeContext &ctx) {
    if(!this->name) {
        this->name = DSValue::create<String_Object>(
                ctx.getPool().getStringType(), ctx.getPool().getTypeName(*this->type));
    }
    return this->name;
}

DSValue Error_Object::newError(RuntimeContext &ctx, DSType *type, const DSValue &message) {
    auto obj = DSValue::create<Error_Object>(type, message);
    typeAs<Error_Object>(obj)->createStackTrace(ctx);
    return std::move(obj);
}

DSValue Error_Object::newError(RuntimeContext &ctx, DSType *type, DSValue &&message) {
    auto obj = DSValue::create<Error_Object>(type, std::move(message));
    typeAs<Error_Object>(obj)->createStackTrace(ctx);
    return std::move(obj);
}

void Error_Object::createStackTrace(RuntimeContext &ctx) {
    ctx.fillInStackTrace(this->stackTrace);
}


// ########################
// ##     FuncObject     ##
// ########################

FuncObject::~FuncObject() {
    delete this->funcNode;
    this->funcNode = nullptr;
}

void FuncObject::setType(DSType *type) {
    if(this->type == nullptr) {
        assert(dynamic_cast<FunctionType *>(type) != nullptr);
        this->type = type;
    }
}

std::string FuncObject::toString(RuntimeContext &ctx) {
    UNUSED(ctx);
    std::string str("function(");
    str += this->funcNode->getFuncName();
    str += ")";
    return str;
}

bool FuncObject::invoke(RuntimeContext &ctx) {  //TODO: default param
    // change stackTopIndex
    ctx.pushFuncContext(this->funcNode);
    ctx.reserveLocalVar(ctx.getLocalVarOffset() + this->funcNode->getMaxVarNum());

    EvalStatus s = this->funcNode->getBlockNode()->eval(ctx);
    ctx.popFuncContext();
    switch(s) {
    case EvalStatus::RETURN:
        return true;
    case EvalStatus::THROW:
        return false;
    default:
        fatal("illegal eval status: %d\n", s);
        return false;
    }
}

// #########################
// ##     DBus_Object     ##
// #########################

DBus_Object::DBus_Object(TypePool *typePool) :
        DSObject(typePool->getDBusType()) {
}

bool DBus_Object::getSystemBus(RuntimeContext &ctx) {
    ctx.throwError(ctx.getPool().getErrorType(), "unsupported feature");
    return false;
}

bool DBus_Object::getSessionBus(RuntimeContext &ctx) {
    ctx.throwError(ctx.getPool().getErrorType(), "unsupported feature");
    return false;
}

bool DBus_Object::waitSignal(RuntimeContext &ctx) {
    ctx.throwError(ctx.getPool().getErrorType(), "not support waitSignal method");
    return false;
}

bool DBus_Object::supportDBus() {
#ifdef USE_DBUS
    return true;
#else
    return false;
#endif
}

bool DBus_Object::getServiceFromProxy(RuntimeContext &ctx, const DSValue &proxy) {
    UNUSED(proxy);
    ctx.throwError(ctx.getPool().getErrorType(), "not support method");
    return false;
}

bool DBus_Object::getObjectPathFromProxy(RuntimeContext &ctx, const DSValue &proxy) {
    UNUSED(proxy);
    ctx.throwError(ctx.getPool().getErrorType(), "not support method");
    return false;
}

bool DBus_Object::getIfaceListFromProxy(RuntimeContext &ctx, const DSValue &proxy) {
    UNUSED(proxy);
    ctx.throwError(ctx.getPool().getErrorType(), "not support method");
    return false;
}

bool DBus_Object::introspectProxy(RuntimeContext &ctx, const DSValue &proxy) {
    UNUSED(proxy);
    ctx.throwError(ctx.getPool().getErrorType(), "not support method");
    return false;
}

DBus_Object *DBus_Object::newDBus_Object(TypePool *typePool) {
#ifdef USE_DBUS
    return newDBusObject(typePool);
#else
    return new DBus_Object(typePool);
#endif
}

// ########################
// ##     Bus_Object     ##
// ########################

Bus_Object::Bus_Object(DSType *type) : DSObject(type) {
}

bool Bus_Object::service(RuntimeContext &ctx, std::string &&serviceName) {
    UNUSED(serviceName);
    ctx.throwError(ctx.getPool().getErrorType(), "not support D-Bus service object");
    return false;
}

bool Bus_Object::listNames(RuntimeContext &ctx, bool activeName) {
    UNUSED(activeName);
    ctx.throwError(ctx.getPool().getErrorType(), "not support listNames method");
    return false;
}

// ############################
// ##     Service_Object     ##
// ############################

Service_Object::Service_Object(DSType *type) : DSObject(type) {
}

bool Service_Object::object(RuntimeContext &ctx, const DSValue &objectPath) {
    UNUSED(objectPath);
    ctx.throwError(ctx.getPool().getErrorType(), "not support D-Bus proxy object");
    return false;
}

} // namespace core
} // namespace ydsh
