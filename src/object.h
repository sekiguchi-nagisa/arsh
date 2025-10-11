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

#ifndef ARSH_OBJECT_H
#define ARSH_OBJECT_H

#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <memory>

#include "constant.h"
#include "misc/files.hpp"
#include "misc/rtti.hpp"
#include "misc/string_ref.hpp"
#include "object_util.h"
#include "regex_wrapper.h"
#include "type.h"
#include <config.h>

struct ARState;

namespace arsh {

#define EACH_OBJECT_KIND(OP)                                                                       \
  OP(String)                                                                                       \
  OP(StringIter)                                                                                   \
  OP(UnixFd)                                                                                       \
  OP(Regex)                                                                                        \
  OP(RegexMatch)                                                                                   \
  OP(Array)                                                                                        \
  OP(ArrayIter)                                                                                    \
  OP(OrderedMap)                                                                                   \
  OP(OrderedMapIter)                                                                               \
  OP(Base)                                                                                         \
  OP(Error)                                                                                        \
  OP(Func)                                                                                         \
  OP(Closure)                                                                                      \
  OP(Box)                                                                                          \
  OP(EnvCtx)                                                                                       \
  OP(Reader)                                                                                       \
  OP(Timer)                                                                                        \
  OP(Job)                                                                                          \
  OP(Pipeline)                                                                                     \
  OP(Redir)                                                                                        \
  OP(LineEditor)                                                                                   \
  OP(Candidate)

/**
 * for LLVM-style RTTI
 * see. https://llvm.org/docs/HowToSetUpLLVMStyleRTTI.html
 */
enum class ObjectKind : unsigned char {
#define GEN_ENUM(K) K,
  EACH_OBJECT_KIND(GEN_ENUM)
#undef GEN_ENUM
};

class Value;

struct ObjectRefCount;

class Object {
protected:
  int refCount{0};

  /**
   * |  24bit  |  8bit       |
   *   TypeID    ObjectKind
   */
  const unsigned int tag{0};

  friend class Value;

  friend struct ObjectRefCount;

  NON_COPYABLE(Object);

  Object(ObjectKind kind, unsigned int typeID)
      : tag(typeID << 8 | static_cast<unsigned char>(kind)) {}

  ~Object() = default;

public:
  int getRefcount() const { return this->refCount; }

  unsigned int getTypeID() const { return this->tag >> 8; }

  ObjectKind getKind() const { return static_cast<ObjectKind>(this->tag & 0xFF); }

private:
  void destroy();
};

const char *toString(ObjectKind kind);

template <ObjectKind K>
struct ObjectWithRtti : public Object {
protected:
  static_assert(sizeof(Object) == 8);

  explicit ObjectWithRtti(const Type &type) : ObjectWithRtti(type.typeId()) {}

  explicit ObjectWithRtti(TYPE type) : ObjectWithRtti(toUnderlying(type)) {}

  explicit ObjectWithRtti(unsigned int id) : Object(K, id) {}

public:
  static constexpr auto KIND = K;

  static bool classof(const Object *obj) { return obj->getKind() == K; }
};

struct ObjectRefCount {
  static long useCount(const Object *ptr) noexcept { return ptr->refCount; }

  static void increase(Object *ptr) noexcept {
    if (ptr != nullptr) {
      ptr->refCount++;
    }
  }

  static void decrease(Object *ptr) noexcept {
    if (ptr != nullptr && --ptr->refCount == 0) {
      ptr->destroy();
    }
  }
};

template <typename T>
using ObjPtr = IntrusivePtr<T, ObjectRefCount>;

class StringObject : public ObjectWithRtti<ObjectKind::String> {
private:
  std::string value;

public:
  static constexpr size_t MAX_SIZE = SYS_LIMIT_STRING_MAX;
  static_assert(MAX_SIZE <= SIZE_MAX);

  explicit StringObject(std::string &&value)
      : ObjectWithRtti(TYPE::String), value(std::move(value)) {
    assert(this->value.size() <= MAX_SIZE);
  }

  explicit StringObject(StringRef ref) : StringObject(ref.toString()) {}

  const char *getValue() const { return this->value.c_str(); }

  unsigned int size() const { return this->value.size(); }

  /**
   * unsafe api. not directly use it
   * @param v
   */
  void unsafeAppend(StringRef v) { this->value += v; }
};

enum class StackGuardType : unsigned char {
  LOOP,
  TRY,
};

enum class ValueKind : unsigned char {
  EMPTY,
  OBJECT,
  // not null
  NUMBER,
  // uint64_t
  NUM_LIST,
  // [uint32_t uint32_t, uint32_t]
  STACK_GUARD,
  // [uint32_t uint32_t, uint32_t]
  DUMMY,
  // DSType(uint32_t), uint32_t, uint32_t
  EXPAND_META,
  // [uint32_t, uint32_t], for glob meta character, '?', '*', '{', ',', '}'
  INVALID,
  BOOL,
  SIG,
  // int64_t
  INT,
  // int64_t
  FLOAT,
  // double

