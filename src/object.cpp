/*
 * Copyright (C) 2015-2018 Nagisa Sekiguchi
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

#include <memory>
#include <algorithm>

#include "vm.h"
#include "misc/num.h"

namespace ydsh {

// ######################
// ##     DSObject     ##
// ######################

DSValue *DSObject::getFieldTable() {
    return nullptr;
}

std::string DSObject::toString() const {
    std::string str("DSObject(");
    str += std::to_string(reinterpret_cast<long>(this));
    str += ")";
    return str;
}

bool DSObject::opStr(DSState &state) const {
    state.toStrBuf += this->toString();
    return true;
}

bool DSObject::opInterp(DSState &state) const {
    return this->opStr(state);
}

bool DSObject::equals(const DSValue &obj) const {
    return reinterpret_cast<long>(this) == reinterpret_cast<long>(obj.get());
}

bool DSObject::compare(const ydsh::DSValue &obj) const {
    return reinterpret_cast<long>(this) < reinterpret_cast<long>(obj.get());
}

size_t DSObject::hash() const {
    return std::hash<long>()(reinterpret_cast<long>(this));
}

// ########################
// ##     Int_Object     ##
// ########################

std::string Int_Object::toString() const {
    if(this->type->is(TYPE::Uint32)) {
        return std::to_string(static_cast<unsigned int>(this->value));
    }
    return std::to_string(this->value);
}

bool Int_Object::equals(const DSValue &obj) const {
    return this->value == typeAs<Int_Object>(obj)->value;
}

bool Int_Object::compare(const DSValue &obj) const {
    if(this->type->is(TYPE::Uint32)) {
        return static_cast<unsigned int>(this->value) < static_cast<unsigned int>(typeAs<Int_Object>(obj)->value);
    }
    return this->value < typeAs<Int_Object>(obj)->value;
}

size_t Int_Object::hash() const {
    return std::hash<int>()(this->value);
}

// ###########################
// ##     UnixFD_Object     ##
// ###########################

UnixFD_Object::~UnixFD_Object() {
    int fd = this->getValue();
    if(fd > STDERR_FILENO) {
        close(this->getValue());    // do not close standard io file descriptor
    }
}

std::string UnixFD_Object::toString() const {
    std::string str = "/dev/fd/";
    str += std::to_string(this->value);
    return str;
}


// #########################
// ##     Long_Object     ##
// #########################

std::string Long_Object::toString() const {
    if(this->type->is(TYPE::Uint64)) {
        return std::to_string(static_cast<unsigned long>(this->value));
    }
    return std::to_string(this->value);
}

bool Long_Object::equals(const DSValue &obj) const {
    return this->value == typeAs<Long_Object>(obj)->value;
}

bool Long_Object::compare(const DSValue &obj) const {
    if(this->type->is(TYPE::Uint64)) {
        return static_cast<unsigned long>(this->value) < static_cast<unsigned long>(typeAs<Long_Object>(obj)->value);
    }
    return this->value < typeAs<Long_Object>(obj)->value;
}

size_t Long_Object::hash() const {
    return std::hash<long>()(this->value);
}

// ##########################
// ##     Float_Object     ##
// ##########################

std::string Float_Object::toString() const {
    return std::to_string(this->value);
}

bool Float_Object::equals(const DSValue &obj) const {
    return this->value == typeAs<Float_Object>(obj)->value;
}

bool Float_Object::compare(const DSValue &obj) const {
    return this->value < typeAs<Float_Object>(obj)->value;
}

size_t Float_Object::hash() const {
    return std::hash<double>()(this->value);
}


// ############################
// ##     Boolean_Object     ##
// ############################

std::string Boolean_Object::toString() const {
    return this->value ? "true" : "false";
}

bool Boolean_Object::equals(const DSValue &obj) const {
    return this->value == typeAs<Boolean_Object>(obj)->value;
}

bool Boolean_Object::compare(const DSValue &obj) const {
    unsigned int left = this->value ? 1 : 0;
    unsigned int right = typeAs<Boolean_Object>(obj)->value ? 1 : 0;
    return left < right;
}

size_t Boolean_Object::hash() const {
    return std::hash<bool>()(this->value);
}

// ###########################
// ##     String_Object     ##
// ###########################

std::string String_Object::toString() const {
    return this->value;
}

bool String_Object::equals(const DSValue &obj) const {
    return this->value == typeAs<String_Object>(obj)->value;
}

bool String_Object::compare(const DSValue &obj) const {
    auto *str2 = typeAs<String_Object>(obj);
    unsigned int size = std::min(this->size(), str2->size());
    return memcmp(this->getValue(), str2->getValue(), size + 1) < 0;
}

size_t String_Object::hash() const {
    return std::hash<std::string>()(this->value);
}

// ##########################
// ##     Array_Object     ##
// ##########################

static bool checkInvalid(DSState &st, DSValue &v) {
    if(v.isInvalid()) {
        raiseError(st, TYPE::UnwrappingError, "invalid value");
        return false;
    }
    return true;
}

#define TRY(E) \
({ auto v = E; if(state.hasError()) { return false; } std::forward<decltype(v)>(v); })


std::string Array_Object::toString() const {
    std::string str = "[";
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

static DSValue callOP(DSState &state, const DSValue &value, const char *op) {
    DSValue ret = value;
    if(!checkInvalid(state, ret)) {
        return DSValue();
    }
    auto *handle = ret->getType()->lookupMethodHandle(state.symbolTable, op);
    assert(handle != nullptr);
    ret = state.callMethod(handle, std::move(ret), makeArgs());
    return ret;
}

bool Array_Object::opStr(DSState &state) const {
    state.toStrBuf += "[";
    unsigned int size = this->values.size();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            state.toStrBuf += ", ";
        }

        auto ret = TRY(callOP(state, this->values[i], OP_STR));
        if(!ret.isInvalid()) {
            state.toStrBuf += typeAs<String_Object>(ret)->getValue();
        }
    }
    state.toStrBuf += "]";
    return true;
}

bool Array_Object::opInterp(DSState &state) const {
    unsigned int size = this->values.size();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            state.toStrBuf += " ";
        }

        auto ret = TRY(callOP(state, this->values[i], OP_INTERP));
        if(!ret.isInvalid()) {
            state.toStrBuf += typeAs<String_Object>(ret)->getValue();
        }
    }
    return true;
}

static MethodHandle *lookupCmdArg(DSType *recvType, SymbolTable &symbolTable) {
    auto *handle = recvType->lookupMethodHandle(symbolTable, OP_CMD_ARG);
    if(handle == nullptr) {
        handle = recvType->lookupMethodHandle(symbolTable, OP_STR);
    }
    return handle;
}

static bool appendAsCmdArg(std::vector<DSValue> &result, DSState &state, const DSValue &value) {
    DSValue recv = value;
    if(!checkInvalid(state, recv)) {
        return false;
    }
    auto *handle = lookupCmdArg(recv->getType(), state.symbolTable);
    auto ret = TRY(state.callMethod(handle, std::move(recv), makeArgs()));
    auto *retType = ret->getType();
    if(retType->is(TYPE::String)) {
        result.push_back(std::move(ret));
    } else {
        assert(retType->is(TYPE::StringArray));
        auto *tempArray = typeAs<Array_Object>(ret);
        for(auto &tempValue : tempArray->getValues()) {
            result.push_back(tempValue);
        }
    }
    return true;
}

DSValue Array_Object::opCmdArg(DSState &state) const {
    auto result = DSValue::create<Array_Object>(state.symbolTable.get(TYPE::StringArray));
    for(auto &e : this->values) {
        if(!appendAsCmdArg(typeAs<Array_Object>(result)->values, state, e)) {
            return DSValue();
        }
    }
    return result;
}

const DSValue &Array_Object::nextElement() {
    unsigned int index = this->curIndex++;
    assert(index < this->values.size());
    return this->values[index];
}

// ########################
// ##     Map_Object     ##
// ########################

DSValue Map_Object::nextElement(DSState &ctx) {
    std::vector<DSType *> types(2);
    types[0] = this->iter->first->getType();
    types[1] = this->iter->second->getType();

    auto entry = DSValue::create<Tuple_Object>(*ctx.symbolTable.createTupleType(std::move(types)).take());
    typeAs<Tuple_Object>(entry)->set(0, this->iter->first);
    typeAs<Tuple_Object>(entry)->set(1, this->iter->second);
    ++this->iter;

    return entry;
}

std::string Map_Object::toString() const {
    std::string str = "[";
    unsigned int count = 0;
    for(auto &e : this->valueMap) {
        if(count++ > 0) {
            str += ", ";
        }
        str += e.first->toString();
        str += " : ";
        str += e.second->toString();
    }
    str += "]";
    return str;
}

bool Map_Object::opStr(DSState &state) const {
    state.toStrBuf += "[";
    unsigned int count = 0;
    for(auto &e : this->valueMap) {
        if(count++ > 0) {
            state.toStrBuf += ", ";
        }

        // key
        auto ret = TRY(callOP(state, e.first, OP_STR));
        if(!ret.isInvalid()) {
            state.toStrBuf += typeAs<String_Object>(ret)->getValue();
        }

        state.toStrBuf += " : ";

        // value
        ret = TRY(callOP(state, e.second, OP_STR));
        if(!ret.isInvalid()) {
            state.toStrBuf += typeAs<String_Object>(ret)->getValue();
        }
    }
    state.toStrBuf += "]";
    return true;
}

// ########################
// ##     Job_Object     ##
// ########################

std::string Job_Object::toString() const {
    std::string str = "%";
    str += std::to_string(this->entry->jobID());
    return str;
}

// ########################
// ##     BaseObject     ##
// ########################

BaseObject::~BaseObject() {
    delete[] this->fieldTable;
}

DSValue *BaseObject::getFieldTable() {
    return this->fieldTable;
}

// ##########################
// ##     Tuple_Object     ##
// ##########################

std::string Tuple_Object::toString() const {
    std::string str = "(";
    unsigned int size = this->getElementSize();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            str += ", ";
        }
        str += this->fieldTable[i]->toString();
    }
    str += ")";
    return str;
}

bool Tuple_Object::opStr(DSState &state) const {
    state.toStrBuf += "(";
    unsigned int size = this->getElementSize();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            state.toStrBuf += ", ";
        }

        auto ret = TRY(callOP(state, this->fieldTable[i], OP_STR));
        if(!ret.isInvalid()) {
            state.toStrBuf += typeAs<String_Object>(ret)->getValue();
        }
    }
    state.toStrBuf += ")";
    return true;
}

bool Tuple_Object::opInterp(DSState &state) const {
    unsigned int size = this->getElementSize();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            state.toStrBuf += " ";
        }

        auto ret = TRY(callOP(state, this->fieldTable[i], OP_INTERP));
        if(!ret.isInvalid()) {
            state.toStrBuf += typeAs<String_Object>(ret)->getValue();
        }
    }
    return true;
}

DSValue Tuple_Object::opCmdArg(DSState &state) const {
    auto result = DSValue::create<Array_Object>(state.symbolTable.get(TYPE::StringArray));
    unsigned int size = this->getElementSize();
    for(unsigned int i = 0; i < size; i++) {
        if(!appendAsCmdArg(typeAs<Array_Object>(result)->refValues(), state, this->fieldTable[i])) {
            return DSValue();
        }
    }
    return result;
}

// ##########################
// ##     Error_Object     ##
// ##########################

bool Error_Object::opStr(DSState &state) const {
    state.toStrBuf += this->createHeader(state);
    return true;
}

void Error_Object::printStackTrace(DSState &ctx) {
    // print header
    fprintf(stderr, "%s\n", this->createHeader(ctx).c_str());

    // print stack trace
    for(auto &s : this->stackTrace) {
        fprintf(stderr, "    from %s:%d '%s()'\n",
                s.getSourceName().c_str(), s.getLineNum(), s.getCallerName().c_str());
    }
}

DSValue Error_Object::newError(const DSState &ctx, DSType &type, DSValue &&message) {
    DSValue obj(new Error_Object(type, std::move(message)));
    fillInStackTrace(ctx, typeAs<Error_Object>(obj)->stackTrace);
    typeAs<Error_Object>(obj)->name = DSValue::create<String_Object>(
            ctx.symbolTable.get(TYPE::String), ctx.symbolTable.getTypeName(type));
    return obj;
}

std::string Error_Object::createHeader(const DSState &state) const {
    std::string str = state.symbolTable.getTypeName(*this->type);
    str += ": ";
    str += typeAs<String_Object>(this->message)->getValue();
    return str;
}

unsigned int getLineNum(const LineNumEntry *entries, unsigned int index) {  //FIXME binary search
    unsigned int i = 0;
    for(; entries[i].address > 0; i++) {
        if(index < entries[i].address) {
            break;
        }
    }
    return entries[i > 0 ? i - 1 : 0].lineNum;
}

// ########################
// ##     FuncObject     ##
// ########################

std::string FuncObject::toString() const {
    std::string str = this->code.is(CodeKind::FUNCTION) ? "function(" : "module(";
    str += this->code.getName();
    str += ")";
    return str;
}

} // namespace ydsh
