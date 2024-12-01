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

#include "../external/dragonbox/simple_dragonbox.h"
#include "arg_parser.h"
#include "candidates.h"
#include "core.h"
#include "line_editor.h"
#include "misc/num_util.hpp"
#include "ordered_map.h"
#include "pager.h"
#include "redir.h"
#include "vm.h"

namespace arsh {

void Object::destroy() {
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
  constexpr const char *table[] = {
#define GEN_STR(E) #E,
      EACH_OBJECT_KIND(GEN_STR)
#undef GEN_STR
  };
  return table[toUnderlying(kind)];
}

// #####################
// ##     DSValue     ##
// #####################

unsigned int Value::getTypeID() const {
  switch (this->kind()) {
  case ValueKind::DUMMY:
    return this->asTypeId();
  case ValueKind::BOOL:
    return toUnderlying(TYPE::Bool);
  case ValueKind::SIG:
    return toUnderlying(TYPE::Signal);
  case ValueKind::INT:
    return toUnderlying(TYPE::Int);
  case ValueKind::FLOAT:
    return toUnderlying(TYPE::Float);
  default:
    if (isSmallStr(this->kind())) {
      return toUnderlying(TYPE::String);
    }
    assert(this->kind() == ValueKind::OBJECT);
    return this->get()->getTypeID();
  }
}

StringRef Value::asStrRef() const {
  assert(this->hasStrRef());
  if (isSmallStr(this->kind())) {
    return {this->str.value, smallStrSize(this->kind())};
  }
  auto &obj = typeAs<StringObject>(*this);
  return {obj.getValue(), obj.size()};
}

Value Value::withMetaData(uint32_t metaData) const {
  assert(this->kind() != ValueKind::EXPAND_META && this->kind() != ValueKind::NUM_LIST &&
         this->kind() != ValueKind::STACK_GUARD && this->kind() != ValueKind::DUMMY);

  Value newValue = *this;
  if (isSmallStr(newValue.kind())) {
    StringRef ref{newValue.str.value, smallStrSize(newValue.kind())};
    if (newValue.kind() <= ValueKind::SSTR10) {
      const union {
        char i8[4];
        uint32_t u32;
      } conv = {
          .u32 = metaData,
      };
      memcpy(newValue.str.value + 11, conv.i8, 4);
      return newValue;
    } else {
      newValue = create<StringObject>(ref);
    }
  }
  newValue.value.meta = metaData;
  return newValue;
}

uint32_t Value::getMetaData() const {
  assert(this->kind() != ValueKind::EXPAND_META && this->kind() != ValueKind::NUM_LIST &&
         this->kind() != ValueKind::STACK_GUARD && this->kind() != ValueKind::DUMMY);

  if (isSmallStr(this->kind())) {
    assert(smallStrSize(this->kind()) <= 10);
    union { // NOLINT
      char i8[4];
      uint32_t u32;
    } conv = {};
    memcpy(conv.i8, this->str.value + 11, 4);
    return conv.u32;
  }
  return this->value.meta;
}

static std::string toString(double value) {
  if (std::isnan(value)) {
    return "NaN";
  }
  if (std::isinf(value)) {
    return value > 0 ? "Infinity" : "-Infinity";
  }
  if (value == 0.0) {
    return std::signbit(value) ? "-0.0" : "0.0";
  }

  auto [significand, exponent, sign] = jkj::simple_dragonbox::to_decimal(
      value, jkj::simple_dragonbox::policy::cache::compact,
      jkj::simple_dragonbox::policy::binary_to_decimal_rounding::to_even);
  return Decimal{.significand = significand, .exponent = exponent, .sign = sign}.toString();
}

std::string Value::toString() const {
  switch (this->kind()) {
  case ValueKind::NUMBER:
    return std::to_string(this->asNum());
  case ValueKind::NUM_LIST: {
    auto &nums = this->asNumList();
    char buf[256];
    snprintf(buf, std::size(buf), "[%u, %u, %u]", nums[0], nums[1], nums[2]);
    return {buf};
  }
  case ValueKind::DUMMY: {
    unsigned int typeId = this->asTypeId();
    if (typeId == toUnderlying(TYPE::Module)) {
      std::string str = OBJ_TEMP_MOD_PREFIX; // for temporary module descriptor
      str += std::to_string(this->asNumList()[1]);
      str += ")";
      return str;
    } else {
      std::string str("Object(");
      str += std::to_string(typeId);
      str += ")";
      return str;
    }
  }
  case ValueKind::EXPAND_META:
    return ::toString(this->asExpandMeta().first);
  case ValueKind::BOOL:
    return this->asBool() ? "true" : "false";
  case ValueKind::SIG:
    return std::to_string(this->asSig());
  case ValueKind::INT:
    return std::to_string(this->asInt());
  case ValueKind::FLOAT: {
    double d = this->asFloat();
    return ::toString(d);
  }
  default:
    if (this->hasStrRef()) {
      return this->asStrRef().toString();
    }
    assert(this->kind() == ValueKind::OBJECT);
    break;
  }

  switch (this->get()->getKind()) {
  case ObjectKind::UnixFd: {
    std::string str = "/dev/fd/";
    str += std::to_string(typeAs<UnixFdObject>(*this).getRawFd());
    return str;
  }
  case ObjectKind::Regex:
    return typeAs<RegexObject>(*this).getStr();
  case ObjectKind::Array: {
    auto &values = typeAs<ArrayObject>(*this).getValues();
    std::string str = "[";
    unsigned int size = values.size();
    for (unsigned int i = 0; i < size; i++) {
      if (i > 0) {
        str += ", ";
      }
      str += values[i].toString();
    }
    str += "]";
    return str;
  }
  case ObjectKind::OrderedMap: {
    auto &entries = typeAs<OrderedMapObject>(*this).getEntries();
    std::string ret = "[";
    unsigned int count = 0;
    for (auto &e : entries) {
      if (!e) {
        continue;
      }
      if (count++ > 0) {
        ret += ", ";
      }
      ret += e.getKey().toString();
      ret += " : ";
      ret += e.getValue().toString();
    }
    ret += "]";
    return ret;
  }
  case ObjectKind::Base: { // for tuple
    auto &obj = typeAs<BaseObject>(*this);
    unsigned int fieldSize = obj.getFieldSize();
    std::string value = "(";
    for (unsigned int i = 0; i < fieldSize; i++) {
      if (i > 0) {
        value += ", ";
      }
      value += obj[i].toString();
    }
    value += ")";
    return value;
  }
  case ObjectKind::Func: {
    auto &obj = typeAs<FuncObject>(*this);
    std::string str;
    const auto kind = obj.getCode().getKind();
    switch (kind) {
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
    const auto modId = obj.getCode().getBelongedModId();
    if (const char *name = obj.getCode().getName(); name[0]) {
      if (!isBuiltinMod(modId) &&
          (kind == CodeKind::FUNCTION || kind == CodeKind::USER_DEFINED_CMD)) {
        str += toModTypeName(modId);
        str += '.';
      }
      str += name;
    } else {
      char buf[32]; // hex of 64bit pointer is up to 16 chars
      snprintf(buf, std::size(buf), "0x%zx", reinterpret_cast<uintptr_t>(&obj));
      str += buf;
    }
    str += ")";
    return str;
  }
  case ObjectKind::Closure: {
    char buf[32]; // hex of 64bit pointer is up to 16 chars
    snprintf(buf, std::size(buf), "closure(0x%zx)",
             reinterpret_cast<uintptr_t>(&typeAs<ClosureObject>(*this).getFuncObj()));
    return {buf};
  }
  case ObjectKind::Job: {
    std::string str = "%";
    str += std::to_string(typeAs<JobObject>(*this).getJobID());
    return str;
  }
  default:
    std::string str("Object(");
    str += std::to_string(reinterpret_cast<uintptr_t>(this->get()));
    str += ")";
    return str;
  }
}

#define GUARD_RECURSION(state)                                                                     \
  RecursionGuard _guard(state);                                                                    \
  do {                                                                                             \
    if (unlikely(!_guard.checkLimit())) {                                                          \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (unlikely(!(E))) {                                                                          \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

bool Value::opStr(StrBuilder &builder) const {
  GUARD_RECURSION(builder.getState());

  if (this->isInvalid()) {
    return builder.add("(invalid)");
  }
  if (this->isObject()) {
    switch (this->get()->getKind()) {
    case ObjectKind::RegexMatch:
    case ObjectKind::Array: {
      const auto &values = isa<ArrayObject>(this->get())
                               ? typeAs<ArrayObject>(*this).getValues()
                               : typeAs<RegexMatchObject>(*this).getGroups();
      TRY(builder.add("["));
      const unsigned int size = values.size();
      for (unsigned int i = 0; i < size; i++) {
        if (i > 0) {
          TRY(builder.add(", "));
        }
        TRY(values[i].opStr(builder));
      }
      return builder.add("]");
    }
    case ObjectKind::OrderedMap: {
      auto &entries = typeAs<OrderedMapObject>(*this).getEntries();
      TRY(builder.add("["));
      unsigned int count = 0;
      for (auto &e : entries) {
        if (!e) {
          continue;
        }
        if (count++ > 0) {
          TRY(builder.add(", "));
        }
        TRY(e.getKey().opStr(builder));
        TRY(builder.add(" : "));
        TRY(e.getValue().opStr(builder));
      }
      return builder.add("]");
    }
    case ObjectKind::Base: {
      auto &obj = typeAs<BaseObject>(*this);
      auto &type = builder.getState().typePool.get(this->getTypeID());
      if (type.isTupleType()) {
        TRY(builder.add("("));
        unsigned int size = obj.getFieldSize();
        for (unsigned int i = 0; i < size; i++) {
          if (i > 0) {
            TRY(builder.add(", "));
          }
          TRY(obj[i].opStr(builder));
        }
        if (size == 1) {
          TRY(builder.add(","));
        }
        return builder.add(")");
      } else {
        assert(type.isRecordOrDerived());
        auto &recordType = cast<RecordType>(type);
        TRY(builder.add("{"));
        unsigned int size = obj.getFieldSize();
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
          TRY(obj[i].opStr(builder));
        }
        return builder.add("}");
      }
    }
    case ObjectKind::Error: {
      auto &obj = typeAs<ErrorObject>(*this);
      auto ref = obj.getMessage().asStrRef();
      return builder.add(builder.getState().typePool.get(this->getTypeID()).getNameRef()) &&
             builder.add(": ") && builder.add(ref);
    }
    case ObjectKind::Candidate: {
      auto &obj = typeAs<CandidateObject>(*this);
      return builder.add(obj.underlying());
    }
    default:
      break;
    }
  }
  return builder.add(this->toString());
}

bool Value::opInterp(StrBuilder &builder) const {
  GUARD_RECURSION(builder.getState());

  if (this->isObject()) {
    switch (this->get()->getKind()) {
    case ObjectKind::RegexMatch:
    case ObjectKind::Array: {
      unsigned int count = 0;
      const auto &values = isa<ArrayObject>(this->get())
                               ? typeAs<ArrayObject>(*this).getValues()
                               : typeAs<RegexMatchObject>(*this).getGroups();
      for (auto &e : values) {
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
    case ObjectKind::OrderedMap: {
      unsigned int count = 0;
      for (auto &e : typeAs<OrderedMapObject>(*this).getEntries()) {
        if (!e || e.getValue().isInvalid()) {
          continue;
        }
        if (count++ > 0) {
          TRY(builder.add(" "));
        }
        TRY(e.getKey().opInterp(builder) && builder.add(" ") && e.getValue().opInterp(builder));
      }
      return true;
    }
    case ObjectKind::Base: {
      auto &obj = typeAs<BaseObject>(*this);
      assert(builder.getState().typePool.get(this->getTypeID()).isTupleType() ||
             builder.getState().typePool.get(this->getTypeID()).isRecordOrDerived());
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

bool Value::equals(const Value &o) const {
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
  case ValueKind::EMPTY:
    return true;
  case ValueKind::BOOL:
    return this->asBool() == o.asBool();
  case ValueKind::SIG:
    return this->asSig() == o.asSig();
  case ValueKind::INT:
    return this->asInt() == o.asInt();
  case ValueKind::FLOAT:
    return compareByTotalOrder(this->asFloat(), o.asFloat()) == 0;
  default:
    assert(this->kind() == ValueKind::OBJECT);
    if (this->get()->getKind() != o.get()->getKind()) {
      return false;
    }
    return reinterpret_cast<uintptr_t>(this->get()) == reinterpret_cast<uintptr_t>(o.get());
  }
}

int Value::compare(const Value &o) const {
  // for String
  if (this->hasStrRef() && o.hasStrRef()) {
    auto left = this->asStrRef();
    auto right = o.asStrRef();
    return left.compare(right);
  }

  assert(this->kind() == o.kind());
  switch (this->kind()) {
  case ValueKind::BOOL: {
    int left = this->asBool() ? 1 : 0;
    int right = o.asBool() ? 1 : 0;
    return left - right;
  }
  case ValueKind::SIG:
    return this->asSig() - o.asSig();
  case ValueKind::INT: {
    int64_t left = this->asInt();
    int64_t right = o.asInt();
    if (left == right) {
      return 0;
    }
    return left < right ? -1 : 1;
  }
  case ValueKind::FLOAT:
    return compareByTotalOrder(this->asFloat(), o.asFloat());
  default:
    return 1; // normally unreachable
  }
}

bool Value::appendAsStr(ARState &state, StringRef value) {
  assert(this->hasStrRef());

  const bool small = isSmallStr(this->kind());
  const size_t size = small ? smallStrSize(this->kind()) : typeAs<StringObject>(*this).size();
  if (unlikely(size > StringObject::MAX_SIZE - value.size())) {
    raiseStringLimit(state);
    return false;
  }

  if (small) {
    size_t newSize = size + value.size();
    if (newSize <= smallStrSize(ValueKind::SSTR14)) {
      memcpy(this->str.value + size, value.data(), value.size());
      this->str.kind = toSmallStrKind(newSize);
      this->str.value[newSize] = '\0';
      return true;
    }
    (*this) = Value::create<StringObject>(StringRef(this->str.value, size));
  }
  typeAs<StringObject>(*this).unsafeAppend(value);
  return true;
}

Value Value::createStr(const GraphemeCluster &ret) {
  if (ret.hasInvalid()) {
    return Value::createStr(UnicodeUtil::REPLACEMENT_CHAR_UTF8);
  } else {
    return Value::createStr(ret.getRef());
  }
}

// ###########################
// ##     UnixFD_Object     ##
// ###########################

UnixFdObject *UnixFdObject::create(int fd, ObjPtr<JobObject> &&job) {
  void *ptr = operator new(sizeof(UnixFdObject) + sizeof(RawValue));
  auto *obj = new (ptr) UnixFdObject(fd, true);
  new (&obj->data[0]) Value(job);
  return obj;
}

UnixFdObject::~UnixFdObject() {
  if (this->fd > STDERR_FILENO) {
    close(this->fd); // do not close standard io file descriptor
  }
  if (this->hasJob) {
    static_cast<Value &>(this->data[0]).~Value();
  }
}

ObjPtr<UnixFdObject> UnixFdObject::dupWithCloseOnExec() const {
  const int newFd = dupFDCloseOnExec(this->fd);
  if (newFd < 0) {
    return nullptr;
  }
  return ObjPtr<UnixFdObject>(create(newFd));
}

// #########################
// ##     RegexObject     ##
// #########################

bool RegexObject::match(ARState &state, const StringRef ref, MatchResult *ret) {
  assert(ref.size() <= StringObject::MAX_SIZE);
  std::string errorStr;
  const int matchCount = this->re.match(ref, errorStr);
  if (!errorStr.empty()) {
    raiseError(state, TYPE::RegexMatchError, std::move(errorStr));
  }
  if (ret && matchCount > 0) {
    ret->groups.reserve(matchCount); // not check iterator invalidation
    for (int i = 0; i < matchCount; i++) {
      PCRECapture capture; // NOLINT
      const bool set = this->re.getCaptureAt(i, capture);
      if (i == 0) {
        ret->start = capture.begin;
        ret->end = capture.end;
      }
      auto v =
          set ? Value::createStr(ref.slice(capture.begin, capture.end)) : Value::createInvalid();
      ret->groups.push_back(std::move(v)); // not check size
    }
    ASSERT_ARRAY_SIZE(ret->groups);
  }
  return matchCount > 0;
}

// ##########################
// ##     Array_Object     ##
// ##########################

bool ArrayObject::append(ARState &state, Value &&obj) {
  if (unlikely(!this->checkIteratorInvalidation(state))) {
    return false;
  }
  if (unlikely(this->size() == MAX_SIZE)) {
    raiseError(state, TYPE::OutOfRangeError, "reach Array size limit");
    return false;
  }
  this->values.push_back(std::move(obj));
  return true;
}

bool ArrayObject::checkIteratorInvalidation(ARState &state, const char *message) const {
  if (this->locking()) {
    std::string value = "cannot modify array object";
    StringRef ref = message;
    if (!ref.empty()) {
      value += " (";
      value += ref;
      value += ")";
    }
    switch (this->lockType) {
    case LockType::NONE:
      break; // unreachable
    case LockType::ITER:
      value += " during iteration";
      break;
    case LockType::SORT_WITH:
      value += " during sortWith method";
      break;
    case LockType::HISTORY:
      value += " during line editing";
      break;
    }
    raiseError(state, TYPE::InvalidOperationError, std::move(value));
    return false;
  }
  return true;
}

// ########################
// ##     BaseObject     ##
// ########################

BaseObject::~BaseObject() {
  for (unsigned int i = 0; i < this->fieldSize; i++) {
    (*this)[this->fieldSize - 1 - i].~Value(); // destruct object reverse order
  }
}

bool CmdArgsBuilder::add(Value &&arg) {
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
        this->redir = Value::create<RedirObject>();
      } else if (this->redir.isInvalid()) {
        raiseError(this->state, TYPE::ArgumentError, "cannot pass FD object to argument array");
        return false;
      }

      if (arg.get()->getRefcount() > 1) {
        if (auto ret = typeAs<UnixFdObject>(arg).dupWithCloseOnExec()) {
          arg = ret;
        } else {
          raiseSystemError(this->state, errno, "failed to pass FD object to command arguments");
          return false;
        }
      }
      auto strObj = Value::createStr(arg.toString());
      bool r = this->argv->append(this->state, std::move(strObj));
      if (r) {
        assert(arg.get()->getRefcount() == 1);
        if (!typeAs<UnixFdObject>(arg).closeOnExec(false)) {
          raiseSystemError(this->state, errno, "failed to pass FD object to command arguments");
          return false;
        }
        typeAs<RedirObject>(this->redir).addEntry(std::move(arg), RedirOp::NOP, -1);
      }
      return r;
    }
    case ObjectKind::RegexMatch:
    case ObjectKind::Array: {
      auto &values = isa<ArrayObject>(arg.get()) ? typeAs<ArrayObject>(arg).getValues()
                                                 : typeAs<RegexMatchObject>(arg).getGroups();
      for (auto &e : values) {
        TRY(this->add(Value(e)));
      }
      return true;
    }
    case ObjectKind::OrderedMap:
      for (auto &e : typeAs<OrderedMapObject>(arg).getEntries()) {
        if (!e || e.getValue().isInvalid()) {
          continue;
        }
        TRY(this->add(Value(e.getKey())) && this->add(Value(e.getValue())));
      }
      return true;
    case ObjectKind::Base: {
      auto &obj = typeAs<BaseObject>(arg);
      unsigned int size = obj.getFieldSize();
      for (unsigned int i = 0; i < size; i++) {
        TRY(this->add(Value(obj[i])));
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

void ErrorObject::printStackTrace(const ARState &state, PrintOp op) const {
  // print header
  const auto level = state.subshellLevel();
  switch (op) {
  case PrintOp::DEFAULT:
    break;
  case PrintOp::UNCAUGHT: {
    std::string header = "[runtime error";
    if (level) {
      header += " at subshell=";
      header += std::to_string(level);
    }
    header += "]\n";
    fputs(header.c_str(), stderr);
    break;
  }
  case PrintOp::IGNORED:
  case PrintOp::IGNORED_TERM: {
    std::string header = "[warning";
    if (level) {
      header += " at subshell=";
      header += std::to_string(level);
    }
    header += "]\n";
    if (op == PrintOp::IGNORED) {
      header += "the following exception within finally/defer block is ignored\n";
    } else {
      header += "the following exception within termination handler is ignored\n";
    }
    fputs(header.c_str(), stderr);
    break;
  }
  }

  // print message (suppress error)
  {
    auto ref = state.typePool.get(this->getTypeID()).getNameRef();
    fwrite(ref.data(), sizeof(char), ref.size(), stderr);
    ref = ": ";
    fwrite(ref.data(), sizeof(char), ref.size(), stderr);
    ref = this->message.asStrRef();
    fwrite(ref.data(), sizeof(char), ref.size(), stderr);
    fputc('\n', stderr);
  }

  // print stack trace
  for (auto &s : this->stackTrace) {
    fprintf(stderr, "    from %s:%d '%s()'\n", s.getSourceName().c_str(), s.getLineNum(),
            s.getCallerName().c_str());
  }
  if (op == PrintOp::IGNORED) {
    fputs("\n", stderr);
  }
  fflush(stderr);
}

ObjPtr<ErrorObject> ErrorObject::newError(const ARState &state, const Type &type, Value &&message,
                                          int64_t status) {
  std::vector<StackTraceElement> traces;
  state.getCallStack().fillStackTrace([&traces](StackTraceElement &&e) {
    traces.push_back(std::move(e));
    return true;
  });
  auto name = Value::createStr(type.getName());
  return toObjPtr<ErrorObject>(Value::create<ErrorObject>(type, std::move(message), std::move(name),
                                                          status, std::move(traces)));
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

// ###########################
// ##     ClosureObject     ##
// ###########################

ClosureObject::~ClosureObject() {
  for (unsigned int i = 0; i < this->upvarSize; i++) {
    (*this)[this->upvarSize - 1 - i].~Value(); // destruct object reverse order
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
      // if invalid, pass null (unset)
      setEnv(this->state.pathCache, envName, value.isInvalid() ? nullptr : value.asCStr());
    }
  }
}

void EnvCtxObject::setAndSaveEnv(Value &&name, Value &&value) {
  assert(!name.asStrRef().hasNullChar());

  const char *envName = name.asCStr();
  const char *oldEnv = getenv(envName);

  // save old env
  this->envs.emplace_back(name, oldEnv ? Value::createStr(oldEnv) : Value::createInvalid());

  // overwrite env
  setEnv(this->state.pathCache, envName, value.asCStr());

  if (name.asStrRef() == VAR_IFS) { // if env name is IFS, also save and set IFS global variable
    // save old IFS
    auto ifsIndex = Value::createInt(toIndex(BuiltinVarOffset::IFS));
    this->envs.emplace_back(ifsIndex, this->state.getGlobal(BuiltinVarOffset::IFS));

    // overwrite IFS
    this->state.setGlobal(BuiltinVarOffset::IFS, std::move(value));
  }
}

// ##########################
// ##     ReaderObject     ##
// ##########################

bool ReaderObject::nextLine(ARState &state) {
  if (!this->available) {
    return false;
  }

  std::string line;
  while (true) {
    if (this->remainPos == this->usedSize) {
      const ssize_t readSize = read(this->fdObj->getRawFd(), this->buf, std::size(this->buf));
      if (readSize == -1 && errno == EAGAIN) {
        continue;
      }
      if (readSize <= 0) {
        this->available = false;
        if (readSize < 0) {
          raiseSystemError(state, errno, "read failed");
          return false;
        }
        break;
      }
      this->remainPos = 0;
      this->usedSize = readSize;
    }

    // split by newline
    StringRef ref(this->buf + this->remainPos, this->usedSize - this->remainPos);
    for (StringRef::size_type pos = 0; pos != StringRef::npos;) {
      const auto ret = ref.find('\n', pos);
      if (!checkedAppend(ref.slice(pos, ret), StringObject::MAX_SIZE, line)) {
        raiseStringLimit(state);
        return false;
      }
      pos = ret;
      if (ret != StringRef::npos) {
        this->value = Value::createStr(std::move(line));
        this->remainPos += pos + 1;
        return true;
      } else {
        this->remainPos = this->usedSize;
      }
    }
  }
  if (!line.empty()) {
    this->value = Value::createStr(std::move(line));
    return true;
  }
  return false;
}

// #########################
// ##     TimerObject     ##
// #########################

static UserSysTime getTime() {
  struct rusage self; // NOLINT
  getrusage(RUSAGE_SELF, &self);

  struct rusage children; // NOLINT
  getrusage(RUSAGE_CHILDREN, &children);

  struct timeval utime; // NOLINT
  timeradd(&self.ru_utime, &children.ru_utime, &utime);

  struct timeval stime; // NOLINT
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
  struct timeval userTimeDiff{};
  timersub(&curTime.user, &this->userSysTime.user, &userTimeDiff);
  struct timeval systemTimeDiff{};
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

} // namespace arsh