  // for small string (up to 14 characters)
  SSTR0,
  SSTR1,
  SSTR2,
  SSTR3,
  SSTR4,
  SSTR5,
  SSTR6,
  SSTR7,
  SSTR8,
  SSTR9,
  SSTR10,
  SSTR11,
  SSTR12,
  SSTR13,
  SSTR14,
};

inline bool isSmallStr(ValueKind kind) {
  switch (kind) {
  case ValueKind::SSTR0:
  case ValueKind::SSTR1:
  case ValueKind::SSTR2:
  case ValueKind::SSTR3:
  case ValueKind::SSTR4:
  case ValueKind::SSTR5:
  case ValueKind::SSTR6:
  case ValueKind::SSTR7:
  case ValueKind::SSTR8:
  case ValueKind::SSTR9:
  case ValueKind::SSTR10:
  case ValueKind::SSTR11:
  case ValueKind::SSTR12:
  case ValueKind::SSTR13:
  case ValueKind::SSTR14:
    return true;
  default:
    return false;
  }
}

class RawValue {
protected:
  union {
    struct {
      ValueKind kind;
      uint32_t meta; // for future usage
      union {
        Object *obj; // not null
        uint64_t u64;
        int64_t i64;
        double f64;
        bool b;
        const Type *type; // not null
      };
    } value;

    struct {
      ValueKind kind;
      char value[15]; // null terminated
    } str;

    struct {
      ValueKind kind;
      uint32_t values[3];
    } u32s;
  };

public:
  void swap(RawValue &o) noexcept { std::swap(*this, o); }

  ValueKind kind() const { return this->value.kind; }

  static unsigned int smallStrSize(ValueKind kind) {
    assert(isSmallStr(kind));
    return static_cast<unsigned int>(kind) - static_cast<unsigned int>(ValueKind::SSTR0);
  }

  static ValueKind toSmallStrKind(unsigned int size) {
    assert(size <= smallStrSize(ValueKind::SSTR14));
    auto base = static_cast<unsigned int>(ValueKind::SSTR0);
    return static_cast<ValueKind>(base + size);
  }
};

template <typename T, typename... Arg>
struct ObjectConstructor {
  static Object *construct(Arg &&...arg) { return new T(std::forward<Arg>(arg)...); }
};

class GraphemeCluster;

class Value : public RawValue {
private:
  static_assert(sizeof(RawValue) == 16);

  explicit Value(uint64_t value) noexcept {
    this->value.kind = ValueKind::NUMBER;
    this->value.u64 = value;
  }

  explicit Value(int64_t value) noexcept {
    this->value.kind = ValueKind::INT;
    this->value.i64 = value;
  }

  explicit Value(bool value) noexcept {
    this->value.kind = ValueKind::BOOL;
    this->value.b = value;
  }

  explicit Value(double value) noexcept {
    this->value.kind = ValueKind::FLOAT;
    this->value.f64 = value;
  }

  /**
   * for small string construction
   */
  Value(const char *data, unsigned int size) noexcept {
    assert(data || size == 0);
    assert(size <= smallStrSize(ValueKind::SSTR14));
    this->str.kind = toSmallStrKind(size);
    if (data) {
      memcpy(this->str.value, data, size);
    }
    this->str.value[size] = '\0';
  }

public:
  explicit Value(Object *o) noexcept {
    assert(o);
    this->value.kind = ValueKind::OBJECT;
    this->value.obj = o;
    this->value.obj->refCount++;
  }

  /**
   * equivalent to Value(nullptr)
   */
  Value() noexcept { this->value.kind = ValueKind::EMPTY; }

  Value(std::nullptr_t) noexcept : Value() {} // NOLINT

  Value(const Value &value) noexcept : RawValue(value) {
    if (this->isObject()) {
      this->value.obj->refCount++;
    }
  }

  /**
   * not increment refCount
   */
  Value(Value &&value) noexcept : RawValue(value) { value.value.kind = ValueKind::EMPTY; }

  template <typename T, enable_when<std::is_base_of_v<Object, T>> = nullptr>
  Value(const ObjPtr<T> &o) noexcept : Value(Value(o.get())) {} // NOLINT

  ~Value() {
    if (this->isObject()) {
      if (--this->value.obj->refCount == 0) {
        this->value.obj->destroy();
      }
    }
  }

  Value &operator=(const Value &value) noexcept {
    if (this != std::addressof(value)) {
      this->~Value();
      new (this) Value(value);
    }
    return *this;
  }

  Value &operator=(Value &&value) noexcept {
    if (this != std::addressof(value)) {
      this->~Value();
      new (this) Value(std::move(value));
    }
    return *this;
  }

  /**
   * release current pointer.
   */
  void reset() noexcept {
    this->~Value();
    this->value.kind = ValueKind::EMPTY;
  }

  Object *get() const noexcept {
    assert(this->kind() == ValueKind::OBJECT);
    return this->value.obj;
  }

  ObjPtr<Object> toPtr() const { return ObjPtr<Object>(this->get()); }

  explicit operator bool() const noexcept { return this->kind() != ValueKind::EMPTY; }

  /**
   * if represents Object, return true.
   */
  bool isObject() const noexcept { return this->kind() == ValueKind::OBJECT; }

