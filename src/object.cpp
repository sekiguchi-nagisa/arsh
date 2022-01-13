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

#include "core.h"
#include "misc/files.h"
#include "misc/num_util.hpp"
#include "redir.h"
#include "vm.h"

namespace ydsh {

void DSObject::destroy() {
  switch (this->getKind()) {
#define GEN_CASE(K)                                                                                \
  case ObjectKind::K:                                                                              \
    delete static_cast<K##Object *>(this);                                                         \
    break;
    EACH_OBJECT_KIND(GEN_CASE)
#undef GEN_CASE
  }
}

const char *toString(ObjectKind kind) {
  const char *table[] = {
#define GEN_STR(E) #E,
      EACH_OBJECT_KIND(GEN_STR)
#undef GEN_STR
  };
  return table[static_cast<unsigned int>(kind)];
}

// #####################
// ##     DSValue     ##
// #####################

unsigned int DSValue::getTypeID() const {
  switch (this->kind()) {
  case DSValueKind::DUMMY:
    return this->asTypeId();
  case DSValueKind::BOOL:
    return static_cast<unsigned int>(TYPE::Boolean);
  case DSValueKind::SIG:
    return static_cast<unsigned int>(TYPE::Signal);
  case DSValueKind::INT:
    return static_cast<unsigned int>(TYPE::Int);
  case DSValueKind::FLOAT:
    return static_cast<unsigned int>(TYPE::Float);
  default:
    if (isSmallStr(this->kind())) {
      return static_cast<unsigned int>(TYPE::String);
    }
    assert(this->kind() == DSValueKind::OBJECT);
    return this->get()->getTypeID();
  }
}

StringRef DSValue::asStrRef() const {
  assert(this->hasStrRef());
  if (isSmallStr(this->kind())) {
    return StringRef(this->str.value, smallStrSize(this->kind()));
  }
  auto &obj = typeAs<StringObject>(*this);
  return StringRef(obj.getValue(), obj.size());
}

std::string DSValue::toString() const {
  switch (this->kind()) {
  case DSValueKind::NUMBER:
    return std::to_string(this->asNum());
  case DSValueKind::DUMMY: {
    std::string str("DSObject(");
    str += std::to_string(this->asTypeId());
    str += ")";
    return str;
  }
  case DSValueKind::GLOB_META:
    return ::toString(this->asGlobMeta());
  case DSValueKind::BOOL:
    return this->asBool() ? "true" : "false";
  case DSValueKind::SIG:
    return std::to_string(this->asSig());
  case DSValueKind::INT:
    return std::to_string(this->asInt());
  case DSValueKind::FLOAT: {
    double d = this->asFloat();
    if (std::isnan(d)) {
      return "NaN";
    }
    if (std::isinf(d)) {
      return d > 0 ? "Infinity" : "-Infinity";
    }
    return std::to_string(d);
  }
  default:
    if (this->hasStrRef()) {
      return this->asStrRef().toString();
    }
    assert(this->kind() == DSValueKind::OBJECT);
    break;
  }

  switch (this->get()->getKind()) {
  case ObjectKind::UnixFd: {
    std::string str = "/dev/fd/";
    str += std::to_string(typeAs<UnixFdObject>(*this).getValue());
    return str;
  }
  case ObjectKind::Regex:
    return typeAs<RegexObject>(*this).getStr();
  case ObjectKind::Array:
    return typeAs<ArrayObject>(*this).toString();
  case ObjectKind::Map:
    return typeAs<MapObject>(*this).toString();
  case ObjectKind::Func:
    return typeAs<FuncObject>(*this).toString();
  case ObjectKind::Job: {
    std::string str = "%";
    str += std::to_string(typeAs<JobObject>(*this).getJobID());
    return str;
  }
  default:
    std::string str("DSObject(");
    str += std::to_string(reinterpret_cast<uintptr_t>(this->get()));
    str += ")";
    return str;
  }
}

bool DSValue::opStr(DSState &state) const {
  if (this->isObject()) {
    switch (this->get()->getKind()) {
    case ObjectKind::Array:
      return typeAs<ArrayObject>(*this).opStr(state);
    case ObjectKind::Map:
      return typeAs<MapObject>(*this).opStr(state);
    case ObjectKind::Base: {
      auto &type = state.typePool.get(this->getTypeID());
      if (type.isTupleType()) {
        return typeAs<BaseObject>(*this).opStrAsTuple(state);
      }
      break;
    }
    case ObjectKind::Error:
      return typeAs<ErrorObject>(*this).opStr(state);
    default:
      break;
    }
  }
  state.toStrBuf += this->toString();
  return true;
}

bool DSValue::opInterp(DSState &state) const {
  if (this->isObject()) {
    switch (this->get()->getKind()) {
    case ObjectKind::Array:
      return typeAs<ArrayObject>(*this).opInterp(state);
    case ObjectKind::Base: {
      auto &type = state.typePool.get(this->getTypeID());
      if (type.isTupleType()) {
        return typeAs<BaseObject>(*this).opInterpAsTuple(state);
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
  if (this->hasStrRef() && o.hasStrRef()) {
    auto left = this->asStrRef();
    auto right = o.asStrRef();
    return left == right;
  }

  if (this->kind() != o.kind()) {
    return false;
  }
  switch (this->kind()) {
  case DSValueKind::EMPTY:
    return true;
  case DSValueKind::BOOL:
    return this->asBool() == o.asBool();
  case DSValueKind::SIG:
    return this->asSig() == o.asSig();
  case DSValueKind::INT:
    return this->asInt() == o.asInt();
  case DSValueKind::FLOAT:
    return compareByTotalOrder(this->asFloat(), o.asFloat()) == 0;
  default:
    assert(this->kind() == DSValueKind::OBJECT);
    if (this->get()->getKind() != o.get()->getKind()) {
      return false;
    }
    return reinterpret_cast<uintptr_t>(this->get()) == reinterpret_cast<uintptr_t>(o.get());
  }
}

size_t DSValue::hash() const {
  switch (this->kind()) {
  case DSValueKind::BOOL:
    return std::hash<bool>()(this->asBool());
  case DSValueKind::SIG:
    return std::hash<int64_t>()(this->asSig());
  case DSValueKind::INT:
    return std::hash<int64_t>()(this->asInt());
  case DSValueKind::FLOAT:
    return std::hash<int64_t>()(dobuleTobits(this->asFloat()));
  default:
    if (this->hasStrRef()) {
      return StrRefHash()(this->asStrRef());
    }
    assert(this->isObject());
    return std::hash<uintptr_t>()(reinterpret_cast<uintptr_t>(this->get()));
  }
}

int DSValue::compare(const DSValue &o) const {
  // for String
  if (this->hasStrRef() && o.hasStrRef()) {
    auto left = this->asStrRef();
    auto right = o.asStrRef();
    return left.compare(right);
  }

  assert(this->kind() == o.kind());
  switch (this->kind()) {
  case DSValueKind::BOOL: {
    int left = this->asBool() ? 1 : 0;
    int right = o.asBool() ? 1 : 0;
    return left - right;
  }
  case DSValueKind::SIG:
    return this->asSig() - o.asSig();
  case DSValueKind::INT: {
    int64_t left = this->asInt();
    int64_t right = o.asInt();
    if (left == right) {
      return 0;
    }
    return left < right ? -1 : 1;
  }
  case DSValueKind::FLOAT:
    return compareByTotalOrder(this->asFloat(), o.asFloat());
  default:
    return 1; // normally unreachable
  }
}

bool DSValue::appendAsStr(DSState &state, StringRef value) {
  assert(this->hasStrRef());

  const bool small = isSmallStr(this->kind());
  const size_t size = small ? smallStrSize(this->kind()) : typeAs<StringObject>(*this).size();
  if (size > StringObject::MAX_SIZE - value.size()) {
    raiseError(state, TYPE::OutOfRangeError, std::string("reach String size limit"));
    return false;
  }

  if (small) {
    size_t newSize = size + value.size();
    if (newSize <= smallStrSize(DSValueKind::SSTR14)) {
      memcpy(this->str.value + size, value.data(), value.size());
      this->str.kind = toSmallStrKind(newSize);
      this->str.value[newSize] = '\0';
      return true;
    }
    (*this) = DSValue::create<StringObject>(StringRef(this->str.value, size));
  }
  typeAs<StringObject>(*this).append(value);
  return true;
}

// ###########################
// ##     UnixFD_Object     ##
// ###########################

UnixFdObject::~UnixFdObject() {
  if (this->fd > STDERR_FILENO) {
    close(this->fd); // do not close standard io file descriptor
  }
}

bool UnixFdObject::closeOnExec(bool close) const {
  if (this->fd <= STDERR_FILENO) {
    return false;
  }
  return setCloseOnExec(this->fd, close);
}

// #########################
// ##     RegexObject     ##
// #########################

int RegexObject::match(DSState &state, StringRef ref, ArrayObject *out) {
  std::string errorStr;
  int matchCount = this->re.match(ref, errorStr);
  if (!errorStr.empty()) {
    raiseError(state, TYPE::InvalidOperationError, std::move(errorStr));
  }
  if (out && matchCount > 0) {
    out->refValues().reserve(matchCount);
    for (int i = 0; i < matchCount; i++) {
      PCRECapture capture; // NOLINT
      bool set = this->re.getCaptureAt(i, capture);
      auto v = set ? DSValue::createStr(ref.slice(capture.begin, capture.end))
                   : DSValue::createInvalid();
      out->refValues().push_back(std::move(v));
    }
  }
  return matchCount;
}

bool RegexObject::replace(DSState &state, DSValue &value, StringRef repl) {
  auto ret = DSValue::createStr();
  unsigned int count = 0;
  for (auto target = value.asStrRef(); !target.empty(); count++) {
    std::string errorStr;
    int matchCount = this->re.match(target, errorStr);
    if (matchCount < 0) {
      if (!errorStr.empty()) {
        raiseError(state, TYPE::InvalidOperationError, std::move(errorStr));
        return false;
      }

      if (count == 0) { // do nothing
        return true;
      } else if (!ret.appendAsStr(state, target)) {
        return false;
      }
      break;
    }

    PCRECapture capture; // NOLINT
    bool set = this->re.getCaptureAt(0, capture);
    (void)set;
    assert(set);

    if (!ret.appendAsStr(state, target.slice(0, capture.begin))) {
      return false;
    }
    if (!ret.appendAsStr(state, repl)) {
      return false;
    }
    target = target.slice(capture.end, target.size());
  }
  value = std::move(ret);
  return true;
}

// ##########################
// ##     Array_Object     ##
// ##########################

static bool checkInvalid(DSState &st, DSValue &v) {
  if (v.isInvalid()) {
    raiseError(st, TYPE::UnwrappingError, "invalid value");
    return false;
  }
  return true;
}

#define TRY(E)                                                                                     \
  ({                                                                                               \
    auto v = E;                                                                                    \
    if (state.hasError()) {                                                                        \
      return false;                                                                                \
    }                                                                                              \
    std::forward<decltype(v)>(v);                                                                  \
  })

std::string ArrayObject::toString() const {
  std::string str = "[";
  unsigned int size = this->values.size();
  for (unsigned int i = 0; i < size; i++) {
    if (i > 0) {
      str += ", ";
    }
    str += this->values[i].toString();
  }
  str += "]";
  return str;
}

static DSValue callOP(DSState &state, const DSValue &value, const char *op) {
  DSValue ret = value;
  if (!checkInvalid(state, ret)) {
    return DSValue();
  }
  auto &type = state.typePool.get(ret.getTypeID());
  if (!type.is(TYPE::String)) {
    auto *handle = state.typePool.lookupMethod(type, op);
    assert(handle != nullptr);
    ret = VM::callMethod(state, *handle, std::move(ret), makeArgs());
  }
  return ret;
}

bool ArrayObject::opStr(DSState &state) const {
  state.toStrBuf += "[";
  unsigned int size = this->values.size();
  for (unsigned int i = 0; i < size; i++) {
    if (i > 0) {
      state.toStrBuf += ", ";
    }

    auto ret = TRY(callOP(state, this->values[i], OP_STR));
    if (!ret.isInvalid()) {
      auto ref = ret.asStrRef();
      state.toStrBuf += ref;
    }
  }
  state.toStrBuf += "]";
  return true;
}

bool ArrayObject::opInterp(DSState &state) const {
  unsigned int size = this->values.size();
  for (unsigned int i = 0; i < size; i++) {
    if (i > 0) {
      state.toStrBuf += " ";
    }

    auto ret = TRY(callOP(state, this->values[i], OP_INTERP));
    if (!ret.isInvalid()) {
      auto ref = ret.asStrRef();
      state.toStrBuf += ref;
    }
  }
  return true;
}

static const MethodHandle *lookupCmdArg(const DSType &recvType, TypePool &pool) {
  auto *handle = pool.lookupMethod(recvType, OP_CMD_ARG);
  if (handle == nullptr) {
    handle = pool.lookupMethod(recvType, OP_STR);
  }
  return handle;
}

static bool appendAsCmdArg(std::vector<DSValue> &result, DSState &state, const DSValue &value) {
  DSValue ret = value;
  if (!checkInvalid(state, ret)) {
    return false;
  }
  if (!ret.hasType(TYPE::String) && !ret.hasType(TYPE::StringArray)) {
    auto &recv = state.typePool.get(ret.getTypeID());
    auto *handle = lookupCmdArg(recv, state.typePool);
    assert(handle != nullptr);
    ret = TRY(VM::callMethod(state, *handle, std::move(ret), makeArgs()));
  }

  if (ret.hasType(TYPE::String)) {
    result.push_back(std::move(ret));
  } else {
    assert(ret.hasType(TYPE::StringArray));
    auto &tempArray = typeAs<ArrayObject>(ret);
    for (auto &tempValue : tempArray.getValues()) {
      result.push_back(tempValue);
    }
  }
  return true;
}

DSValue ArrayObject::opCmdArg(DSState &state) const {
  auto result = DSValue::create<ArrayObject>(state.typePool.get(TYPE::StringArray));
  for (auto &e : this->values) {
    if (!appendAsCmdArg(typeAs<ArrayObject>(result).values, state, e)) {
      return DSValue();
    }
  }
  return result;
}

// ########################
// ##     Map_Object     ##
// ########################

std::string MapObject::toString() const {
  std::string str = "[";
  unsigned int count = 0;
  for (auto &e : this->valueMap) {
    if (count++ > 0) {
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
  for (auto &e : this->valueMap) {
    if (count++ > 0) {
      state.toStrBuf += ", ";
    }

    // key
    auto ret = TRY(callOP(state, e.first, OP_STR));
    if (!ret.isInvalid()) {
      auto ref = ret.asStrRef();
      state.toStrBuf += ref;
    }

    state.toStrBuf += " : ";

    // value
    ret = TRY(callOP(state, e.second, OP_STR));
    if (!ret.isInvalid()) {
      auto ref = ret.asStrRef();
      state.toStrBuf += ref;
    }
  }
  state.toStrBuf += "]";
  return true;
}

// ###########################
// ##     MapIterObject     ##
// ###########################

DSValue MapIterObject::next(TypePool &pool) {
  std::vector<const DSType *> types(2);
  types[0] = &pool.get(this->iter->first.getTypeID());
  types[1] = &pool.get(this->iter->second.getTypeID());

  auto *type = pool.createTupleType(std::move(types)).take();
  auto entry = DSValue::create<BaseObject>(cast<TupleType>(*type));
  typeAs<BaseObject>(entry)[0] = this->iter->first;
  typeAs<BaseObject>(entry)[1] = this->iter->second;
  ++this->iter;

  return entry;
}

// ########################
// ##     BaseObject     ##
// ########################

BaseObject::~BaseObject() {
  for (unsigned int i = 0; i < this->fieldSize; i++) {
    (*this)[i].~DSValue();
  }
}

bool BaseObject::opStrAsTuple(DSState &state) const {
  assert(state.typePool.get(this->getTypeID()).isTupleType());

  state.toStrBuf += "(";
  unsigned int size = this->getFieldSize();
  for (unsigned int i = 0; i < size; i++) {
    if (i > 0) {
      state.toStrBuf += ", ";
    }

    auto ret = TRY(callOP(state, (*this)[i], OP_STR));
    if (!ret.isInvalid()) {
      auto ref = ret.asStrRef();
      state.toStrBuf += ref;
    }
  }
  if (size == 1) {
    state.toStrBuf += ",";
  }
  state.toStrBuf += ")";
  return true;
}

bool BaseObject::opInterpAsTuple(DSState &state) const {
  assert(state.typePool.get(this->getTypeID()).isTupleType());

  unsigned int size = this->getFieldSize();
  for (unsigned int i = 0; i < size; i++) {
    if (i > 0) {
      state.toStrBuf += " ";
    }

    auto ret = TRY(callOP(state, (*this)[i], OP_INTERP));
    if (!ret.isInvalid()) {
      auto ref = ret.asStrRef();
      state.toStrBuf += ref;
    }
  }
  return true;
}

DSValue BaseObject::opCmdArgAsTuple(DSState &state) const {
  assert(state.typePool.get(this->getTypeID()).isTupleType());

  auto result = DSValue::create<ArrayObject>(state.typePool.get(TYPE::StringArray));
  unsigned int size = this->getFieldSize();
  for (unsigned int i = 0; i < size; i++) {
    if (!appendAsCmdArg(typeAs<ArrayObject>(result).refValues(), state, (*this)[i])) {
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
  for (auto &s : this->stackTrace) {
    fprintf(stderr, "    from %s:%d '%s()'\n", s.getSourceName().c_str(), s.getLineNum(),
            s.getCallerName().c_str());
  }
}

DSValue ErrorObject::newError(const DSState &state, const DSType &type, DSValue &&message) {
  auto traces = state.getCallStack().createStackTrace();
  auto name = DSValue::createStr(type.getName());
  return DSValue::create<ErrorObject>(type, std::move(message), std::move(name), std::move(traces));
}

std::string ErrorObject::createHeader(const DSState &state) const {
  auto ref = this->message.asStrRef();
  std::string str = state.typePool.get(this->getTypeID()).getNameRef().toString();
  str += ": ";
  str += ref;
  return str;
}

// ##########################
// ##     CompiledCode     ##
// ##########################

unsigned int CompiledCode::getLineNum(unsigned int index) const { // FIXME: binary search
  unsigned int i = 0;
  for (; this->lineNumEntries[i]; i++) {
    if (index <= this->lineNumEntries[i].address) {
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
  switch (this->code.getKind()) {
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

// ##########################
// ##     EnvCtxObject     ##
// ##########################

EnvCtxObject::~EnvCtxObject() {
  for (auto &e : this->envs) {
    auto &name = e.first;
    auto &value = e.second;
    if (name.hasType(TYPE::Int)) {
      auto k = name.asInt();
      assert(k > -1);
      auto index = static_cast<unsigned int>(k);
      this->state.setGlobal(index, value);
    } else {
      assert(name.hasStrRef());
      const char *envName = name.asCStr();
      if (value.isInvalid()) { // unset
        unsetenv(envName);
      } else {
        setenv(envName, value.asCStr(), 1);
      }
    }
  }
}

void EnvCtxObject::setAndSaveEnv(DSValue &&name, DSValue &&value) {
  assert(!name.asStrRef().hasNullChar());

  const char *envName = name.asCStr();
  const char *oldEnv = getenv(envName);

  // save old env
  this->envs.emplace_back(name, oldEnv ? DSValue::createStr(oldEnv) : DSValue::createInvalid());

  // overwrite env
  setenv(envName, value.asCStr(), 1);

  if (name.asStrRef() == VAR_IFS) { // if env name is IFS, also save and set IFS global variable
    // save old IFS
    auto ifsIndex = DSValue::createInt(toIndex(BuiltinVarOffset::IFS));
    this->envs.emplace_back(ifsIndex, this->state.getGlobal(BuiltinVarOffset::IFS));

    // overwrite IFS
    this->state.setGlobal(BuiltinVarOffset::IFS, std::move(value));
  }
}

} // namespace ydsh
