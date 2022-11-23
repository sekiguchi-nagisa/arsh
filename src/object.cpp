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
#include "line_editor.h"
#include "misc/format.hpp"
#include "misc/num_util.hpp"
#include "redir.h"
#include "vm.h"

namespace ydsh {

void DSObject::destroy() {
  switch (this->getKind()) {
#define GEN_CASE(K)                                                                                \
  case ObjectKind::K:                                                                              \
    delete cast<K##Object>(this);                                                                  \
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
  case DSValueKind::NUM_LIST: {
    auto &nums = this->asNumList();
    char buf[256];
    snprintf(buf, std::size(buf), "[%u, %u, %u]", nums[0], nums[1], nums[2]);
    return std::string(buf);
  }
  case DSValueKind::DUMMY: {
    unsigned int typeId = this->asTypeId();
    if (typeId == static_cast<unsigned int>(TYPE::Module)) {
      std::string str = OBJ_TEMP_MOD_PREFIX; // for temporary module descriptor
      str += std::to_string(this->asNumList()[1]);
      str += ")";
      return str;
    } else {
      std::string str("DSObject(");
      str += std::to_string(typeId);
      str += ")";
      return str;
    }
  }
  case DSValueKind::EXPAND_META:
    return ::toString(this->asExpandMeta().first);
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
  case ObjectKind::Base:
    return typeAs<BaseObject>(*this).toString();
  case ObjectKind::Func:
    return typeAs<FuncObject>(*this).toString();
  case ObjectKind::Closure: {
    char buf[32]; // hex of 64bit pointer is up to 16 chars
    snprintf(buf, std::size(buf), "closure(0x%zx)",
             reinterpret_cast<uintptr_t>(&typeAs<ClosureObject>(*this).getFuncObj()));
    return std::string(buf);
  }
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

#define GUARD_RECURSION(state)                                                                     \
  RecursionGuard _guard(state);                                                                    \
  do {                                                                                             \
    if (!_guard.checkLimit()) {                                                                    \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (!(E)) {                                                                                    \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

bool DSValue::opStr(StrBuilder &builder) const {
  GUARD_RECURSION(builder.getState());

  if (this->isInvalid()) {
    raiseError(builder.getState(), TYPE::UnwrappingError, "invalid value");
    return false;
  }
  if (this->isObject()) {
    switch (this->get()->getKind()) {
    case ObjectKind::Array:
      return typeAs<ArrayObject>(*this).opStr(builder);
    case ObjectKind::Map:
      return typeAs<MapObject>(*this).opStr(builder);
    case ObjectKind::Base:
      return typeAs<BaseObject>(*this).opStrAsTupleRecord(builder);
    case ObjectKind::Error:
      return typeAs<ErrorObject>(*this).opStr(builder);
    default:
      break;
    }
  }
  return builder.add(this->toString());
}

bool DSValue::opInterp(StrBuilder &builder) const {
  GUARD_RECURSION(builder.getState());

  if (this->isObject()) {
    switch (this->get()->getKind()) {
    case ObjectKind::Array: {
      unsigned int count = 0;
      for (auto &e : typeAs<ArrayObject>(*this).getValues()) {
        if (e.isInvalid()) {
          continue;
        }
        if (count++ > 0) {
          TRY(builder.add(" "));
        }
        TRY(e.opInterp(builder));
      }
      return true;
    }
    case ObjectKind::Map: {
      unsigned int count = 0;
      for (auto &e : typeAs<MapObject>(*this).getValueMap()) {
        if (e.second.isInvalid()) {
          continue;
        }
        if (count++ > 0) {
          TRY(builder.add(" "));
        }
        TRY(e.first.opInterp(builder) && builder.add(" ") && e.second.opInterp(builder));
      }
      return true;
    }
    case ObjectKind::Base: {
      auto &obj = typeAs<BaseObject>(*this);
      assert(builder.getState().typePool.get(this->getTypeID()).isTupleType() ||
             builder.getState().typePool.get(this->getTypeID()).isRecordType());
      unsigned int size = obj.getFieldSize();
      unsigned int count = 0;
      for (unsigned int i = 0; i < size; i++) {
        auto &e = obj[i];
        if (e.isInvalid()) {
          continue;
        }
        if (count++ > 0) {
          TRY(builder.add(" "));
        }
        TRY(e.opInterp(builder));
      }
      return true;
    }
    default:
      break;
    }
  }
  return this->opStr(builder);
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
    return std::hash<int64_t>()(doubleToBits(this->asFloat()));
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
    raiseError(state, TYPE::OutOfRangeError, STRING_LIMIT_ERROR);
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

DSValue DSValue::createStr(const GraphemeScanner::Result &ret) {
  if (!ret.hasInvalid) {
    return DSValue::createStr(ret.ref);
  }

  std::string value;
  for (unsigned int i = 0; i < ret.codePointCount; i++) {
    auto codePoint = ret.codePoints[i];
    if (codePoint == -1) {
      value += UnicodeUtil::REPLACEMENT_CHAR_UTF8;
    } else {
      char buf[8];
      unsigned int bufSize = UnicodeUtil::codePointToUtf8(codePoint, buf);
      value.append(buf, bufSize);
    }
  }
  assert(value.size() <= StringObject::MAX_SIZE);
  return DSValue::createStr(std::move(value));
}

std::string toPrintable(StringRef ref) {
  auto old = errno;
  std::string ret;
  for (int ch : ref) { // for arm32/arm64
    if ((ch >= 0 && ch < 32) || ch == 127) {
      char d[16];
      snprintf(d, std::size(d), "\\x%02x", ch);
      ret += d;
    } else {
      ret += static_cast<char>(ch);
    }
  }
  errno = old;
  return ret;
}

// ###########################
// ##     UnixFD_Object     ##
// ###########################

UnixFdObject::~UnixFdObject() {
  if (this->fd > STDERR_FILENO) {
    close(this->fd); // do not close standard io file descriptor
  }
}

// #########################
// ##     RegexObject     ##
// #########################

int RegexObject::match(DSState &state, StringRef ref, ArrayObject *out) {
  std::string errorStr;
  int matchCount = this->re.match(ref, errorStr);
  if (!errorStr.empty()) {
    raiseError(state, TYPE::RegexMatchError, std::move(errorStr));
  }
  if (out && matchCount > 0) {
    out->refValues().reserve(matchCount);
    for (int i = 0; i < matchCount; i++) {
      PCRECapture capture; // NOLINT
      bool set = this->re.getCaptureAt(i, capture);
      auto v = set ? DSValue::createStr(ref.slice(capture.begin, capture.end))
                   : DSValue::createInvalid();
      out->append(std::move(v));
    }
    ASSERT_ARRAY_SIZE(*out);
  }
  return matchCount;
}

// ##########################
// ##     Array_Object     ##
// ##########################

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

bool ArrayObject::opStr(StrBuilder &builder) const {
  TRY(builder.add("["));
  unsigned int size = this->values.size();
  for (unsigned int i = 0; i < size; i++) {
    if (i > 0) {
      TRY(builder.add(", "));
    }
    TRY(this->values[i].opStr(builder));
  }
  return builder.add("]");
}

bool ArrayObject::append(DSState &state, DSValue &&obj) {
  if (this->size() == MAX_SIZE) {
    raiseError(state, TYPE::OutOfRangeError, "reach Array size limit");
    return false;
  }
  this->values.push_back(std::move(obj));
  return true;
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

bool MapObject::opStr(StrBuilder &builder) const {
  TRY(builder.add("["));
  unsigned int count = 0;
  for (auto &e : this->valueMap) {
    if (count++ > 0) {
      TRY(builder.add(", "));
    }
    TRY(e.first.opStr(builder)); // key
    TRY(builder.add(" : "));
    TRY(e.second.opStr(builder)); // value
  }
  return builder.add("]");
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
    (*this)[this->fieldSize - 1 - i].~DSValue(); // destruct object reverse order
  }
}

std::string BaseObject::toString() const {
  std::string value = "(";
  for (unsigned int i = 0; i < this->fieldSize; i++) {
    if (i > 0) {
      value += ", ";
    }
    value += (*this)[i].toString();
  }
  value += ")";
  return value;
}

bool BaseObject::opStrAsTupleRecord(StrBuilder &builder) const {
  auto &type = builder.getState().typePool.get(this->getTypeID());
  if (type.isTupleType()) {
    TRY(builder.add("("));
    unsigned int size = this->getFieldSize();
    for (unsigned int i = 0; i < size; i++) {
      if (i > 0) {
        TRY(builder.add(", "));
      }
      TRY((*this)[i].opStr(builder));
    }
    if (size == 1) {
      TRY(builder.add(","));
    }
    return builder.add(")");
  } else {
    assert(type.isRecordType());
    auto &recordType = cast<RecordType>(type);
    TRY(builder.add("{"));
    unsigned int size = this->getFieldSize();
    std::vector<StringRef> buf;
    buf.resize(size);
    for (auto &e : recordType.getHandleMap()) {
      if (e.second->is(HandleKind::TYPE_ALIAS)) {
        continue;
      }
      buf[e.second->getIndex()] = e.first;
    }
    for (unsigned int i = 0; i < size; i++) {
      if (i > 0) {
        TRY(builder.add(", "));
      }
      TRY(builder.add(buf[i]));
      TRY(builder.add(" : "));
      TRY((*this)[i].opStr(builder));
    }
    return builder.add("}");
  }
}

bool CmdArgsBuilder::add(DSValue &&arg) {
  GUARD_RECURSION(this->state);

  if (arg.isInvalid()) {
    return true; // do nothing
  }

  if (arg.hasStrRef()) {
    return this->argv->append(this->state, std::move(arg));
  }

  if (arg.isObject()) {
    switch (arg.get()->getKind()) {
    case ObjectKind::UnixFd: {
      /**
       * when pass fd object to command arguments,
       * unset close-on-exec and add pseudo redirection entry
       */
      if (!this->redir) {
        this->redir = DSValue::create<RedirObject>();
      }

      if (arg.get()->getRefcount() > 1) {
        if (int newFd = dup(typeAs<UnixFdObject>(arg).getValue()); newFd > -1) {
          arg = DSValue::create<UnixFdObject>(newFd);
        }
      }
      auto strObj = DSValue::createStr(arg.toString());
      bool r = this->argv->append(this->state, std::move(strObj));
      if (r) {
        if (!typeAs<UnixFdObject>(arg).closeOnExec(false)) {
          raiseSystemError(this->state, errno, "failed to pass FD object to command arguments");
          return false;
        }
        typeAs<RedirObject>(this->redir).addEntry(std::move(arg), RedirOp::NOP, -1);
      }
      return r;
    }
    case ObjectKind::Array:
      for (auto &e : typeAs<ArrayObject>(arg).getValues()) {
        TRY(this->add(DSValue(e)));
      }
      return true;
    case ObjectKind::Map:
      for (auto &e : typeAs<MapObject>(arg).getValueMap()) {
        if (e.second.isInvalid()) {
          continue;
        }
        TRY(this->add(DSValue(e.first)) && this->add(DSValue(e.second)));
      }
      return true;
    case ObjectKind::Base: {
      auto &obj = typeAs<BaseObject>(arg);
      unsigned int size = obj.getFieldSize();
      for (unsigned int i = 0; i < size; i++) {
        TRY(this->add(DSValue(obj[i])));
      }
      return true;
    }
    default:
      break;
    }
  }
  StrBuilder builder(this->state);
  TRY(arg.opStr(builder));
  return this->add(std::move(builder).take());
}

// ##########################
// ##     Error_Object     ##
// ##########################

bool ErrorObject::opStr(StrBuilder &builder) const {
  auto ref = this->message.asStrRef();
  return builder.add(builder.getState().typePool.get(this->getTypeID()).getNameRef()) &&
         builder.add(": ") && builder.add(ref);
}

void ErrorObject::printStackTrace(DSState &state) {
  StrBuilder builder(state);
  bool r = this->opStr(builder);
  (void)r;
  assert(r);

  // print header
  fprintf(stderr, "%s\n", std::move(builder).take().asCStr());

  // print stack trace
  for (auto &s : this->stackTrace) {
    fprintf(stderr, "    from %s:%d '%s()'\n", s.getSourceName().c_str(), s.getLineNum(),
            s.getCallerName().c_str());
  }
}

DSValue ErrorObject::newError(const DSState &state, const DSType &type, DSValue &&message,
                              int64_t status) {
  std::vector<StackTraceElement> traces;
  state.getCallStack().fillStackTrace([&traces](StackTraceElement &&e) {
    traces.push_back(std::move(e));
    return true;
  });
  auto name = DSValue::createStr(type.getName());
  return DSValue::create<ErrorObject>(type, std::move(message), std::move(name), status,
                                      std::move(traces));
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

StackTraceElement CompiledCode::toTraceElement(unsigned int index) const {
  unsigned int lineNum = this->getLineNum(index);
  std::string callableName;
  switch (this->getKind()) {
  case CodeKind::TOPLEVEL:
    callableName += "<toplevel>";
    break;
  case CodeKind::FUNCTION:
    callableName += "function ";
    callableName += this->getName();
    break;
  case CodeKind::USER_DEFINED_CMD:
    callableName += "command ";
    callableName += this->getName();
    break;
  default:
    break;
  }
  return {this->sourceName, lineNum, std::move(callableName)};
}

// ########################
// ##     FuncObject     ##
// ########################

std::string FuncObject::toString() const {
  std::string str;
  switch (this->code.getKind()) {
  case CodeKind::TOPLEVEL:
    str += OBJ_MOD_PREFIX;
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

// ###########################
// ##     ClosureObject     ##
// ###########################

ClosureObject::~ClosureObject() {
  for (unsigned int i = 0; i < this->upvarSize; i++) {
    (*this)[this->upvarSize - 1 - i].~DSValue(); // destruct object reverse order
  }
}

// ##########################
// ##     EnvCtxObject     ##
// ##########################

EnvCtxObject::~EnvCtxObject() {
  for (auto iter = this->envs.rbegin(); iter != this->envs.rend(); ++iter) {
    auto &name = iter->first;
    auto &value = iter->second;
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

// ##########################
// ##     ReaderObject     ##
// ##########################

bool ReaderObject::nextLine() {
  if (!this->available) {
    return false;
  }

  std::string line;
  while (true) {
    if (this->remainPos == this->usedSize) {
      const ssize_t readSize = read(this->fdObj->getValue(), this->buf, std::size(this->buf));
      if (readSize == -1 && (errno == EINTR || errno == EAGAIN)) {
        continue;
      }
      if (readSize <= 0) {
        this->available = false;
        break;
      }
      this->remainPos = 0;
      this->usedSize = readSize;
    }

    // split by newline
    StringRef ref(this->buf + this->remainPos, this->usedSize - this->remainPos);
    for (StringRef::size_type pos = 0; pos != StringRef::npos;) {
      auto ret = ref.find('\n', pos);
      line += ref.slice(pos, ret);
      pos = ret;
      if (ret != StringRef::npos) {
        this->value = DSValue::createStr(std::move(line));
        this->remainPos += pos + 1;
        return true;
      } else {
        this->remainPos = this->usedSize;
      }
    }
  }
  if (!line.empty()) {
    this->value = DSValue::createStr(std::move(line));
    return true;
  }
  return false;
}

// #########################
// ##     TimerObject     ##
// #########################

static UserSysTime getTime() {
  struct rusage self;
  getrusage(RUSAGE_SELF, &self);

  struct rusage children;
  getrusage(RUSAGE_CHILDREN, &children);

  struct timeval utime;
  timeradd(&self.ru_utime, &children.ru_utime, &utime);

  struct timeval stime;
  timeradd(&self.ru_stime, &children.ru_stime, &stime);

  return {
      .user = utime,
      .sys = stime,
  };
}

TimerObject::TimerObject() : ObjectWithRtti(TYPE::Any) {
  this->realTime = std::chrono::high_resolution_clock::now();
  this->userSysTime = getTime();
}

static std::string formatTimeval(const struct timeval &time) {
  char buf[64];
  snprintf(buf, std::size(buf), "%lldm%02lld.%03ds", static_cast<long long>(time.tv_sec / 60),
           static_cast<long long>(time.tv_sec % 60), static_cast<int>(time.tv_usec / 1000));
  std::string value = buf;
  return value;
}

using time_point_diff =
    decltype(std::chrono::high_resolution_clock::now() - std::chrono::high_resolution_clock::now());

static std::string formatTimePoint(const time_point_diff &time) {
  char buf[64];
  snprintf(
      buf, std::size(buf), "%ldm%02d.%03ds",
      std::chrono::duration_cast<std::chrono::minutes>(time).count(),
      static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(time).count() % 60),
      static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(time).count() % 1000));
  std::string value = buf;
  return value;
}

TimerObject::~TimerObject() {
  auto curTime = getTime();
  auto real = std::chrono::high_resolution_clock::now();

  // get diff
  auto realDiff = real - this->realTime;
  struct timeval userTimeDiff {};
  timersub(&curTime.user, &this->userSysTime.user, &userTimeDiff);
  struct timeval systemTimeDiff {};
  timersub(&curTime.sys, &this->userSysTime.sys, &systemTimeDiff);

  // show time
  std::string times[] = {
      formatTimePoint(realDiff),
      formatTimeval(userTimeDiff),
      formatTimeval(systemTimeDiff),
  };
  std::size_t maxLen = std::max({times[0].size(), times[1].size(), times[2].size()});
  const char *prefix[] = {"real", "user", "sys "};
  std::string out = "\n";
  for (unsigned int i = 0; i < std::size(prefix); i++) {
    out += prefix[i];
    out += "    ";
    out.append(maxLen - times[i].size(), ' ');
    out += times[i];
    out += '\n';
  }
  fputs(out.c_str(), stderr);
  fflush(stderr);
}

bool formatJobDesc(const StringRef ref, std::string &out) {
  return splitByDelim(ref, '\n', [&out](StringRef sub, bool newline) {
    out += sub;
    if (newline) {
      out += "\\n";
    }
    if (out.size() > SYS_LIMIT_JOB_DESC_LEN) {
      out.resize(SYS_LIMIT_JOB_DESC_LEN - 3);
      out += "...";
      return false;
    }
    return true;
  });
}

} // namespace ydsh