  bool isInvalid() const noexcept { return this->kind() == ValueKind::INVALID; }

  unsigned int getTypeID() const;

  bool hasType(TYPE t) const { return hasType(static_cast<unsigned int>(t)); }

  bool hasType(unsigned int id) const { return this->getTypeID() == id; }

  bool hasStrRef() const {
    return isSmallStr(this->kind()) ||
           (this->isObject() && this->get()->getKind() == ObjectKind::String);
  }

  unsigned int asNum() const {
    assert(this->kind() == ValueKind::NUMBER);
    return this->value.u64;
  }

  using uint32_3 = const uint32_t (&)[3];
  uint32_3 asNumList() const { return this->u32s.values; }

  std::pair<StackGuardType, unsigned int> asStackGuard() const {
    assert(this->kind() == ValueKind::STACK_GUARD);
    return {static_cast<StackGuardType>(this->u32s.values[0]), this->u32s.values[1]};
  }

  unsigned int asTypeId() const {
    assert(this->kind() == ValueKind::DUMMY);
    return this->u32s.values[0];
  }

  std::pair<ExpandMeta, unsigned int> asExpandMeta() const {
    assert(this->kind() == ValueKind::EXPAND_META);
    return {static_cast<ExpandMeta>(this->u32s.values[0]), this->u32s.values[1]};
  }

  bool asBool() const {
    assert(this->kind() == ValueKind::BOOL);
    return this->value.b;
  }

  int asSig() const {
    assert(this->kind() == ValueKind::SIG);
    return static_cast<int>(this->value.i64);
  }

  int64_t asInt() const {
    assert(this->kind() == ValueKind::INT);
    return this->value.i64;
  }

  double asFloat() const {
    assert(this->kind() == ValueKind::FLOAT);
    return this->value.f64;
  }

  StringRef asStrRef() const;

  const char *asCStr() const { return this->asStrRef().data(); }

  /**
   * create new value with meta-data
   * @param metaData
   * @return
   */
  Value withMetaData(uint32_t metaData) const;

  /**
   * get metadata.
   * only called for value from withMetaData()
   * @return
   */
  uint32_t getMetaData() const;

  /**
   * get string representation (limited length )
   * @param pool
   * @return
   */
  std::string toString(const TypePool &pool) const;

  /**
   * OP_STR method implementation (may have error)
   * @param state
   * @param out
   * must be string
   * @return
   * if it has error, return false
   */
  bool opStr(ARState &state, Value &out) const;

  /**
   * OP_INTERP method implementation (may have error)
   * @param state
   * @param out must be string
   * @return if it has error, return false
   */
  bool opInterp(ARState &state, Value &out) const;

  /**
   * for equality. may raise error (reach stack depth limit)
   * @param state
   * @param o
   * @param partial
   * if true, check partial equality
   * @return
   * if this == o, return true,
   * otherwise return false (even if error)
   */
  bool equals(ARState &state, const Value &o, bool partial = false) const;

  /**
   * three-way total order compare. may raise error (reach stack depth limit)
   * @param state
   * @param o
   * @return
   * if this < o, return negative number
   * if this == o, return 0
   * if this > o, return positive number
   * if error, return -1
   */
  int compare(ARState &state, const Value &o) const;

  /**
   * force mutate string.
   * @param state
   * if it has error, set error to state
   * @param value
   * @return
   * if new size is greater than limit, return false
   */
  bool appendAsStr(ARState &state, StringRef value);

  template <typename T, typename... A>
  static Value create(A &&...args) {
    static_assert(std::is_base_of_v<Object, T>, "must be subtype of Object");

    return Value(ObjectConstructor<T, A...>::construct(std::forward<A>(args)...));
  }

  static Value createNum(unsigned int v) { return Value(static_cast<uint64_t>(v)); }

  static Value createStackGuard(StackGuardType t, unsigned int level = 0) {
    Value ret;
    ret.u32s.kind = ValueKind::STACK_GUARD;
    ret.u32s.values[0] = static_cast<uint32_t>(t);
    ret.u32s.values[1] = level;
    ret.u32s.values[2] = 0;
    return ret;
  }

  static Value createDummy(const Type &type, unsigned int v1 = 0, unsigned int v2 = 0) {
    Value ret;
    ret.u32s.kind = ValueKind::DUMMY;
    ret.u32s.values[0] = static_cast<uint32_t>(type.typeId());
    ret.u32s.values[1] = v1;
    ret.u32s.values[2] = v2;
    return ret;
  }

  static Value createExpandMeta(ExpandMeta meta, unsigned int v) {
    Value ret;
    ret.u32s.kind = ValueKind::EXPAND_META;
    ret.u32s.values[0] = static_cast<unsigned int>(meta);
    ret.u32s.values[1] = v;
    return ret;
  }

  static Value createNumList(uint32_t v1, uint32_t v2, uint32_t v3) {
    Value ret;
    ret.u32s.kind = ValueKind::NUM_LIST;
    ret.u32s.values[0] = v1;
    ret.u32s.values[1] = v2;
    ret.u32s.values[2] = v3;
    return ret;
  }

