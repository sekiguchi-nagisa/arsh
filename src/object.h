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

#ifndef YDSH_OBJECT_H
#define YDSH_OBJECT_H

#include <unistd.h>

#include <array>
#include <memory>
#include <tuple>

#include "constant.h"
#include "lexer.h"
#include "misc/buffer.hpp"
#include "misc/fatal.h"
#include "misc/rtti.hpp"
#include "misc/string_ref.hpp"
#include "opcode.h"
#include "regex_wrapper.h"
#include "type.h"
#include <config.h>

struct DSState;

namespace ydsh {

#define EACH_OBJECT_KIND(OP)                                                                       \
  OP(String)                                                                                       \
  OP(UnixFd)                                                                                       \
  OP(Regex)                                                                                        \
  OP(Array)                                                                                        \
  OP(Map)                                                                                          \
  OP(MapIter)                                                                                      \
  OP(Base)                                                                                         \
  OP(Error)                                                                                        \
  OP(Func)                                                                                         \
  OP(Closure)                                                                                      \
  OP(Box)                                                                                          \
  OP(EnvCtx)                                                                                       \
  OP(Job)                                                                                          \
  OP(Pipeline)                                                                                     \
  OP(Redir)

/**
 * for LLVM-style RTTI
 * see. https://llvm.org/docs/HowToSetUpLLVMStyleRTTI.html
 */
enum class ObjectKind : unsigned char {
#define GEN_ENUM(K) K,
  EACH_OBJECT_KIND(GEN_ENUM)
#undef GEN_ENUM
};

class DSValue;

struct ObjectRefCount;

class DSObject {
protected:
  int refCount{0};

  /**
   * |  24bit  |  8bit       |
   *   TypeID    ObjectKind
   */
  const unsigned int tag{0};

  friend class DSValue;

  friend struct ObjectRefCount;

  NON_COPYABLE(DSObject);

  DSObject(ObjectKind kind, unsigned int typeID)
      : tag(typeID << 8 | static_cast<unsigned char>(kind)) {}

  ~DSObject() = default;

public:
  int getRefcount() const { return this->refCount; }

  unsigned int getTypeID() const { return this->tag >> 8; }

  ObjectKind getKind() const { return static_cast<ObjectKind>(this->tag & 0xFF); }

private:
  void destroy();
};

const char *toString(ObjectKind kind);

template <ObjectKind K>
struct ObjectWithRtti : public DSObject {
protected:
  static_assert(sizeof(DSObject) == 8);

  explicit ObjectWithRtti(const DSType &type) : ObjectWithRtti(type.typeId()) {}
  explicit ObjectWithRtti(TYPE type) : ObjectWithRtti(static_cast<unsigned int>(type)) {}
  explicit ObjectWithRtti(unsigned int id) : DSObject(K, id) {}

public:
  static constexpr auto KIND = K;

  static bool classof(const DSObject *obj) { return obj->getKind() == K; }
};

struct ObjectRefCount {
  static long useCount(const DSObject *ptr) noexcept { return ptr->refCount; }

  static void increase(DSObject *ptr) noexcept {
    if (ptr != nullptr) {
      ptr->refCount++;
    }
  }

  static void decrease(DSObject *ptr) noexcept {
    if (ptr != nullptr && --ptr->refCount == 0) {
      ptr->destroy();
    }
  }
};

template <typename T>
using ObjPtr = IntrusivePtr<T, ObjectRefCount>;

class UnixFdObject : public ObjectWithRtti<ObjectKind::UnixFd> {
private:
  int fd;

public:
  explicit UnixFdObject(int fd) : ObjectWithRtti(TYPE::UnixFD), fd(fd) {}
  ~UnixFdObject();

  int tryToClose(bool forceClose) {
    if (!forceClose && this->fd < 0) {
      return 0;
    }
    int s = close(this->fd);
    this->fd = -1;
    return s;
  }

  /**
   * set close-on-exec flag to file descriptor.
   * if fd is STDIN, STDOUT or STDERR, not set flag.
   * @param close
   * @return
   * if failed, return false
   */
  bool closeOnExec(bool close) const;

