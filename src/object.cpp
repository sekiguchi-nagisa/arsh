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

#include "arg_parser.h"
#include "candidates.h"
#include "core.h"
#include "line_editor.h"
#include "object_util.h"
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

// ###################
// ##     Value     ##
// ###################

unsigned int Value::getTypeID() const {
  switch (this->kind()) {
  case ValueKind::DUMMY:
    return this->asTypeId();
  case ValueKind::BOOL:
    return toUnderlying(TYPE::Bool);
  case ValueKind::SIG:
    return toUnderlying(TYPE::Signal);
  case ValueKind::SMALL_INT:
    return toUnderlying(TYPE::Int);
  case ValueKind::COMMON_FLOAT:
    return toUnderlying(TYPE::Float);
  case ValueKind::SMALL_STR:
    return toUnderlying(TYPE::String);
  case ValueKind::OBJECT:
    return this->get()->getTypeID();
  default:
    return toUnderlying(TYPE::Void); // normally unreachable
  }
}

StringRef Value::asStrRef() const {
  assert(this->hasStrRef());
  if (this->ss.hasTag(ValueTag::STRING)) {
    return {this->ss.data(), this->ss.size()};
  }
  auto &obj = typeAs<StringObject>(*this);
  return {obj.getValue(), obj.size()};
}

std::string Value::toString(const TypePool &pool) const {
  StrAppender appender(SYS_LIMIT_PRINTABLE_MAX);
  Stringifier stringifier(pool, appender);
  stringifier.addAsStr(*this);
  return std::move(appender).take();
}

bool Value::opStr(ARState &state, Value &out) const {
  StrObjAppender appender(state, out);
  Stringifier stringifier(state.typePool, appender);
  if (!stringifier.addAsStr(*this)) {
    if (stringifier.hasOverflow()) {
      raiseError(state, TYPE::StackOverflowError, "string representation of deep nesting object");
    }
    assert(state.hasError());
    return false;
  }
  return true;
}

bool Value::opInterp(ARState &state, Value &out) const {
  StrObjAppender appender(state, out);
  Stringifier stringifier(state.typePool, appender);
  if (!stringifier.addAsInterp(*this)) {
    if (stringifier.hasOverflow()) {
      raiseError(state, TYPE::StackOverflowError, "string interpolation of deep nesting object");
    }
    assert(state.hasError());
    return false;
  }
  return true;
}

bool Value::equals(ARState &state, const Value &o, bool partial) const {
  Equality equality(partial);
  const bool s = equality(*this, o);
  if (equality.hasOverflow()) {
    raiseError(state, TYPE::StackOverflowError, "equal deep nesting objects");
    return false;
  }
  return s;
}

int Value::compare(ARState &state, const Value &o) const {
  Ordering ordering;
  const int r = ordering(*this, o);
  if (ordering.hasOverflow()) {
    raiseError(state, TYPE::StackOverflowError, "compare deep nesting objects");
    return -1;
  }
  return r;
}

bool Value::appendAsStr(ARState &state, StringRef value) {
  assert(this->hasStrRef());

  const bool small = this->ss.hasTag(ValueTag::STRING);
  const size_t size = small ? this->ss.size() : typeAs<StringObject>(*this).size();
  if (unlikely(size > StringObject::MAX_SIZE - value.size())) {
    raiseStringLimit(state);
    return false;
  }

  if (small) {
    size_t newSize = size + value.size();
    if (newSize <= TValue::MAX_STR_SIZE) {
      this->ss.append<ValueTag::STRING>(value.data(), value.size());
      return true;
    }
    *this = create<StringObject>(StringRef(ss.data(), size));
  }
  typeAs<StringObject>(*this).unsafeAppend(value);
  return true;
}

Value Value::createStr(StringRef ref) {
  if (ref.size() <= TValue::MAX_STR_SIZE) {
    return Value(ref.data(), ref.size());
  }
  return create<StringObject>(ref);
}