  static Value createInvalid() {
    Value ret;
    ret.value.kind = ValueKind::INVALID;
    return ret;
  }

  static Value createBool(bool v) { return Value(v); }

  static Value createSig(int num) {
    Value ret(static_cast<int64_t>(num));
    ret.value.kind = ValueKind::SIG;
    return ret;
  }

  static Value createInt(int64_t num) { return Value(num); }

  static Value createFloat(double v) { return Value(v); }

  // for string construction
  static Value createStr() { return Value("", 0); }

  static Value createStr(const char *str) {
    assert(str);
    return createStr(StringRef(str));
  }

  static Value createStr(StringRef ref) {
    if (ref.size() <= smallStrSize(ValueKind::SSTR14)) {
      return Value(ref.data(), ref.size());
    }
    return create<StringObject>(ref);
  }

  static Value createStr(std::string &&value) {
    if (value.size() <= smallStrSize(ValueKind::SSTR14)) {
      return Value(value.data(), value.size());
    }
    return create<StringObject>(std::move(value));
  }

  /**
   * create String from grapheme cluster.
   * if it has invalid code points, replace theme with 'Unicode replacement char'
   * @param ret
   * @return
   */
  static Value createStr(const GraphemeCluster &ret);
};

template <typename T>
T &typeAs(const Value &value) noexcept {
  static_assert(std::is_base_of_v<Object, T>, "must be subtype of Object");

#ifdef USE_SAFE_CAST
  constexpr bool useSafeCast = true;
#else
  constexpr bool useSafeCast = false;
#endif

  if (useSafeCast) {
    if (!value.isObject()) {
      fatal("must be represent Object, but actual is: %d\n", toUnderlying(value.kind()));
    }
    auto *r = checked_cast<T>(value.get());
    if (r == nullptr) {
      const char *target = toString(T::KIND);
      const char *actual = toString(value.get()->getKind());
      fatal("target type is: %s, but actual is: %s\n", target, actual);
    }
    return *r;
  }
  return cast<T>(*value.get());
}

template <typename T>
ObjPtr<T> toObjPtr(const Value &value) noexcept {
  auto &ref = typeAs<T>(value);
  return ObjPtr<T>(&ref);
}

enum class ConcatOp : unsigned char {
  APPEND = 1u << 0u,
  INTERPOLATE = 1u << 1u,
};

template <>
struct allow_enum_bitop<ConcatOp> : std::true_type {};

inline bool concatAsStr(ARState &state, Value &left, const Value &right,
                        const ConcatOp concatOp = {}) {
  if (right.kind() == ValueKind::SSTR0) {
    return true; // do nothing
  }
  if (left.kind() == ValueKind::SSTR0 && right.hasStrRef()) {
    left = right;
    return true;
  }
  const int copyCount = hasFlag(concatOp, ConcatOp::APPEND) ? 2 : 1;
  if (left.isObject() && left.get()->getRefcount() > copyCount) {
    left = Value::createStr(left.asStrRef());
  }
  if (right.hasStrRef()) { // fast-path
    return left.appendAsStr(state, right.asStrRef());
  }
  if (hasFlag(concatOp, ConcatOp::INTERPOLATE)) {
    return right.opInterp(state, left);
  }
  return right.opStr(state, left);
}

inline Value exitStatusToBool(int64_t s) { return Value::createBool(s == 0); }

class JobObject;

class UnixFdObject : public ObjectWithRtti<ObjectKind::UnixFd> {
private:
  int fd;
  const bool hasJob;
  RawValue data[]; // for Job

  UnixFdObject(int fd, bool hasJob)
      : ObjectWithRtti(hasJob ? TYPE::ProcSubst : TYPE::FD), fd(fd), hasJob(hasJob) {}

public:
  static const ObjPtr<UnixFdObject> &empty();

  static UnixFdObject *create(int fd) {
    void *ptr = operator new(sizeof(UnixFdObject));
    return new (ptr) UnixFdObject(fd, false);
  }

  /*
   * @param fd
   * @param job
   * must not be null
   * @return
   */
  static UnixFdObject *create(int fd, ObjPtr<JobObject> &&job);

  void operator delete(void *ptr) { ::operator delete(ptr); }

  ~UnixFdObject();

  int tryToClose(bool forceClose) {
    if (!forceClose && this->fd < 0) {
      return 0;
    }
    const int s = close(this->fd);
    this->fd = -1;
    return s;
  }

  /**
   * try to set close-on-exec flag to file descriptor.
   * if fd is STDIN, STDOUT or STDERR, not set flag.
   * @param set
   * if true, try to set close-on-exec
   * if false, try to unset close-on-exec
   * @return
   * if internal file descriptor is invalid, return false
   */
  bool closeOnExec(bool set) const {
    if (this->fd < 0) {
      errno = EBADF;
      return false;
    }
    if (this->fd > STDERR_FILENO) {
      return setCloseOnExec(this->fd, set);
    }
    return true;
  }

  int getRawFd() const { return this->fd; }

  std::string toString() const {
    char buf[32];
    const int s = snprintf(buf, std::size(buf), "/dev/fd/%d", this->getRawFd());
    assert(s > 0);
    return {buf, static_cast<size_t>(s)};
  }