  int getValue() const { return this->fd; }
};

class StringObject : public ObjectWithRtti<ObjectKind::String> {
private:
  std::string value;

public:
  static constexpr size_t MAX_SIZE = INT32_MAX;

  explicit StringObject(std::string &&value)
      : ObjectWithRtti(TYPE::String), value(std::move(value)) {}

  explicit StringObject(const StringRef &ref) : StringObject(ref.toString()) {}

  const char *getValue() const { return this->value.c_str(); }

  unsigned int size() const { return this->value.size(); }

  void append(StringRef v) { this->value += v; }
};

enum class DSValueKind : unsigned char {
  EMPTY,
  OBJECT,    // not null
  NUMBER,    // uint64_t
  NUM_LIST,  // [uint32_t uint32_t, uint32_t]
  DUMMY,     // DSType(uint32_t), uint32_t, uint32_t
  GLOB_META, // uint64_t, for glob meta character, '?', '*'
  INVALID,
  BOOL,
  SIG,   // int64_t
  INT,   // int64_t
  FLOAT, // double

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

inline bool isSmallStr(DSValueKind kind) {
  switch (kind) {
  case DSValueKind::SSTR0:
  case DSValueKind::SSTR1:
  case DSValueKind::SSTR2:
  case DSValueKind::SSTR3:
  case DSValueKind::SSTR4:
  case DSValueKind::SSTR5:
  case DSValueKind::SSTR6:
  case DSValueKind::SSTR7:
  case DSValueKind::SSTR8:
  case DSValueKind::SSTR9:
  case DSValueKind::SSTR10:
  case DSValueKind::SSTR11:
  case DSValueKind::SSTR12:
  case DSValueKind::SSTR13:
  case DSValueKind::SSTR14:
    return true;
  default:
    return false;
  }
}

class DSValueBase {
protected:
  union {
    struct {
      DSValueKind kind;
      DSObject *value; // not null
    } obj;

    struct {
      DSValueKind kind;
      uint64_t value;
    } u64;

    struct {
      DSValueKind kind;
      int64_t value;
    } i64;

    struct {
      DSValueKind kind;
      uint32_t values[3];
    } u32s;

    struct {
      DSValueKind kind;
      bool value;
    } b;

    struct {
      DSValueKind kind;
      double value;
    } d;

    struct {
      DSValueKind kind;
      const DSType *value; // not null
    } type;

    struct {
      DSValueKind kind;
      char value[15]; // null terminated
    } str;
  };

public:
  void swap(DSValueBase &o) noexcept { std::swap(*this, o); }

  DSValueKind kind() const { return this->obj.kind; }

  static unsigned int smallStrSize(DSValueKind kind) {
    assert(isSmallStr(kind));
    return static_cast<unsigned int>(kind) - static_cast<unsigned int>(DSValueKind::SSTR0);
  }

  static DSValueKind toSmallStrKind(unsigned int size) {
    assert(size <= smallStrSize(DSValueKind::SSTR14));
    auto base = static_cast<unsigned int>(DSValueKind::SSTR0);
    return static_cast<DSValueKind>(base + size);
  }
};

template <typename T, typename... Arg>
struct ObjectConstructor {
  static DSObject *construct(Arg &&...arg) { return new T(std::forward<Arg>(arg)...); }
};

class StrBuilder;

class DSValue : public DSValueBase {
private:
  static_assert(sizeof(DSValueBase) == 16);

  explicit DSValue(uint64_t value) noexcept {
    this->u64.kind = DSValueKind::NUMBER;
    this->u64.value = value;
  }

  explicit DSValue(int64_t value) noexcept {
    this->i64.kind = DSValueKind::INT;
    this->i64.value = value;
  }

  explicit DSValue(bool value) noexcept {
    this->b.kind = DSValueKind::BOOL;
    this->b.value = value;
  }

  explicit DSValue(double value) noexcept {
    this->d.kind = DSValueKind::FLOAT;
    this->d.value = value;
  }