Value Value::createStr(std::string &&value) {
  if (value.size() <= TValue::MAX_STR_SIZE) {
    return Value(value.data(), value.size());
  }
  return create<StringObject>(std::move(value));
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

const ObjPtr<UnixFdObject> &UnixFdObject::empty() {
  static ObjPtr<UnixFdObject> empty = createObject<UnixFdObject>(-1);
  return empty;
}

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

bool ArrayObject::insert(ARState &state, size_t index, Value &&value) {
  if (unlikely(!this->checkIteratorInvalidation(state))) {
    return false;
  }
  if (unlikely(this->size() == MAX_SIZE)) {
    raiseError(state, TYPE::OutOfRangeError, "reach Array size limit");
    return false;
  }
  index = std::min(index, this->size());
  this->values.insert(this->values.begin() + index, std::move(value));
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
    case LockType::SORT_BY:
      value += " during sortBy method";
      break;
    case LockType::SEARCH_SORTED_BY:
      value += " during searchSortedBy method";
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

// ##############################
// ##     StringIterObject     ##
// ##############################

StringIterObject::StringIterObject(Value str)
    : ObjectWithRtti(TYPE::StringIter), str(std::move(str)) {
  const auto ref = this->str.asStrRef();
  Utf8GraphemeScanner scanner(Utf8Stream(ref.begin(), ref.end()));

  // save scanner state
  this->prevPos = scanner.getCharBegin() - ref.begin();
  this->curPos = scanner.getStream().iter - ref.begin();
  this->boundary =
      CodePointWithMeta(scanner.getCodePoint(), toUnderlying(scanner.getProperty())).getValue();
}

Value StringIterObject::next() {
  // restore scanner state
  const auto ref = this->str.asStrRef();
  const char *charBegin = ref.begin() + this->prevPos;
  const char *cur = ref.begin() + this->curPos;
  auto vv = CodePointWithMeta::from(this->boundary);
  Utf8GraphemeScanner scanner(Utf8Stream(cur, ref.end()), charBegin, vv.codePoint(),
                              static_cast<GraphemeBoundary::BreakProperty>(vv.getMeta()));
  if (!scanner.hasNext()) {
    return Value::createInvalid();
  }
  const auto ret = Value::createStr(scanner.next());

  // save the current scanner state
  this->prevPos = scanner.getCharBegin() - ref.begin();
  this->curPos = scanner.getStream().iter - ref.begin();
  this->boundary =
      CodePointWithMeta(scanner.getCodePoint(), toUnderlying(scanner.getProperty())).getValue();

  return ret;
}

// ########################
// ##     BaseObject     ##
// ########################

BaseObject::~BaseObject() {
  for (unsigned int i = 0; i < this->fieldSize; i++) {
    (*this)[this->fieldSize - 1 - i].~Value(); // destruct object reverse order
  }
}

// ##########################
// ##     Error_Object     ##
// ##########################

static void printMessage(FILE *fp, const ErrorObject &obj) {
  auto ref = obj.getName().asStrRef();
  fwrite(ref.data(), sizeof(char), ref.size(), fp);
  ref = ": ";
  fwrite(ref.data(), sizeof(char), ref.size(), fp);
  ref = obj.getMessage().asStrRef();
  fwrite(ref.data(), sizeof(char), ref.size(), fp);
  fputc('\n', fp);
}

static void printStackTraceImpl(FILE *fp, const std::vector<StackTraceElement> &stackTrace) {
  for (auto &s : stackTrace) {
    fprintf(fp, "    from %s:%d '%s()'\n", s.getSourceName().c_str(), s.getLineNum(),
            s.getCallerName().c_str());
  }
}

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
  printMessage(stderr, *this); // FIXME: check io error ?
  printStackTraceImpl(stderr, this->stackTrace);

  // show suppressed exceptions
  if (!this->suppressed.empty()) {
    fputs("[note] the following exceptions are suppressed\n", stderr);
    for (auto &e : this->suppressed) {
      printMessage(stderr, *e);
      printStackTraceImpl(stderr, e->getStackTrace());
    }
  }
  if (op == PrintOp::IGNORED) {
    fputs("\n", stderr);
  }
  fflush(stderr);
}

ObjPtr<ErrorObject> ErrorObject::addSuppressed(ObjPtr<ErrorObject> &&except) {
  ObjPtr<ErrorObject> oldest;
  if (reinterpret_cast<uintptr_t>(this) != reinterpret_cast<uintptr_t>(except.get())) {
    if (this->suppressed.size() == SYS_LIMIT_SUPPRESSED_EXCEPT_MAX) {
      oldest = std::move(this->suppressed[0]);
      this->suppressed.erase(this->suppressed.begin());
    }
    this->suppressed.push_back(std::move(except));
  }
  return oldest;
}

ObjPtr<ErrorObject> ErrorObject::newError(const ARState &state, const Type &type, Value &&message,
                                          int64_t status) {
  std::vector<StackTraceElement> traces;
  state.getCallStack().fillStackTrace([&traces](StackTraceElement &&e) {
    traces.push_back(std::move(e));
    return true;
  });
  auto name = Value::createStr(type.getName());
  return createObject<ErrorObject>(type, std::move(message), std::move(name), status,
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