  /**
   * @return must be JobObject
   */
  const Value &getJob() const {
    assert(this->hasJob);
    return static_cast<const Value &>(this->data[0]);
  }

  /**
   * duplicate underlying file descriptor with close-on-exec=on
   * @return
   * if failed, set errno and return null
   */
  ObjPtr<UnixFdObject> dupWithCloseOnExec() const;
};

template <typename... Arg>
struct ObjectConstructor<UnixFdObject, Arg...> {
  static Object *construct(Arg &&...arg) { return UnixFdObject::create(std::forward<Arg>(arg)...); }
};

class RegexObject : public ObjectWithRtti<ObjectKind::Regex> {
private:
  PCRE re;

public:
  struct MatchResult {
    std::vector<Value> groups; // must be String?
    size_t start;              // start byte offset of match string
    size_t end;                // end byte offset of match string
  };

  explicit RegexObject(PCRE &&re) : ObjectWithRtti(TYPE::Regex), re(std::move(re)) {}

  bool search(ARState &state, StringRef ref) { return this->match(state, ref, nullptr); }

  /**
   * @param state
   * if it has error, set error to state
   * @param ref
   * @param ret
   * may be null
   * @return
   * if not matched, return false
   */
  bool match(ARState &state, StringRef ref, MatchResult *ret);

  bool replace(StringRef target, StringRef replacement, std::string &output, bool global) {
    return this->re.substitute(target, replacement, global, StringObject::MAX_SIZE, output) >= 0;
  }

  const PCRE &getRE() const { return this->re; }

  const char *getStr() const { return this->re.getPattern(); }
};

class RegexMatchObject : public ObjectWithRtti<ObjectKind::RegexMatch> {
private:
  ObjPtr<RegexObject> reObj;
  std::vector<Value> groups;
  unsigned int startOffset;
  unsigned int endOffset;

public:
  RegexMatchObject(ObjPtr<RegexObject> &&reObj, RegexObject::MatchResult &&matchResults)
      : ObjectWithRtti(TYPE::RegexMatch), reObj(std::move(reObj)),
        groups(std::move(matchResults.groups)), startOffset(matchResults.start),
        endOffset(matchResults.end) {}

  const auto &getRE() const { return this->reObj->getRE(); }

  const auto &getGroups() const { return this->groups; }

  unsigned int getStartOffset() const { return this->startOffset; }

  unsigned int getEndOffset() const { return this->endOffset; }
};

class ArrayObject : public ObjectWithRtti<ObjectKind::Array> {
public:
  enum class LockType : unsigned char {
    NONE,
    ITER,
    SORT_BY,
    SEARCH_SORTED_BY,
    HISTORY,
  };

private:
  std::vector<Value> values;
  LockType lockType{LockType::NONE};
  int lockCount{0};

public:
  static constexpr size_t MAX_SIZE = SYS_LIMIT_ARRAY_MAX;

  using IterType = std::vector<Value>::const_iterator;

  explicit ArrayObject(const Type &type) : ObjectWithRtti(type) {}

  ArrayObject(const Type &type, std::vector<Value> &&values)
      : ArrayObject(type.typeId(), std::move(values)) {}

  ArrayObject(unsigned int typeID, std::vector<Value> &&values)
      : ObjectWithRtti(typeID), values(std::move(values)) {}

  void lock(LockType t) {
    if (this->lockCount == 0) {
      this->lockType = t;
    }
    this->lockCount++;
  }

  void unlock() {
    if (--this->lockCount == 0) {
      this->lockType = LockType::NONE;
    }
  }

  bool locking() const { return this->lockCount > 0; }

  const std::vector<Value> &getValues() const { return this->values; }

  std::vector<Value> &refValues() { return this->values; }

  size_t size() const { return this->values.size(); }

  void append(Value &&obj) { this->values.push_back(std::move(obj)); }

  void append(const Value &obj) { this->values.push_back(obj); }

  /**
   * append and check array size limit
   * @param state
   * @param obj
   * @return
   * if has error (reach array size limit), return false
   */
  [[nodiscard]] bool append(ARState &state, Value &&obj);

  /**
   *
   * @param state
   * @param name
   * additional name
   * @return
   * if in locking, return false
   */
  bool checkIteratorInvalidation(ARState &state, const char *name = nullptr) const;

  ObjPtr<ArrayObject> copy() const {
    return toObjPtr<ArrayObject>(
        Value::create<ArrayObject>(this->getTypeID(), std::vector<Value>(this->values)));
  }

  Value takeFirst() {
    auto v = this->values.front();
    this->values.erase(values.begin());
    return v;
  }

  void sortAsStrArray(unsigned int beginOffset = 0) {
    std::sort(this->values.begin() + beginOffset, this->values.end(),
              [](const Value &x, const Value &y) { return x.asStrRef() < y.asStrRef(); });
  }
};

class ArrayIterObject : public ObjectWithRtti<ObjectKind::ArrayIter> {
private:
  ObjPtr<ArrayObject> arrayObj;
  unsigned int index{0};

public:
  explicit ArrayIterObject(ObjPtr<ArrayObject> obj)
      : ObjectWithRtti(obj->getTypeID()), arrayObj(std::move(obj)) {
    this->arrayObj->lock(ArrayObject::LockType::ITER);
  }

