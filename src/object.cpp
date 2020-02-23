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

#include "vm.h"
#include "misc/num_util.hpp"

namespace ydsh {

// #####################
// ##     DSValue     ##
// #####################

unsigned int DSValue::getTypeID() const {
    switch(this->kind()) {
    case DSValueKind::BOOL:
        return static_cast<unsigned int>(TYPE::Boolean);
    case DSValueKind::SIG:
        return static_cast<unsigned int>(TYPE::Signal);
    default:
        assert(this->kind() == DSValueKind::OBJECT);
        return this->get()->getType()->getTypeID();
    }
}

std::string DSValue::toString() const {
    switch(this->kind()) {
    case DSValueKind::NUMBER:
        return std::to_string(static_cast<uint64_t>(this->value()));
    case DSValueKind::BOOL:
        return this->asBool() ? "true" : "false";
    case DSValueKind::SIG:
        return std::to_string(this->asSig());
    default:
        assert(this->kind() == DSValueKind::OBJECT);
        break;
    }

    switch(this->get()->getKind()) {
    case ObjectKind::INT:
        return std::to_string(typeAs<Int_Object>(*this)->getValue());
    case ObjectKind::LONG:
        return std::to_string(typeAs<Long_Object>(*this)->getValue());
    case ObjectKind::FLOAT:
        return std::to_string(typeAs<Float_Object>(*this)->getValue());
    case ObjectKind::STRING:
        return createStrRef(*this).toString();
    case ObjectKind::FD: {
        std::string str = "/dev/fd/";
        str += std::to_string(typeAs<UnixFD_Object>(*this)->getValue());
        return str;
    }
    case ObjectKind::REGEX:
        return typeAs<Regex_Object>(*this)->getStr();
    case ObjectKind::ARRAY:
        return typeAs<Array_Object>(*this)->toString();
    case ObjectKind::MAP:
        return typeAs<Map_Object>(*this)->toString();
    case ObjectKind::TUPLE:
        return typeAs<Tuple_Object>(*this)->toString();
    case ObjectKind::FUNC_OBJ:
        return typeAs<FuncObject>(*this)->toString();
    case ObjectKind::JOB: {
        std::string str = "%";
        str += std::to_string(typeAs<JobImpl>(*this)->getJobID());
        return str;
    }
    default:
        break;
    }

    std::string str("DSObject(");
    str += std::to_string(reinterpret_cast<long>(this));
    str += ")";
    return str;
}

bool DSValue::opStr(DSState &state) const {
    if(this->isObject()) {
        switch(this->get()->getKind()) {
        case ObjectKind::ARRAY:
            return typeAs<Array_Object>(*this)->opStr(state);
        case ObjectKind::MAP:
            return typeAs<Map_Object>(*this)->opStr(state);
        case ObjectKind::TUPLE:
            return typeAs<Tuple_Object>(*this)->opStr(state);
        case ObjectKind::ERROR:
            return typeAs<Error_Object>(*this)->opStr(state);
        default:
            break;
        }
    }
    state.toStrBuf += this->toString();
    return true;
}

bool DSValue::opInterp(DSState &state) const {
    if(this->isObject()) {
        switch(this->get()->getKind()) {
        case ObjectKind::ARRAY:
            return typeAs<Array_Object>(*this)->opInterp(state);
        case ObjectKind::TUPLE:
            return typeAs<Tuple_Object>(*this)->opInterp(state);
        default:
            break;
        }
    }
    return this->opStr(state);
}

bool DSValue::equals(const DSValue &o) const {
    assert(this->kind() == o.kind());
    if(this->isObject()) {
        assert(this->get()->getKind() == o.get()->getKind());
        switch(this->get()->getKind()) {
        case ObjectKind::INT:
            return static_cast<Int_Object*>(this->get())->getValue() == typeAs<Int_Object>(o)->getValue();
        case ObjectKind::LONG:
            return static_cast<Long_Object*>(this->get())->getValue() == typeAs<Long_Object>(o)->getValue();
        case ObjectKind::FLOAT:
            return static_cast<Float_Object*>(this->get())->getValue() == typeAs<Float_Object>(o)->getValue();
        case ObjectKind::STRING: {
            auto left = createStrRef(*this);
            auto right = createStrRef(o);
            return left == right;
        }
        default:
            break;
        }
    }
    return this->val == o.val;
}

size_t DSValue::hash() const {
    if(this->isObject()) {
        switch(this->get()->getKind()) {
        case ObjectKind::INT:
            return std::hash<int>()(static_cast<Int_Object*>(this->get())->getValue());
        case ObjectKind::LONG:
            return std::hash<long>()(static_cast<Long_Object*>(this->get())->getValue());
        case ObjectKind::FLOAT:
            return std::hash<double>()(static_cast<Float_Object*>(this->get())->getValue());
        case ObjectKind::STRING:
            return std::hash<StringRef>()(createStrRef(*this));
        default:
            break;
        }
    }
    return std::hash<int64_t>()(this->val);
}

bool DSValue::compare(const DSValue &o) const {
    assert(this->kind() == o.kind());
    switch(this->kind()) {
    case DSValueKind::BOOL:
    case DSValueKind::SIG: {
        int left = this->value();
        int right = o.value();
        return left < right;
    }
    default:
        assert(this->kind() == DSValueKind::OBJECT);
        break;
    }

    assert(this->get()->getKind() == o.get()->getKind());
    switch(this->get()->getKind()) {
    case ObjectKind::INT:
        return static_cast<Int_Object*>(this->get())->getValue() < typeAs<Int_Object>(o)->getValue();
    case ObjectKind::LONG:
        return static_cast<Long_Object*>(this->get())->getValue() < typeAs<Long_Object>(o)->getValue();
    case ObjectKind::FLOAT:
        return static_cast<Float_Object*>(this->get())->getValue() < typeAs<Float_Object>(o)->getValue();
    case ObjectKind::STRING: {
        auto left = createStrRef(*this);
        auto right = createStrRef(o);
        return left < right;
    }
    default:
        break;
    }
    return false;
}

// ###########################
// ##     UnixFD_Object     ##
// ###########################

UnixFD_Object::~UnixFD_Object() {
    if(this->fd > STDERR_FILENO) {
        close(this->fd);    // do not close standard io file descriptor
    }
}

bool UnixFD_Object::closeOnExec(bool close) {
    if(this->fd <= STDERR_FILENO) {
        return false;
    }

    int flag = fcntl(this->fd, F_GETFD);
    if(flag == -1) {
        return false;
    }
    if(close) {
        setFlag(flag, FD_CLOEXEC);
    } else {
        unsetFlag(flag, FD_CLOEXEC);
    }
    return fcntl(this->fd, F_SETFD, flag) != -1;
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
        str += this->values[i].toString();
    }
    str += "]";
    return str;
}

