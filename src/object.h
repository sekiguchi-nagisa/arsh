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

#include "brace.h"
#include "constant.h"
#include "misc/array_ref.hpp"
#include "misc/files.hpp"
#include "misc/rtti.hpp"
#include "misc/string_ref.hpp"
#include "object_util.h"
#include "regex_wrapper.h"
#include "type.h"
#include "value.h"
#include <config.h>

struct ARState;

namespace arsh {

#define EACH_OBJECT_KIND(OP)                                                                       \
  OP(Int)                                                                                          \
  OP(Float)                                                                                        \
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
  OP(Candidate)                                                                                    \
  OP(Candidates)

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
  unsigned int refCount{0};

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
  unsigned int getRefcount() const { return this->refCount; }

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
  static auto useCount(const Object *ptr) noexcept { return ptr->refCount; }

  static void increase(Object *ptr) noexcept {
    if (ptr != nullptr) {
      ++ptr->refCount;
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

class IntObject : public ObjectWithRtti<ObjectKind::Int> {
private:
  const int64_t value;

public:
  explicit IntObject(int64_t value) : ObjectWithRtti(TYPE::Int), value(value) {}

  int64_t getValue() const { return this->value; }
};

class FloatObject : public ObjectWithRtti<ObjectKind::Float> {
private:
  const double value;

public:
  explicit FloatObject(double value) : ObjectWithRtti(TYPE::Float), value(value) {};

  double getValue() const { return this->value; }
};

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

enum class ValueTag : unsigned char {
  OBJECT, // object pointer (not null)
  EMPTY,  // null
  FLOAT,  // common float
  UINT,   // uint56
  INT,    // int56
  STRING, // small string (up to 6 byte)
};

enum class ValueKind : unsigned char {
  EMPTY = toUnderlying(ValueTag::EMPTY),
  OBJECT = toUnderlying(ValueTag::OBJECT),                      // not null
  NUMBER = (0x1 << 3) | toUnderlying(ValueTag::UINT),           // uint64_t
  BRACE_RANGE_ATTR = (0x2 << 3) | toUnderlying(ValueTag::UINT), // [uint16_t uint32_t]
  STACK_GUARD = (0x3 << 3) | toUnderlying(ValueTag::UINT),      // [uint16_t uint32_t]
  DUMMY = (0x4 << 3) | toUnderlying(ValueTag::UINT),            // [uint16_t, DSType(uint32_t)]
  EXPAND_META =
      (0x5 << 3) |
      toUnderlying(ValueTag::UINT), // [uint16_t, uint32_t], for glob meta, '?', '*', '{', ',', '}'
  INVALID = (0x6 << 3) | toUnderlying(ValueTag::UINT),

  BOOL = (0x1 << 3) | toUnderlying(ValueTag::INT),
  SIG = (0x2 << 3) | toUnderlying(ValueTag::INT),       // int64_t
  SMALL_INT = (0x3 << 3) | toUnderlying(ValueTag::INT), // int64_t
  COMMON_FLOAT = toUnderlying(ValueTag::FLOAT),         // double
  SMALL_STR = toUnderlying(ValueTag::STRING),
};

struct RawValue {
  using TValue = TaggedValue<ValueTag>;

  TValue tv;
};

template <typename T, typename... Arg>
struct ObjectConstructor {
  static Object *construct(Arg &&...arg) { return new T(std::forward<Arg>(arg)...); }
};

class GraphemeCluster;

class Value : public RawValue {
private:
  static_assert(sizeof(RawValue) == 8);

  static constexpr auto EMPTY = TValue{.u64 = toUnderlying(ValueTag::EMPTY)};
  static constexpr auto INVALID = TValue{.u64 = toUnderlying(ValueKind::INVALID)};

  Value(ValueKind kind, uint64_t value) noexcept {
    this->tv = TValue::encodeTaggedUInt(kind, value);
  }

  Value(ValueKind kind, uint16_t u16, uint32_t u32) noexcept {
    this->tv = TValue::encodeTaggedUInt(kind, static_cast<uint64_t>(u16) << 32 |
                                                  static_cast<uint64_t>(u32));
  }

  explicit Value(int64_t value) noexcept {
    this->tv = TValue::encodeTaggedInt(ValueKind::SMALL_INT, value);
  }

  explicit Value(bool value) noexcept {
    this->tv = TValue::encodeTaggedInt(ValueKind::BOOL, value ? 1 : 0);
  }

  explicit Value(double value) noexcept {
    this->tv = TValue::encodeTaggedFloat<ValueTag::FLOAT>(value);
  }

  /**
   * for small string construction
   */
  Value(const char *data, unsigned int size) noexcept {
    assert(data || size == 0);
    assert(size <= TValue::MAX_STR_SIZE);
    this->tv.set<ValueTag::STRING>(data, size);
  }

public:
  explicit Value(Object *o) noexcept {
    assert(o);
    o->refCount++;
    this->tv.ptr = o;
  }

  /**
   * equivalent to Value(nullptr)
   */
  Value() noexcept { this->tv = EMPTY; }

  Value(std::nullptr_t) noexcept : Value() {} // NOLINT

  Value(const Value &value) noexcept : RawValue(value) {
    if (this->isObject()) {
      this->get()->refCount++;
    }
  }

  /**
   * not increment refCount
   */
  Value(Value &&value) noexcept : RawValue(value) { value.tv = EMPTY; }

  template <typename T, enable_when<std::is_base_of_v<Object, T>> = nullptr>
  Value(const ObjPtr<T> &o) noexcept : Value(Value(o.get())) {} // NOLINT

  ~Value() {
    if (this->isObject()) {
      if (--this->get()->refCount == 0) {
        this->get()->destroy();
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

  void swap(Value &o) noexcept { std::swap(this->tv, o.tv); }

  ValueKind kind() const {
    switch (this->tv.getTag()) {
    case ValueTag::EMPTY:
      return ValueKind::EMPTY;
    case ValueTag::OBJECT:
      return ValueKind::OBJECT;
    case ValueTag::FLOAT:
      return ValueKind::COMMON_FLOAT;
    case ValueTag::INT:
    case ValueTag::UINT:
      return static_cast<ValueKind>(this->tv.u64 & 0xFF);
    case ValueTag::STRING:
      return ValueKind::SMALL_STR;
    }
    return ValueKind::EMPTY; // unreachable
  }

  /**
   * release current pointer.
   */
  void reset() noexcept {
    this->~Value();
    this->tv = EMPTY;
  }

  Object *get() const noexcept {
    static_assert(toUnderlying(ValueTag::OBJECT) == 0);
    assert(this->isObject());
    return static_cast<Object *>(this->tv.ptr);
  }

  ObjPtr<Object> toPtr() const { return ObjPtr<Object>(this->get()); }

  explicit operator bool() const noexcept { return this->tv.u64 != EMPTY.u64; }

  /**
   * if represents Object, return true.
   */
  bool isObject() const noexcept { return this->tv.hasTag(ValueTag::OBJECT); }

  bool isInvalid() const noexcept { return this->tv.u64 == INVALID.u64; }

  unsigned int getTypeID() const;

  bool hasType(TYPE t) const { return hasType(static_cast<unsigned int>(t)); }

  bool hasType(unsigned int id) const { return this->getTypeID() == id; }

  bool hasStrRef() const {
    return this->tv.hasTag(ValueTag::STRING) ||
           (this->isObject() && this->get()->getKind() == ObjectKind::String);
  }

  unsigned int asNum() const {
    assert(this->kind() == ValueKind::NUMBER);
    return TValue::decodeTaggedUInt(this->tv);
  }

  std::pair<uint16_t, uint32_t> asUInt16UInt32Pair() const {
    assert(this->tv.hasTag(ValueTag::UINT));
    auto v = TValue::decodeTaggedUInt(this->tv);
    return {static_cast<uint16_t>(v >> 32),
            static_cast<uint32_t>(v & static_cast<uint64_t>(UINT32_MAX))};
  }

  std::pair<BraceRange::Kind, unsigned int> asBraceRangeAttr() const {
    assert(this->kind() == ValueKind::BRACE_RANGE_ATTR);
    auto [u16, u32] = this->asUInt16UInt32Pair();
    return {static_cast<BraceRange::Kind>(u16), u32};
  }

  std::pair<StackGuardType, unsigned int> asStackGuard() const {
    assert(this->kind() == ValueKind::STACK_GUARD);
    auto [u16, u32] = this->asUInt16UInt32Pair();
    return {static_cast<StackGuardType>(u16), u32};
  }

  unsigned int asTypeId() const {
    assert(this->kind() == ValueKind::DUMMY);
    return this->asUInt16UInt32Pair().second;
  }

  unsigned int asTypeIdMeta() const {
    assert(this->kind() == ValueKind::DUMMY);
    return this->asUInt16UInt32Pair().first;
  }

  std::pair<ExpandMeta, unsigned int> asExpandMeta() const {
    assert(this->kind() == ValueKind::EXPAND_META);
    auto [u16, u32] = this->asUInt16UInt32Pair();
    return {static_cast<ExpandMeta>(u16), u32};
  }

  bool asBool() const {
    assert(this->kind() == ValueKind::BOOL);
    return TValue::decodeTaggedInt(this->tv) == 1;
  }

  int asSig() const {
    assert(this->kind() == ValueKind::SIG);
    return static_cast<int>(TValue::decodeTaggedInt(this->tv));
  }

  bool hasInt() const {
    return this->kind() == ValueKind::SMALL_INT ||
           (this->isObject() && this->get()->getKind() == ObjectKind::Int);
  }

  int64_t asInt() const {
    if (this->tv.hasTag(ValueTag::INT)) {
      return TValue::decodeTaggedInt(this->tv);
    }
    assert(this->get()->getKind() == ObjectKind::Int);
    return static_cast<IntObject *>(this->get())->getValue();
  }

  bool hasFloat() const {
    return this->tv.hasTag(ValueTag::FLOAT) ||
           (this->isObject() && this->get()->getKind() == ObjectKind::Float);
  }

  double asFloat() const {
    if (this->tv.hasTag(ValueTag::FLOAT)) {
      return TValue::decodeTaggedFloat<ValueTag::FLOAT>(this->tv);
    }
    assert(this->get()->getKind() == ObjectKind::Float);
    return static_cast<FloatObject *>(this->get())->getValue();
  }

  StringRef asStrRef() const;

  const char *asCStr() const { return this->asStrRef().data(); }

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

  static Value createNum(unsigned int v) { return Value(ValueKind::NUMBER, v); }

  static Value createStackGuard(StackGuardType t, unsigned int level = 0) {
    static_assert(sizeof(StackGuardType) <= sizeof(uint16_t));
    return Value(ValueKind::STACK_GUARD, toUnderlying(t), level);
  }

  static Value createDummy(const Type &type, unsigned int v1 = 0) {
    return Value(ValueKind::DUMMY, v1, type.typeId());
  }

  static Value createExpandMeta(ExpandMeta meta, unsigned int v) {
    static_assert(sizeof(ExpandMeta) <= sizeof(uint16_t));
    return Value(ValueKind::EXPAND_META, toUnderlying(meta), v);
  }

  static Value createBraceRangeAttr(BraceRange::Kind kind, unsigned int digits) {
    static_assert(sizeof(BraceRange::Kind) <= sizeof(uint16_t));
    return Value(ValueKind::BRACE_RANGE_ATTR, toUnderlying(kind), digits);
  }

  static Value createInvalid() {
    Value value;
    value.tv = INVALID;
    return value;
  }

  static Value createBool(bool v) { return Value(v); }

  static Value createSig(int num) {
    Value ret;
    ret.tv = TValue::encodeTaggedInt(ValueKind::SIG, num);
    return ret;
  }

  static Value createInt(int64_t num) {
    if (TValue::withinInt56(num)) {
      return Value(num);
    }
    return create<IntObject>(num);
  }

  static Value createFloat(double v) {
    if (Value value(v); value.tv.hasTag(ValueTag::FLOAT)) {
      return value;
    }
    return create<FloatObject>(v);
  }

  // for string construction
  static Value createStr() { return createStr(StringRef()); }

  static Value createStr(const char *str) {
    assert(str);
    return createStr(StringRef(str));
  }

  static Value createStr(StringRef ref);

  static Value createStr(std::string &&value);

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

template <typename T, enable_when<std::is_base_of_v<Object, T>> = nullptr>
ObjPtr<T> toObjPtr(const Value &value) noexcept {
  auto &ref = typeAs<T>(value);
  return ObjPtr<T>(&ref);
}

template <typename T, typename... A>
ObjPtr<T> createObject(A &&...args) {
  static_assert(std::is_base_of_v<Object, T>, "must be subtype of Object");

  return ObjPtr<T>(
      static_cast<T *>(ObjectConstructor<T, A...>::construct(std::forward<A>(args)...)));
}

enum class ConcatOp : unsigned char {
  APPEND = 1u << 0u,
  INTERPOLATE = 1u << 1u,
};

template <>
struct allow_enum_bitop<ConcatOp> : std::true_type {};

inline bool concatAsStr(ARState &state, Value &left, const Value &right,
                        const ConcatOp concatOp = {}) {
  if (right.hasStrRef()) {
    if (right.asStrRef().empty()) {
      return true; // do nothing
    }
    if (left.hasStrRef() && left.asStrRef().empty()) {
      left = right;
      return true;
    }
  }
  const unsigned int copyCount = hasFlag(concatOp, ConcatOp::APPEND) ? 2 : 1;
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

  ArrayRef<Value> groupsView() const { return {this->groups.data(), this->groups.size()}; }

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

  ArrayRef<Value> view() const { return {this->values.data(), this->values.size()}; }

  const Value &operator[](const size_t index) const { return this->values[index]; }

  Value &operator[](const size_t index) { return this->values[index]; }

  const auto &back() const { return this->values.back(); }

  const auto &front() const { return this->values.front(); }

  size_t size() const { return this->values.size(); }

  auto begin() const { return this->values.begin(); }

  auto end() const { return this->values.end(); }

  auto begin() { return this->values.begin(); }

  auto end() { return this->values.end(); }

  auto erase(IterType first, IterType last) { return this->values.erase(first, last); }

  auto erase(IterType iter) { return this->values.erase(iter); }

  void pop_back() { this->values.pop_back(); }

  void clear() { this->erase(this->begin(), this->end()); }

  void resize(size_t afterSize) { this->values.resize(afterSize); }

  void reserve(size_t newCap) { this->values.reserve(newCap); }

  void append(Value &&obj) { this->values.push_back(std::move(obj)); }

  void append(const Value &obj) { this->values.push_back(obj); }

  /**
   * append and check array size limit
   * @param state
   * @param obj
   * @return
   * if error (reach array size limit), return false
   */
  [[nodiscard]] bool append(ARState &state, Value &&obj) {
    return this->insert(state, this->size(), std::move(obj));
  }

  /**
   * insert and check array size limit
   * @param state
   * @param index
   * if larger than size, append to last
   * @param value
   * @return
   * if error (reach array size limit), return false
   */
  [[nodiscard]] bool insert(ARState &state, size_t index, Value &&value);

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
    return createObject<ArrayObject>(this->getTypeID(), std::vector<Value>(this->values));
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

  Value next() { return (*this->arrayObj)[this->index++]; }
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