  ~ArrayIterObject() { this->arrayObj->unlock(); }

  bool hasNext() const { return this->index < this->arrayObj->size(); }

  Value next() { return this->arrayObj->getValues()[this->index++]; }
};

struct StrArrayIter {
  ArrayObject::IterType actual;

  explicit StrArrayIter(ArrayObject::IterType actual) : actual(actual) {}

  auto operator*() const { return this->actual->asStrRef(); }

  auto operator-(const StrArrayIter &o) const { return this->actual - o.actual; }

  bool operator==(const StrArrayIter &o) const { return this->actual == o.actual; }

  bool operator!=(const StrArrayIter &o) const { return !(*this == o); }

  StrArrayIter &operator++() {
    ++this->actual;
    return *this;
  }

  StrArrayIter &operator--() {
    --this->actual;
    return *this;
  }
};

#define ASSERT_ARRAY_SIZE(obj) assert((obj).size() <= ArrayObject::MAX_SIZE)

class StringIterObject : public ObjectWithRtti<ObjectKind::StringIter> {
private:
  const Value str; // must be String

  // for grapheme scanner state
  unsigned int prevPos;
  unsigned int curPos;
  unsigned int boundary;

public:
  explicit StringIterObject(Value str);

  /**
   * get next iteration
   * @return
   * if reach the end, return invalid
   */
  Value next();
};

class BaseObject : public ObjectWithRtti<ObjectKind::Base> {
private:
  unsigned int hash{0}; // for Value type
  unsigned int fieldSize;
  RawValue fields[];

  BaseObject(const Type &type, unsigned int size) : ObjectWithRtti(type), fieldSize(size) {
    for (unsigned int i = 0; i < this->fieldSize; i++) {
      new (&this->fields[i]) Value();
    }
  }

public:
  static BaseObject *create(const Type &type, unsigned int size) {
    void *ptr = operator new(sizeof(BaseObject) + sizeof(RawValue) * size);
    return new (ptr) BaseObject(type, size);
  }

  void operator delete(void *ptr) { ::operator delete(ptr); }

  static BaseObject *create(const BaseRecordType &type) {
    return create(type, type.getFieldSize());
  }

  ~BaseObject();

  Value &operator[](unsigned int index) { return static_cast<Value &>(this->fields[index]); }

  const Value &operator[](unsigned int index) const {
    return static_cast<const Value &>(this->fields[index]);
  }

  unsigned int getFieldSize() const { return this->fieldSize; }

  unsigned int getHash() const { return this->hash; }

  void setHash(unsigned int h) { this->hash = h; }
};

template <typename... Arg>
struct ObjectConstructor<BaseObject, Arg...> {
  static Object *construct(Arg &&...arg) { return BaseObject::create(std::forward<Arg>(arg)...); }
};

class StackTraceElement {
private:
  std::string sourceName;
  unsigned int lineNum;
  std::string callerName;

public:
  StackTraceElement(const char *sourceName, unsigned int lineNum, std::string &&callerName)
      : sourceName(sourceName), lineNum(lineNum), callerName(std::move(callerName)) {}

  ~StackTraceElement() = default;

  const std::string &getSourceName() const { return this->sourceName; }

  unsigned int getLineNum() const { return this->lineNum; }

  const std::string &getCallerName() const { return this->callerName; }
};

/**
 *
 * @param elements
 * @return
 * if stack trace elements is empty, return 0.
 */
inline unsigned int getOccurredLineNum(const std::vector<StackTraceElement> &elements) {
  return elements.empty() ? 0 : elements.front().getLineNum();
}

/**
 *
 * @param elements
 * @return
 * fi stack trace elements is empty, return empty string
 */
inline const char *getOccurredSourceName(const std::vector<StackTraceElement> &elements) {
  return elements.empty() ? "" : elements.front().getSourceName().c_str();
}

class ErrorObject : public ObjectWithRtti<ObjectKind::Error> {
private:
  Value message;
  Value name;
  int64_t status;
  std::vector<StackTraceElement> stackTrace;
  std::vector<ObjPtr<ErrorObject>> suppressed;

public:
  ErrorObject(const Type &type, Value &&message, Value &&name, int64_t status,
              std::vector<StackTraceElement> &&stackTrace)
      : ObjectWithRtti(type), message(std::move(message)), name(std::move(name)), status(status),
        stackTrace(std::move(stackTrace)) {}

  const Value &getMessage() const { return this->message; }

  const Value &getName() const { return this->name; }

  int64_t getStatus() const { return this->status; }

  enum class PrintOp : unsigned char {
    DEFAULT,      // only print stack trace
    UNCAUGHT,     // show uncaught exception message header
    IGNORED,      // show ignored exception message header
    IGNORED_TERM, // show ignore exception (from termination handler) message header
  };

  /**
   * print stack trace to stderr
   * @param ctx
   * @param op
   */
  void printStackTrace(const ARState &ctx, PrintOp op = PrintOp::DEFAULT) const;

