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
    case DSValueKind::INT:
        return static_cast<unsigned int>(TYPE::Int);
    case DSValueKind::FLOAT:
        return static_cast<unsigned int>(TYPE::Float);
    default:
        if(isSmallStr(this->kind())) {
            return static_cast<unsigned int>(TYPE::String);
        }
        assert(this->kind() == DSValueKind::OBJECT);
        return this->get()->getTypeID();
    }
}

StringRef DSValue::asStrRef() const {
    assert(this->hasStrRef());
    if(isSmallStr(this->kind())) {
        return StringRef(this->str.value, smallStrSize(this->kind()));
    }
    auto *obj = typeAs<StringObject>(*this);
    return StringRef(obj->getValue(), obj->size());
}

std::string DSValue::toString() const {
    switch(this->kind()) {
    case DSValueKind::NUMBER:
        return std::to_string(this->asNum());
    case DSValueKind::BOOL:
        return this->asBool() ? "true" : "false";
    case DSValueKind::SIG:
        return std::to_string(this->asSig());
    case DSValueKind::INT:
        return std::to_string(this->asInt());
    case DSValueKind::FLOAT:
        return std::to_string(this->asFloat());
    default:
        if(this->hasStrRef()) {
            return this->asStrRef().toString();
        }
        assert(this->kind() == DSValueKind::OBJECT);
        break;
    }

    switch(this->get()->getKind()) {
    case DSObject::Long:
        return std::to_string(typeAs<LongObject>(*this)->getValue());
    case DSObject::UnixFd: {
        std::string str = "/dev/fd/";
        str += std::to_string(typeAs<UnixFdObject>(*this)->getValue());
        return str;
    }
    case DSObject::Regex:
        return typeAs<RegexObject>(*this)->getStr();
    case DSObject::Array:
        return typeAs<ArrayObject>(*this)->toString();
    case DSObject::Map:
        return typeAs<MapObject>(*this)->toString();
    case DSObject::Func:
        return typeAs<FuncObject>(*this)->toString();
    case DSObject::JobImpl: {
        std::string str = "%";
        str += std::to_string(typeAs<JobImplObject>(*this)->getJobID());
        return str;
    }
    default:
        break;
    }

    std::string str("DSObject(");
    str += std::to_string(reinterpret_cast<long>(this->get()));
    str += ")";
    return str;
}

bool DSValue::opStr(DSState &state) const {
    if(this->isObject()) {
        switch(this->get()->getKind()) {
        case DSObject::Array:
            return typeAs<ArrayObject>(*this)->opStr(state);
        case DSObject::Map:
            return typeAs<MapObject>(*this)->opStr(state);
        case DSObject::Base: {
            auto &type = state.symbolTable.get(this->getTypeID());
            if(state.symbolTable.getTypePool().isTupleType(type)) {
                return typeAs<BaseObject>(*this)->opStrAsTuple(state);
            }
            break;
        }
        case DSObject::Error:
            return typeAs<ErrorObject>(*this)->opStr(state);
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
        case DSObject::Array:
            return typeAs<ArrayObject>(*this)->opInterp(state);
        case DSObject::Base: {
            auto &type = state.symbolTable.get(this->getTypeID());
            if(state.symbolTable.getTypePool().isTupleType(type)) {
                return typeAs<BaseObject>(*this)->opInterpAsTuple(state);
            }
            break;
        }
        default:
            break;
        }
    }
    return this->opStr(state);
}

bool DSValue::equals(const DSValue &o) const {
    // for String
    if(this->hasStrRef() && o.hasStrRef()) {
        auto left = this->asStrRef();
        auto right = o.asStrRef();
        return left == right;
    }

    if(this->kind() != o.kind()) {
        return false;
    }
    switch(this->kind()) {
    case DSValueKind::EMPTY:
        return true;
    case DSValueKind::BOOL:
        return this->asBool() == o.asBool();
    case DSValueKind::SIG:
        return this->asSig() == o.asSig();
    case DSValueKind::INT:
        return this->asInt() == o.asInt();
    case DSValueKind::FLOAT:
        return this->asFloat() == o.asFloat();
    default:
        assert(this->kind() == DSValueKind::OBJECT);
        break;
    }

    if(this->get()->getKind() != o.get()->getKind()) {
        return false;
    }

    switch(this->get()->getKind()) {
    case DSObject::Long:
        return typeAs<LongObject>(*this)->getValue() == typeAs<LongObject>(o)->getValue();
    default:
        return reinterpret_cast<uint64_t>(this->get()) == reinterpret_cast<uint64_t>(o.get());
    }
}