  /**
   * for small string construction
   */
  DSValue(const char *data, unsigned int size) noexcept {
    assert(data);
    assert(size <= smallStrSize(DSValueKind::SSTR14));
    this->str.kind = toSmallStrKind(size);
    memcpy(this->str.value, data, size);
    this->str.value[size] = '\0';
  }

public:
  explicit DSValue(DSObject *o) noexcept {
    assert(o);
    this->obj.kind = DSValueKind::OBJECT;
    this->obj.value = o;
    this->obj.value->refCount++;
  }

  /**
   * equivalent to DSValue(nullptr)
   */
  DSValue() noexcept { this->obj.kind = DSValueKind::EMPTY; }

  DSValue(std::nullptr_t) noexcept : DSValue() {} // NOLINT

  DSValue(const DSValue &value) noexcept : DSValueBase(value) {
    if (this->isObject()) {
      this->obj.value->refCount++;
    }
  }

  /**
   * not increment refCount
   */
  DSValue(DSValue &&value) noexcept : DSValueBase(value) { value.obj.kind = DSValueKind::EMPTY; }

  template <typename T, enable_when<std::is_base_of_v<DSObject, T>> = nullptr>
  DSValue(const ObjPtr<T> &o) noexcept : DSValue(DSValue(o.get())) {} // NOLINT

  ~DSValue() {
    if (this->isObject()) {
      if (--this->obj.value->refCount == 0) {
        this->obj.value->destroy();
      }
    }
  }

  DSValue &operator=(const DSValue &value) noexcept {
    if (this != std::addressof(value)) {
      this->~DSValue();
      new (this) DSValue(value);
    }
    return *this;
  }

  DSValue &operator=(DSValue &&value) noexcept {
    if (this != std::addressof(value)) {
      this->~DSValue();
      new (this) DSValue(std::move(value));
    }
    return *this;
  }

  /**
   * release current pointer.
   */
  void reset() noexcept {
    this->~DSValue();
    this->obj.kind = DSValueKind::EMPTY;
  }

  DSObject *get() const noexcept {
    assert(this->kind() == DSValueKind::OBJECT);
    return this->obj.value;
  }

  bool operator==(const DSValue &v) const noexcept { return this->equals(v); }

  bool operator!=(const DSValue &v) const noexcept { return !this->equals(v); }

  explicit operator bool() const noexcept { return this->kind() != DSValueKind::EMPTY; }

  /**
   * if represents DSObject, return true.
   */
  bool isObject() const noexcept { return this->kind() == DSValueKind::OBJECT; }

  bool isInvalid() const noexcept { return this->kind() == DSValueKind::INVALID; }

  unsigned int getTypeID() const;

  bool hasType(TYPE t) const { return hasType(static_cast<unsigned int>(t)); }

  bool hasType(unsigned int id) const { return this->getTypeID() == id; }

  bool hasStrRef() const {
    return isSmallStr(this->kind()) ||
           (this->isObject() && this->get()->getKind() == ObjectKind::String);
  }

  unsigned int asNum() const {
    assert(this->kind() == DSValueKind::NUMBER);
    return this->u64.value;
  }

  using uint32_3 = const uint32_t (&)[3];
  uint32_3 asNumList() const { return this->u32s.values; }

  unsigned int asTypeId() const {
    assert(this->kind() == DSValueKind::DUMMY);
    return this->u32s.values[0];
  }

  GlobMeta asGlobMeta() const {
    assert(this->kind() == DSValueKind::GLOB_META);
    return static_cast<GlobMeta>(this->u64.value);
  }

  bool asBool() const {
    assert(this->kind() == DSValueKind::BOOL);
    return this->b.value;
  }

  int asSig() const {
    assert(this->kind() == DSValueKind::SIG);
    return this->i64.value;
  }

  int64_t asInt() const {
    assert(this->kind() == DSValueKind::INT);
    return this->i64.value;
  }

  double asFloat() const {
    assert(this->kind() == DSValueKind::FLOAT);
    return this->d.value;
  }

  StringRef asStrRef() const;

  const char *asCStr() const { return this->asStrRef().data(); }

  /**
   * get light-weight string representation
   * @return
   */
  std::string toString() const;