  const std::vector<StackTraceElement> &getStackTrace() const { return this->stackTrace; }

  /**
   * @param
   * @return if suppressed except size reaches limit, remove and return oldest entry
   */
  ObjPtr<ErrorObject> addSuppressed(ObjPtr<ErrorObject> &&except);

  const auto &getSuppressed() const { return this->suppressed; }

  /**
   * create new Error_Object and create stack trace
   */
  static ObjPtr<ErrorObject> newError(const ARState &state, const Type &type, Value &&message,
                                      int64_t status);
};

enum class CodeKind : unsigned char {
  TOPLEVEL,
  FUNCTION,
  USER_DEFINED_CMD,
  NATIVE,
};

class ARCode {
protected:
  struct Base {
    const CodeKind codeKind{CodeKind::NATIVE};

    const unsigned char localVarNum{0};

    const unsigned short stackDepth{0};

    const unsigned int size{0};

    unsigned char *code{nullptr};
  } base;

public:
  ARCode() = default;

  ARCode(Base base) : base(base) {} // NOLINT

  const unsigned char *getCode() const { return this->base.code; }

  CodeKind getKind() const { return this->base.codeKind; }

  bool is(CodeKind kind) const { return this->getKind() == kind; }

  unsigned short getLocalVarNum() const { return this->base.localVarNum; }

  unsigned short getStackDepth() const { return this->base.stackDepth; }

  unsigned int getCodeSize() const { return this->base.size; }
};

class NativeCode : public ARCode {
public:
  using ArrayType = std::array<char, 8>;

private:
  ArrayType value;

public:
  NON_COPYABLE(NativeCode);

  NativeCode() noexcept
      : ARCode({
            .codeKind = CodeKind::NATIVE,
            .localVarNum = 4,
            .stackDepth = 4,
            .size = 0,
            .code = nullptr,
        }) {}

  explicit NativeCode(const ArrayType &value) noexcept : NativeCode() {
    this->value = value;
    this->setCode();
  }

  NativeCode(NativeCode &&o) noexcept : NativeCode() {
    this->value = o.value;
    this->setCode();
  }

  NativeCode &operator=(NativeCode &&o) noexcept {
    if (this != std::addressof(o)) {
      this->~NativeCode();
      new (this) NativeCode(std::move(o));
    }
    return *this;
  }

  static bool classof(const ARCode *code) { return code->is(CodeKind::NATIVE); }

private:
  void setCode() { this->base.code = reinterpret_cast<unsigned char *>(this->value.data()); }
};

struct LineNumEntry {
  unsigned int address;
  unsigned int lineNum;

  explicit operator bool() const { return this->address != SYS_LIMIT_FUNC_LEN; }
};

struct ExceptionEntry {
  /**
   * if Unresolved, indicate sentinel
   */
  unsigned int typeId;

  unsigned int begin; // inclusive
  unsigned int end;   // exclusive
  unsigned int dest;  // catch block address

  // for try block unwind
  unsigned short localOffset; // local variable offset of try block
  unsigned short localSize;   // local variable size of try block
  unsigned int guardLevel;    // level of try block guard

  explicit operator bool() const {
    return this->typeId != static_cast<unsigned int>(TYPE::Unresolved_);
  }
};

class CompiledCode : public ARCode {
private:
  ModId belongedModId{};

  /**
   * must not be null
   */
  char *sourceName{nullptr};

  /**
   * must not be null
   */
  char *name{nullptr};

  /**
   * last element is sentinel (nullptr)
   */
  Value *constPool{nullptr};

  /**
   * last element is sentinel ({0, 0})
   */
  LineNumEntry *lineNumEntries{nullptr};

  /**
   * last element is sentinel.
   */
  ExceptionEntry *exceptionEntries{nullptr};

public:
  NON_COPYABLE(CompiledCode);

  CompiledCode(const std::string &sourceName, ModId modId, const std::string &name, ARCode code,
               Value *constPool, LineNumEntry *sourcePosEntries,
               ExceptionEntry *exceptionEntries) noexcept
      : ARCode(code), belongedModId(modId), sourceName(strdup(sourceName.c_str())),
        name(strdup(name.c_str())), constPool(constPool), lineNumEntries(sourcePosEntries),
        exceptionEntries(exceptionEntries) {}

  CompiledCode(CompiledCode &&c) noexcept
      : ARCode(c), belongedModId(c.belongedModId), sourceName(c.sourceName), name(c.name),
        constPool(c.constPool), lineNumEntries(c.lineNumEntries),
        exceptionEntries(c.exceptionEntries) {
    c.name = nullptr;
    c.sourceName = nullptr;
    c.base.code = nullptr;
    c.constPool = nullptr;
    c.lineNumEntries = nullptr;
    c.exceptionEntries = nullptr;
  }

  CompiledCode() noexcept : ARCode() { this->base.code = nullptr; }

  ~CompiledCode() {
    free(this->sourceName);
    free(this->name);
    free(this->base.code);
    delete[] this->constPool;
    free(this->lineNumEntries);
    delete[] this->exceptionEntries;
  }