size_t DSValue::hash() const {
    switch(this->kind()) {
    case DSValueKind::BOOL:
        return std::hash<bool>()(this->asBool());
    case DSValueKind::SIG:
        return std::hash<int64_t>()(this->asSig());
    case DSValueKind::INT:
        return std::hash<int64_t>()(this->asInt());
    case DSValueKind::FLOAT:
        return std::hash<double>()(this->asFloat());
    default:
        if(this->hasStrRef()) {
            return std::hash<StringRef>()(this->asStrRef());
        }
        assert(this->isObject());
        break;
    }

    switch(this->get()->getKind()) {
    case DSObject::Long:
        return std::hash<long>()(typeAs<LongObject>(*this)->getValue());
    default:
        return std::hash<uint64_t>()(reinterpret_cast<uint64_t>(this->get()));
    }
}

bool DSValue::compare(const DSValue &o) const {
    // for String
    if(this->hasStrRef() && o.hasStrRef()) {
        auto left = this->asStrRef();
        auto right = o.asStrRef();
        return left < right;
    }

    assert(this->kind() == o.kind());
    switch(this->kind()) {
    case DSValueKind::BOOL: {
        int left = this->asBool() ? 1 : 0;
        int right = o.asBool() ? 1 : 0;
        return left < right;
    }
    case DSValueKind::SIG:
        return this->asSig() < o.asSig();
    case DSValueKind::INT:
        return this->asInt() < o.asInt();
    case DSValueKind::FLOAT:
        return this->asFloat() < o.asFloat();
    default:
        assert(this->kind() == DSValueKind::OBJECT);
        break;
    }

    assert(this->get()->getKind() == o.get()->getKind());
    switch(this->get()->getKind()) {
    case DSObject::Long:
        return typeAs<LongObject>(*this)->getValue() < typeAs<LongObject>(o)->getValue();
    default:
        break;
    }
    return false;
}

bool DSValue::appendAsStr(StringRef value) {
    assert(this->hasStrRef());

    const bool small = isSmallStr(this->kind());
    const size_t size = small ? smallStrSize(this->kind()) : typeAs<StringObject>(*this)->size();
    if(size > StringObject::MAX_SIZE - value.size()) {
        return false;
    }

    if(small) {
        size_t newSize = size + value.size();
        if(newSize <= smallStrSize(DSValueKind::SSTR14)) {
            memcpy(this->str.value + size, value.data(), value.size());
            this->str.kind = toSmallStrKind(newSize);
            this->str.value[newSize] = '\0';
            return true;
        }
        (*this) = DSValue::create<StringObject>(StringRef(this->str.value, size));
    }
    typeAs<StringObject>(*this)->append(value);
    return true;
}

// ###########################
// ##     UnixFD_Object     ##
// ###########################

UnixFdObject::~UnixFdObject() {
    if(this->fd > STDERR_FILENO) {
        close(this->fd);    // do not close standard io file descriptor
    }
}

bool UnixFdObject::closeOnExec(bool close) {
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


std::string ArrayObject::toString() const {
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

bool ArrayObject::opStr(DSState &state) const {
    state.toStrBuf += "[";
    unsigned int size = this->values.size();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            state.toStrBuf += ", ";
        }

        auto ret = TRY(callOP(state, this->values[i], OP_STR));
        if(!ret.isInvalid()) {
            auto ref = ret.asStrRef();
            state.toStrBuf.append(ref.data(), ref.size());
        }
    }
    state.toStrBuf += "]";
    return true;
}

bool ArrayObject::opInterp(DSState &state) const {
    unsigned int size = this->values.size();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            state.toStrBuf += " ";
        }

        auto ret = TRY(callOP(state, this->values[i], OP_INTERP));
        if(!ret.isInvalid()) {
            auto ref = ret.asStrRef();
            state.toStrBuf.append(ref.data(), ref.size());
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
        auto *tempArray = typeAs<ArrayObject>(ret);
        for(auto &tempValue : tempArray->getValues()) {
            result.push_back(tempValue);
        }
    }
    return true;
}

DSValue ArrayObject::opCmdArg(DSState &state) const {
    auto result = DSValue::create<ArrayObject>(state.symbolTable.get(TYPE::StringArray));
    for(auto &e : this->values) {
        if(!appendAsCmdArg(typeAs<ArrayObject>(result)->values, state, e)) {
            return DSValue();
        }
    }
    return result;
}