  /**
   * OP_STR method implementation
   * @param builder
   * @return
   * if has error, return false
   */
  bool opStr(StrBuilder &builder) const;

  /**
   * OP_INTERP method implementation. write result to 'toStrBuf'
   * @param state
   * @return
   * if has error, return false
   */
  bool opInterp(StrBuilder &builder) const;

  /**
   * for HashMap
   */
  bool equals(const DSValue &o) const;

  /**
   * for HashMap
   * @return
   */
  size_t hash() const;

  /**
   * three-way compare for Array#sort
   * @param o
   * @return
   * if this < o, return negative number
   * if this == o, return 0
   * if this > o, return positive number
   */
  int compare(const DSValue &o) const;

  /**
   * force mutate string.
   * @param state
   * if has error, set error to state
   * @param value
   * @return
   * if new size is greater than limit, return false
   */
  bool appendAsStr(DSState &state, StringRef value);

  template <typename T, typename... A>
  static DSValue create(A &&...args) {
    static_assert(std::is_base_of<DSObject, T>::value, "must be subtype of DSObject");

    return DSValue(ObjectConstructor<T, A...>::construct(std::forward<A>(args)...));
  }

  static DSValue createNum(unsigned int v) { return DSValue(static_cast<uint64_t>(v)); }

  static DSValue createDummy(const DSType &type, unsigned int v1 = 0, unsigned int v2 = 0) {
    DSValue ret;
    ret.u32s.kind = DSValueKind::DUMMY;
    ret.u32s.values[0] = static_cast<uint32_t>(type.typeId());
    ret.u32s.values[1] = v1;
    ret.u32s.values[2] = v2;
    return ret;
  }

  static DSValue createGlobMeta(GlobMeta meta) {
    DSValue ret;
    ret.u64.kind = DSValueKind::GLOB_META;
    ret.u64.value = static_cast<unsigned int>(meta);
    return ret;
  }

  static DSValue createNumList(uint32_t v1, uint32_t v2, uint32_t v3) {
    DSValue ret;
    ret.u32s.kind = DSValueKind::NUM_LIST;
    ret.u32s.values[0] = v1;
    ret.u32s.values[1] = v2;
    ret.u32s.values[2] = v3;
    return ret;
  }

  static DSValue createInvalid() {
    DSValue ret;
    ret.obj.kind = DSValueKind::INVALID;
    return ret;
  }

  static DSValue createBool(bool v) { return DSValue(v); }

  static DSValue createSig(int num) {
    DSValue ret(static_cast<int64_t>(num));
    ret.i64.kind = DSValueKind::SIG;
    return ret;
  }

  static DSValue createInt(int64_t num) { return DSValue(num); }

  static DSValue createFloat(double v) { return DSValue(v); }

  // for string construction
  static DSValue createStr() { return DSValue("", 0); }

  static DSValue createStr(const char *str) {
    assert(str);
    return createStr(StringRef(str));
  }

  static DSValue createStr(StringRef ref) {
    if (ref.size() <= smallStrSize(DSValueKind::SSTR14)) {
      return DSValue(ref.data(), ref.size());
    }
    return DSValue::create<StringObject>(ref);
  }