  CompiledCode &operator=(CompiledCode &&o) noexcept {
    if (this != std::addressof(o)) {
      this->~CompiledCode();
      new (this) CompiledCode(std::move(o));
    }
    return *this;
  }

  ModId getBelongedModId() const { return this->belongedModId; }

  StringRef getSourceName() const { return this->sourceName; }

  /**
   * must not be null.
   */
  const char *getName() const { return this->name; }

  const Value *getConstPool() const { return this->constPool; }

  const LineNumEntry *getLineNumEntries() const { return this->lineNumEntries; }

  unsigned int getLineNum(unsigned int index) const;

  const ExceptionEntry *getExceptionEntries() const { return this->exceptionEntries; }

  explicit operator bool() const noexcept { return this->base.code != nullptr; }

  StackTraceElement toTraceElement(unsigned int index) const;

  static bool classof(const ARCode *code) { return !code->is(CodeKind::NATIVE); }
};

class FuncObject : public ObjectWithRtti<ObjectKind::Func> {
private:
  CompiledCode code;

public:
  FuncObject(const Type &funcType, CompiledCode &&callable)
      : ObjectWithRtti(funcType), code(std::move(callable)) {}

  const CompiledCode &getCode() const { return this->code; }
};

class ClosureObject : public ObjectWithRtti<ObjectKind::Closure> {
private:
  ObjPtr<FuncObject> func;

  unsigned int upvarSize;
  RawValue upvars[];

  ClosureObject(ObjPtr<FuncObject> func, unsigned int size)
      : ObjectWithRtti(func->getTypeID()), func(std::move(func)), upvarSize(size) {
    for (unsigned int i = 0; i < this->upvarSize; i++) {
      new (&this->upvars[i]) Value();
    }
  }

public:
  static ClosureObject *create(ObjPtr<FuncObject> func, unsigned int size, const Value *values) {
    void *ptr = operator new(sizeof(ClosureObject) + sizeof(RawValue) * size);
    auto *closure = new (ptr) ClosureObject(std::move(func), size);
    for (unsigned int i = 0; i < size; i++) {
      (*closure)[i] = values[i];
    }
    return closure;
  }

  ~ClosureObject();

  void operator delete(void *ptr) { ::operator delete(ptr); }

  const FuncObject &getFuncObj() const { return *this->func; }

  Value &operator[](unsigned int index) { return static_cast<Value &>(this->upvars[index]); }

  const Value &operator[](unsigned int index) const {
    return static_cast<const Value &>(this->upvars[index]);
  }
};

template <typename... Arg>
struct ObjectConstructor<ClosureObject, Arg...> {
  static Object *construct(Arg &&...arg) {
    return ClosureObject::create(std::forward<Arg>(arg)...);
  }
};

using CallArgs = std::pair<unsigned int, std::array<Value, 3>>;

template <typename... T>
CallArgs makeArgs(T &&...arg) {
  static_assert(sizeof...(arg) <= 3, "too long");
  return std::make_pair(sizeof...(arg), std::array<Value, 3>{{std::forward<T>(arg)...}});
}

class BoxObject : public ObjectWithRtti<ObjectKind::Box> {
private:
  Value value;

public:
  explicit BoxObject(Value &&value) : ObjectWithRtti(TYPE::Any), value(std::move(value)) {}

  const Value &getValue() const { return this->value; }

  void setValue(Value &&v) { this->value = std::move(v); }
};

class EnvCtxObject : public ObjectWithRtti<ObjectKind::EnvCtx> {
private:
  ARState &state;

  /**
   * maintains old env
   * first is env name
   * second is old value
   * if old value is invalid, unset env
   */
  std::vector<std::pair<Value, Value>> envs;

public:
  explicit EnvCtxObject(ARState &state) : ObjectWithRtti(TYPE::Any), state(state) {}

  ~EnvCtxObject();

  /**
   * save and set env
   * if name is IFS, also set and save global variable
   * @param name
   * @param value
   */
  void setAndSaveEnv(Value &&name, Value &&value);
};

class ReaderObject : public ObjectWithRtti<ObjectKind::Reader> {
private:
  bool available{true};
  unsigned short remainPos{0};
  unsigned short usedSize{0};
  char buf[256]; // NOLINT
  ObjPtr<UnixFdObject> fdObj;
  Value value; // actual read line

public:
  explicit ReaderObject(ObjPtr<UnixFdObject> &&fdObj)
      : ObjectWithRtti(TYPE::Reader), fdObj(std::move(fdObj)) {
    if (this->fdObj->getRawFd() == -1) {
      this->available = false;
    }
  }

  bool nextLine(ARState &state);

  Value takeLine() { return std::move(this->value); }
};

struct UserSysTime {
  struct timeval user{};
  struct timeval sys{};
};

class TimerObject : public ObjectWithRtti<ObjectKind::Timer> {
private:
  std::chrono::high_resolution_clock::time_point realTime; // for real-time
  UserSysTime userSysTime;

public:
  TimerObject();

  ~TimerObject();
};

}; // namespace arsh

#endif // ARSH_OBJECT_H