// ########################
// ##     Map_Object     ##
// ########################

DSValue MapObject::nextElement(DSState &ctx) {
    std::vector<DSType *> types(2);
    types[0] = &ctx.symbolTable.get(this->iter->first.getTypeID());
    types[1] = &ctx.symbolTable.get(this->iter->second.getTypeID());

    auto entry = DSValue::create<BaseObject>(*ctx.symbolTable.createTupleType(std::move(types)).take());
    (*typeAs<BaseObject>(entry))[0] = this->iter->first;
    (*typeAs<BaseObject>(entry))[1] = this->iter->second;
    ++this->iter;

    return entry;
}

std::string MapObject::toString() const {
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

bool MapObject::opStr(DSState &state) const {
    state.toStrBuf += "[";
    unsigned int count = 0;
    for(auto &e : this->valueMap) {
        if(count++ > 0) {
            state.toStrBuf += ", ";
        }

        // key
        auto ret = TRY(callOP(state, e.first, OP_STR));
        if(!ret.isInvalid()) {
            auto ref = ret.asStrRef();
            state.toStrBuf.append(ref.data(), ref.size());
        }

        state.toStrBuf += " : ";

        // value
        ret = TRY(callOP(state, e.second, OP_STR));
        if(!ret.isInvalid()) {
            auto ref = ret.asStrRef();
            state.toStrBuf.append(ref.data(), ref.size());
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

bool BaseObject::opStrAsTuple(DSState &state) const {
    assert(state.symbolTable.getTypePool().isTupleType(state.symbolTable.get(this->getTypeID())));

    state.toStrBuf += "(";
    unsigned int size = this->getFieldSize();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            state.toStrBuf += ", ";
        }

        auto ret = TRY(callOP(state, this->fieldTable[i], OP_STR));
        if(!ret.isInvalid()) {
            auto ref = ret.asStrRef();
            state.toStrBuf.append(ref.data(), ref.size());
        }
    }
    if(size == 1) {
        state.toStrBuf += ",";
    }
    state.toStrBuf += ")";
    return true;
}

bool BaseObject::opInterpAsTuple(DSState &state) const {
    assert(state.symbolTable.getTypePool().isTupleType(state.symbolTable.get(this->getTypeID())));

    unsigned int size = this->getFieldSize();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            state.toStrBuf += " ";
        }

        auto ret = TRY(callOP(state, this->fieldTable[i], OP_INTERP));
        if(!ret.isInvalid()) {
            auto ref = ret.asStrRef();
            state.toStrBuf.append(ref.data(), ref.size());
        }
    }
    return true;
}

DSValue BaseObject::opCmdArgAsTuple(DSState &state) const {
    assert(state.symbolTable.getTypePool().isTupleType(state.symbolTable.get(this->getTypeID())));

    auto result = DSValue::create<ArrayObject>(state.symbolTable.get(TYPE::StringArray));
    unsigned int size = this->getFieldSize();
    for(unsigned int i = 0; i < size; i++) {
        if(!appendAsCmdArg(typeAs<ArrayObject>(result)->refValues(), state, this->fieldTable[i])) {
            return DSValue();
        }
    }
    return result;
}

// ##########################
// ##     Error_Object     ##
// ##########################

bool ErrorObject::opStr(DSState &state) const {
    state.toStrBuf += this->createHeader(state);
    return true;
}

void ErrorObject::printStackTrace(DSState &ctx) {
    // print header
    fprintf(stderr, "%s\n", this->createHeader(ctx).c_str());

    // print stack trace
    for(auto &s : this->stackTrace) {
        fprintf(stderr, "    from %s:%d '%s()'\n",
                s.getSourceName().c_str(), s.getLineNum(), s.getCallerName().c_str());
    }
}

DSValue ErrorObject::newError(const DSState &state, const DSType &type, DSValue &&message) {
    auto traces = state.getCallStack().createStackTrace();
    auto name = DSValue::createStr(state.symbolTable.getTypeName(type));
    return DSValue::create<ErrorObject>(type, std::move(message), std::move(name), std::move(traces));
}

std::string ErrorObject::createHeader(const DSState &state) const {
    auto ref = this->message.asStrRef();
    std::string str = state.symbolTable.getTypeName(state.symbolTable.get(this->getTypeID()));
    str += ": ";
    str.append(ref.data(), ref.size());
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