  static DSValue createStr(std::string &&value) {
    if (value.size() <= smallStrSize(DSValueKind::SSTR14)) {
      return DSValue(value.data(), value.size());
    }
    return DSValue::create<StringObject>(std::move(value));
  }
};

template <typename T>
inline T &typeAs(const DSValue &value) noexcept {
  static_assert(std::is_base_of<DSObject, T>::value, "must be subtype of DSObject");

#ifdef USE_SAFE_CAST
  constexpr bool useSafeCast = true;
#else
  constexpr bool useSafeCast = false;
#endif

  if (useSafeCast) {
    if (!value.isObject()) {
      fatal("must be represent DSObject, but actual is: %d\n",
            static_cast<unsigned int>(value.kind()));
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
inline ObjPtr<T> toObjPtr(const DSValue &value) noexcept {
  auto &ref = typeAs<T>(value);
  return ObjPtr<T>(&ref);
}

inline bool concatAsStr(DSState &state, DSValue &left, const DSValue &right, bool selfConcat) {
  assert(right.hasStrRef());
  if (right.kind() == DSValueKind::SSTR0) {
    return true;
  }
  if (left.kind() == DSValueKind::SSTR0) {
    left = right;
    return true;
  }
  int copyCount = selfConcat ? 2 : 1;
  if (left.isObject() && left.get()->getRefcount() > copyCount) {
    left = DSValue::createStr(left.asStrRef());
  }
  return left.appendAsStr(state, right.asStrRef());
}

class StrBuilder {
private:
  DSState &state;
  DSValue buf; // must be String

public:
  explicit StrBuilder(DSState &st) : state(st), buf(DSValue::createStr("")) {}

  bool add(StringRef value) { return this->buf.appendAsStr(this->state, value); }

  DSState &getState() const { return this->state; }

  DSValue take() && { return std::move(this->buf); }
};

inline DSValue exitStatusToBool(int64_t s) { return DSValue::createBool(s == 0); }

class ArrayObject;

class RegexObject : public ObjectWithRtti<ObjectKind::Regex> {
private:
  std::string str; // for string representation
  PCRE re;

public:
  RegexObject(std::string str, PCRE &&re)
      : ObjectWithRtti(TYPE::Regex), str(std::move(str)), re(std::move(re)) {}

  bool search(DSState &state, StringRef ref) { return this->match(state, ref, nullptr) >= 0; }

  /**
   * @param state
   * if has error, set error to state
   * @param ref
   * @param out
   * may be null
   * @return
   * if not matched, return negative number
   */
  int match(DSState &state, StringRef ref, ArrayObject *out);

  /**
   * @param state
   * if has error, set error to state
   * @param value
   * if replace success, write result to it.
   * must be String
   * @param repl
   * replacing string
   * @return
   * if string creation failed, return false
   */
  bool replace(DSState &state, DSValue &value, StringRef repl);

  const std::string &getStr() const { return this->str; }
};

class ArrayObject : public ObjectWithRtti<ObjectKind::Array> {
private:
  std::vector<DSValue> values;

public:
  static constexpr size_t MAX_SIZE = INT32_MAX;

  using IterType = std::vector<DSValue>::const_iterator;

  explicit ArrayObject(const DSType &type) : ObjectWithRtti(type) {}

  ArrayObject(const DSType &type, std::vector<DSValue> &&values)
      : ArrayObject(type.typeId(), std::move(values)) {}

  ArrayObject(unsigned int typeID, std::vector<DSValue> &&values)
      : ObjectWithRtti(typeID), values(std::move(values)) {}

  const std::vector<DSValue> &getValues() const { return this->values; }

  std::vector<DSValue> &refValues() { return this->values; }

  size_t size() const { return this->values.size(); }

  std::string toString() const;
  bool opStr(StrBuilder &builder) const;

  void append(DSValue &&obj) { this->values.push_back(std::move(obj)); }

  void append(const DSValue &obj) { this->values.push_back(obj); }

  /**
   * append and check array size limit
   * @param state
   * @param obj
   * @return
   * if has error (reach array size limit), return false
   */
  [[nodiscard]] bool append(DSState &state, DSValue &&obj);

  DSValue takeFirst() {
    auto v = this->values.front();
    this->values.erase(values.begin());
    return v;
  }

  void sortAsStrArray(unsigned int beginOffset = 0) {
    std::sort(values.begin() + beginOffset, values.end(),
              [](const DSValue &x, const DSValue &y) { return x.asStrRef() < y.asStrRef(); });
  }
};

#define ASSERT_ARRAY_SIZE(obj) assert((obj).size() <= ArrayObject::MAX_SIZE)

struct KeyCompare {
  bool operator()(const DSValue &x, const DSValue &y) const { return x.equals(y); }
};

struct GenHash {
  std::size_t operator()(const DSValue &key) const { return key.hash(); }
};

using HashMap = std::unordered_map<DSValue, DSValue, GenHash, KeyCompare>;

class MapIterObject;

class MapObject : public ObjectWithRtti<ObjectKind::Map> {
private:
  friend class MapIterObject;

  HashMap valueMap;
  int lockCount{0};

public:
  explicit MapObject(const DSType &type) : ObjectWithRtti(type) {}

  MapObject(const DSType &type, HashMap &&map) : MapObject(type.typeId(), std::move(map)) {}

  MapObject(unsigned int typeID, HashMap &&map)
      : ObjectWithRtti(typeID), valueMap(std::move(map)) {}

  const HashMap &getValueMap() const { return this->valueMap; }

  bool locked() const { return this->lockCount > 0; }

  void clear() { this->valueMap.clear(); }

  /**
   *
   * @param key
   * @param value
   * @return
   * old element. if not found (first time insertion), return invalid
   */
  DSValue set(DSValue &&key, DSValue &&value) {
    auto pair = this->valueMap.emplace(std::move(key), value);
    if (pair.second) {
      return DSValue::createInvalid();
    }
    std::swap(pair.first->second, value);
    return std::move(value);
  }

  DSValue setIfNotFound(DSValue &&key, DSValue &&value) {
    auto pair = this->valueMap.emplace(std::move(key), std::move(value));
    return pair.first->second;
  }

  bool trySwap(const DSValue &key, DSValue &value) {
    auto ret = this->valueMap.find(key);
    if (ret != this->valueMap.end()) {
      std::swap(ret->second, value);
      return true;
    }
    return false;
  }

  bool remove(const DSValue &key) {
    auto ret = this->valueMap.find(key);
    if (ret == this->valueMap.end()) {
      return false;
    }
    this->valueMap.erase(ret);
    return true;
  }

  std::string toString() const;
  bool opStr(StrBuilder &builder) const;

  DSValue iter() { return DSValue::create<MapIterObject>(*this); }
};

class MapIterObject : public ObjectWithRtti<ObjectKind::MapIter> {
private:
  DSValue obj;
  HashMap::const_iterator iter;

public:
  explicit MapIterObject(MapObject &obj)
      : ObjectWithRtti(obj.getTypeID()), obj(&obj), iter(obj.getValueMap().begin()) {
    obj.lockCount++;
  }

  ~MapIterObject() { typeAs<MapObject>(this->obj).lockCount--; }

  bool hasNext() const { return this->iter != typeAs<MapObject>(this->obj).getValueMap().cend(); }

  DSValue next(TypePool &pool);
};

class BaseObject : public ObjectWithRtti<ObjectKind::Base> {
private:
  unsigned int fieldSize;
  DSValueBase fields[];

  BaseObject(const DSType &type, unsigned int size) : ObjectWithRtti(type), fieldSize(size) {
    for (unsigned int i = 0; i < this->fieldSize; i++) {
      new (&this->fields[i]) DSValue();
    }
  }

public:
  static BaseObject *create(const DSType &type, unsigned int size) {
    void *ptr = malloc(sizeof(BaseObject) + sizeof(DSValueBase) * size);
    return new (ptr) BaseObject(type, size);
  }

  /**
   * for tuple object construction
   * @param type
   * must be tuple type
   */
  static BaseObject *create(const TupleType &type) { return create(type, type.getFieldSize()); }

  static BaseObject *create(const RecordType &type) { return create(type, type.getFieldSize()); }

  ~BaseObject();

  DSValue &operator[](unsigned int index) { return static_cast<DSValue &>(this->fields[index]); }

  const DSValue &operator[](unsigned int index) const {
    return static_cast<const DSValue &>(this->fields[index]);
  }

  static void operator delete(void *ptr) noexcept { // NOLINT
    free(ptr);
  }

  unsigned int getFieldSize() const { return this->fieldSize; }

  // for tuple type
  bool opStrAsTupleRecord(StrBuilder &builder) const;
};

template <typename... Arg>
struct ObjectConstructor<BaseObject, Arg...> {
  static DSObject *construct(Arg &&...arg) { return BaseObject::create(std::forward<Arg>(arg)...); }
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
  DSValue message;
  DSValue name;
  std::vector<StackTraceElement> stackTrace;

public:
  ErrorObject(const DSType &type, DSValue &&message, DSValue &&name,
              std::vector<StackTraceElement> &&stackTrace)
      : ObjectWithRtti(type), message(std::move(message)), name(std::move(name)),
        stackTrace(std::move(stackTrace)) {}

  bool opStr(StrBuilder &builder) const;

  const DSValue &getMessage() const { return this->message; }

  const DSValue &getName() const { return this->name; }

  /**
   * print stack trace to stderr
   */
  void printStackTrace(DSState &ctx);

  const std::vector<StackTraceElement> &getStackTrace() const { return this->stackTrace; }

  /**
   * create new Error_Object and create stack trace
   */
  static DSValue newError(const DSState &state, const DSType &type, const DSValue &message) {
    return newError(state, type, DSValue(message));
  }

  static DSValue newError(const DSState &state, const DSType &type, DSValue &&message);
};

enum class CodeKind : unsigned char {
  TOPLEVEL,
  FUNCTION,
  USER_DEFINED_CMD,
  NATIVE,
};

struct DSCode {
  CodeKind codeKind;

  unsigned char localVarNum;

  unsigned short stackDepth;

  unsigned int size;

  unsigned char *code;

  const unsigned char *getCode() const { return this->code; }

  CodeKind getKind() const { return this->codeKind; }

  bool is(CodeKind kind) const { return this->getKind() == kind; }

  unsigned short getLocalVarNum() const { return this->localVarNum; }

  unsigned short getStackDepth() const { return this->stackDepth; }

  unsigned int getCodeSize() const { return this->size; }
};

class NativeCode : public DSCode {
public:
  using ArrayType = std::array<char, 8>;

private:
  ArrayType value;

public:
  NativeCode() noexcept {
    this->codeKind = CodeKind::NATIVE;
    this->localVarNum = 4;
    this->stackDepth = 4;
    this->size = 0;
  }

  explicit NativeCode(unsigned int index) noexcept : NativeCode() {
    this->value[0] = static_cast<char>(OpCode::CALL_BUILTIN);
    this->value[1] = index;
    this->value[2] = static_cast<char>(OpCode::RETURN);
    this->setCode();
  }

  explicit NativeCode(const ArrayType &value) noexcept : NativeCode() {
    this->value = value;
    this->setCode();
  }

  NativeCode(NativeCode &&o) noexcept : NativeCode() {
    this->value = o.value;
    this->setCode();
  }

  NON_COPYABLE(NativeCode);

  NativeCode &operator=(NativeCode &&o) noexcept {
    this->swap(o);
    return *this;
  }

  void swap(NativeCode &o) noexcept {
    this->value.swap(o.value);
    this->setCode();
    o.setCode();
  }

private:
  void setCode() { this->code = reinterpret_cast<unsigned char *>(this->value.data()); }
};

struct LineNumEntry {
  unsigned int address;
  unsigned int lineNum;

  explicit operator bool() const { return this->address != SYS_LIMIT_FUNC_LEN; }
};

struct ExceptionEntry {
  /**
   * if null, indicate sentinel
   */
  const DSType *type;

  unsigned int begin; // inclusive
  unsigned int end;   // exclusive
  unsigned int dest;  // catch block address

  unsigned short localOffset;
  unsigned short localSize;
};

class CompiledCode : public DSCode {
private:
  unsigned short belongedModId;

  /**
   * must not be null
   */
  char *name{nullptr};

  /**
   * last element is sentinel (nullptr)
   */
  DSValue *constPool{nullptr};

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

  CompiledCode(unsigned short modId, const std::string &name, DSCode code, DSValue *constPool,
               LineNumEntry *sourcePosEntries, ExceptionEntry *exceptionEntries) noexcept
      : DSCode(code), belongedModId(modId), name(strdup(name.c_str())), constPool(constPool),
        lineNumEntries(sourcePosEntries), exceptionEntries(exceptionEntries) {}

  CompiledCode(CompiledCode &&c) noexcept
      : DSCode(std::move(c)), belongedModId(c.belongedModId), name(c.name), constPool(c.constPool),
        lineNumEntries(c.lineNumEntries), exceptionEntries(c.exceptionEntries) {
    c.name = nullptr;
    c.code = nullptr;
    c.constPool = nullptr;
    c.lineNumEntries = nullptr;
    c.exceptionEntries = nullptr;
  }

  CompiledCode() noexcept : DSCode() { this->code = nullptr; }

  ~CompiledCode() {
    free(this->name);
    free(this->code);
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

  unsigned short getBelongedModId() const { return this->belongedModId; }

  StringRef getSourceName() const { return this->constPool[0].asStrRef(); }

  /**
   * must not be null.
   */
  const char *getName() const { return this->name; }

  const DSValue *getConstPool() const { return this->constPool; }

  const LineNumEntry *getLineNumEntries() const { return this->lineNumEntries; }

  unsigned int getLineNum(unsigned int index) const;

  const ExceptionEntry *getExceptionEntries() const { return this->exceptionEntries; }

  explicit operator bool() const noexcept { return this->code != nullptr; }
};

class FuncObject : public ObjectWithRtti<ObjectKind::Func> {
private:
  CompiledCode code;

public:
  FuncObject(const DSType &funcType, CompiledCode &&callable)
      : ObjectWithRtti(funcType), code(std::move(callable)) {}

  const CompiledCode &getCode() const { return this->code; }

  std::string toString() const;
};

class ClosureObject : public ObjectWithRtti<ObjectKind::Closure> {
private:
  ObjPtr<FuncObject> func;

  unsigned int upvarSize;
  DSValueBase upvars[];

  ClosureObject(ObjPtr<FuncObject> func, unsigned int size)
      : ObjectWithRtti(func->getTypeID()), func(std::move(func)), upvarSize(size) {
    for (unsigned int i = 0; i < this->upvarSize; i++) {
      new (&this->upvars[i]) DSValue();
    }
  }

public:
  static ClosureObject *create(ObjPtr<FuncObject> func, unsigned int size, const DSValue *values) {
    void *ptr = malloc(sizeof(ClosureObject) + sizeof(DSValueBase) * size);
    auto *closure = new (ptr) ClosureObject(std::move(func), size);
    for (unsigned int i = 0; i < size; i++) {
      (*closure)[i] = values[i];
    }
    return closure;
  }

  ~ClosureObject();

  const FuncObject &getFuncObj() const { return *this->func; }

  DSValue &operator[](unsigned int index) { return static_cast<DSValue &>(this->upvars[index]); }

  const DSValue &operator[](unsigned int index) const {
    return static_cast<const DSValue &>(this->upvars[index]);
  }

  static void operator delete(void *ptr) noexcept { // NOLINT
    free(ptr);
  }
};

template <typename... Arg>
struct ObjectConstructor<ClosureObject, Arg...> {
  static DSObject *construct(Arg &&...arg) {
    return ClosureObject::create(std::forward<Arg>(arg)...);
  }
};

class BoxObject : public ObjectWithRtti<ObjectKind::Box> {
private:
  DSValue value;

public:
  explicit BoxObject(DSValue &&value) : ObjectWithRtti(TYPE::Any), value(std::move(value)) {}

  const DSValue &getValue() const { return this->value; }

  void setValue(DSValue &&v) { this->value = std::move(v); }
};

class EnvCtxObject : public ObjectWithRtti<ObjectKind::EnvCtx> {
private:
  DSState &state;

  /**
   * maintains old env
   * first is env name
   * second is old value
   * if old value is invalid, unset env
   */
  std::vector<std::pair<DSValue, DSValue>> envs;

public:
  explicit EnvCtxObject(DSState &state) : ObjectWithRtti(TYPE::Any), state(state) {}

  ~EnvCtxObject();

  /**
   * save and set env
   * if name is IFS, also set and save global variable
   * @param name
   * @param value
   */
  void setAndSaveEnv(DSValue &&name, DSValue &&value);
};

} // namespace ydsh

#endif // YDSH_OBJECT_H