static DSValue callOP(DSState &state, const DSValue &value, const char *op) {
    DSValue ret = value;
    if(!checkInvalid(state, ret)) {
        return DSValue();
    }
    auto &type = state.symbolTable.get(ret.getTypeID());
    if(!type.is(TYPE::String)) {
        auto *handle = state.symbolTable.lookupMethod(type, op);
        assert(handle != nullptr);
        ret = callMethod(state, handle, std::move(ret), makeArgs());
    }
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

static const MethodHandle *lookupCmdArg(const DSType &recvType, SymbolTable &symbolTable) {
    auto *handle = symbolTable.lookupMethod(recvType, OP_CMD_ARG);
    if(handle == nullptr) {
        handle = symbolTable.lookupMethod(recvType, OP_STR);
    }
    return handle;
}

static bool appendAsCmdArg(std::vector<DSValue> &result, DSState &state, const DSValue &value) {
    DSValue ret = value;
    if(!checkInvalid(state, ret)) {
        return false;
    }
    if(!ret.hasType(TYPE::String) && !ret.hasType(TYPE::StringArray)) {
        auto &recv = state.symbolTable.get(ret.getTypeID());
        auto *handle = lookupCmdArg(recv, state.symbolTable);
        assert(handle != nullptr);
        ret = TRY(callMethod(state, handle, std::move(ret), makeArgs()));
    }

    if(ret.hasType(TYPE::String)) {
        result.push_back(std::move(ret));
    } else {
        assert(ret.hasType(TYPE::StringArray));
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


// ########################
// ##     Map_Object     ##
// ########################

DSValue Map_Object::nextElement(DSState &ctx) {
    std::vector<DSType *> types(2);
    types[0] = &ctx.symbolTable.get(this->iter->first.getTypeID());
    types[1] = &ctx.symbolTable.get(this->iter->second.getTypeID());

    auto entry = DSValue::create<Tuple_Object>(*ctx.symbolTable.createTupleType(std::move(types)).take());
    (*typeAs<Tuple_Object>(entry))[0] = this->iter->first;
    (*typeAs<Tuple_Object>(entry))[1] = this->iter->second;
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
        str += e.first.toString();
        str += " : ";
        str += e.second.toString();
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
// ##     BaseObject     ##
// ########################

BaseObject::~BaseObject() {
    delete[] this->fieldTable;
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
        str += this->fieldTable[i].toString();
    }
    if(size == 1) {
        str += ",";
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
    if(size == 1) {
        state.toStrBuf += ",";
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
    typeAs<Error_Object>(obj)->stackTrace = ctx.getCallStack().createStackTrace();
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

// ##########################
// ##     CompiledCode     ##
// ##########################

unsigned int CompiledCode::getLineNum(unsigned int index) const {   //FIXME: binary search
    unsigned int i = 0;
    for(; this->lineNumEntries[i].address > 0; i++) {
        if(index < this->lineNumEntries[i].address) {
            break;
        }
    }
    return this->lineNumEntries[i > 0 ? i - 1 : 0].lineNum;
}

// ########################
// ##     FuncObject     ##
// ########################

std::string FuncObject::toString() const {
    std::string str;
    switch(this->code.getKind()) {
    case CodeKind::TOPLEVEL:
        str += "module(";
        break;
    case CodeKind::FUNCTION:
        str += "function(";
        break;
    case CodeKind::USER_DEFINED_CMD:
        str += "command(";
        break;
    default:
        break;
    }
    str += this->code.getName();
    str += ")";
    return str;
}

} // namespace ydsh
