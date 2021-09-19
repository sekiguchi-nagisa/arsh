/*
 * Copyright (C) 2015-2021 Nagisa Sekiguchi
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

#ifndef YDSH_TYPE_H
#define YDSH_TYPE_H

#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "constant.h"
#include "handle_info.h"
#include "misc/buffer.hpp"
#include "misc/flag_util.hpp"
#include "misc/noncopyable.h"
#include "misc/resource.hpp"
#include "misc/rtti.hpp"
#include "misc/string_ref.hpp"

namespace ydsh {

class FieldHandle;
class TypePool;

enum class TYPE : unsigned int {
  _ProcGuard, // for guard parent code execution from child process
  _Root,      // pseudo top type of all throwable type(except for option types)

  Any,
  Void,
  Nothing,

  _Value, // super type of value type(int, float, bool, string). not directly used it.

  Int,
  Float,
  Boolean,
  String,

  Regex,
  Signal,
  Signals,
  Error,
  Job,
  Func,
  Module,
  StringIter,
  UnixFD,      // for Unix file descriptor
  StringArray, // for command argument

  ArithmeticError,
  OutOfRangeError,
  KeyNotFoundError,
  TypeCastError,
  SystemError, // for errno
  StackOverflowError,
  RegexSyntaxError,
  GlobbingError,
  UnwrappingError,
  IllegalAccessError,
  InvalidOperationError,

  /**
   * for internal status reporting.
   * they are pseudo type, so must not use it from shell
   */
  _InternalStatus, // base type
  _ShellExit,
  _AssertFail,
};

#define EACH_TYPE_KIND(OP)                                                                         \
  OP(Function)                                                                                     \
  OP(Builtin)                                                                                      \
  OP(Reified)                                                                                      \
  OP(Tuple)                                                                                        \
  OP(Option)                                                                                       \
  OP(Error)                                                                                        \
  OP(Mod)

enum class TypeKind : unsigned char {
#define GEN_ENUM(E) E,
  EACH_TYPE_KIND(GEN_ENUM)
#undef GEN_ENUM
};

class DSType {
protected:
  /**
   * |   8bit   | 24bit  |
   * | TypeKind | TypeID |
   */
  const unsigned int tag;

  union {
    native_type_info_t info;
    unsigned int u32;
    struct {
      unsigned short v1;
      unsigned short v2;
    } u16_2;
  } meta;

  const CStrPtr name;

  /**
   * if this type is Void or Any type, superType is null
   */
  const DSType *superType;

  NON_COPYABLE(DSType);

  /**
   * not directly call it.
   */
  DSType(TypeKind k, unsigned int id, StringRef ref, const DSType *superType)
      : tag(static_cast<unsigned int>(k) << 24 | id), name(strdup(ref.data())),
        superType(superType) {}

  ~DSType() = default;

public:
  void destroy();

  TypeKind typeKind() const { return static_cast<TypeKind>(this->tag >> 24); }

  StringRef getNameRef() const { return StringRef(this->name.get()); }

  const char *getName() const { return this->name.get(); }

  unsigned int typeId() const { return this->tag & 0xFFFFFF; }

  bool is(TYPE type) const { return this->typeId() == static_cast<unsigned int>(type); }

  /**
   * if this type is VoidType, return true.
   */
  bool isVoidType() const { return this->is(TYPE::Void); }

  bool isNothingType() const { return this->is(TYPE::Nothing); }

  bool isBuiltinOrDerived() const {
    auto k = this->typeKind();
    return static_cast<unsigned int>(k) >= static_cast<unsigned int>(TypeKind::Builtin) &&
           static_cast<unsigned int>(k) <= static_cast<unsigned int>(TypeKind::Tuple);
  }

  /**
   * if this type is FunctionType, return true.
   */
  bool isFuncType() const { return this->typeKind() == TypeKind::Function; }

  bool isReifiedType() const { return this->typeKind() == TypeKind::Reified; }

  bool isTupleType() const { return this->typeKind() == TypeKind::Tuple; }

  bool isOptionType() const { return this->typeKind() == TypeKind::Option; }

  bool isModType() const { return this->typeKind() == TypeKind::Mod; }

  /**
   * get super type of this type.
   * return null, if has no super type(ex. AnyType, VoidType).
   */
  const DSType *getSuperType() const { return this->superType; }

  const FieldHandle *lookupField(const std::string &fieldName) const;

  void walkField(std::function<bool(StringRef, const FieldHandle &)> &walker) const;

  std::vector<const DSType *> getTypeParams(const TypePool &pool) const;

  bool operator==(const DSType &type) const {
    return reinterpret_cast<uintptr_t>(this) == reinterpret_cast<uintptr_t>(&type);
  }

  bool operator!=(const DSType &type) const { return !(*this == type); }

  /**
   * check inheritance of target type.
   * if this type is equivalent to target type or
   * the super type of target type, return true.
   */
  bool isSameOrBaseTypeOf(const DSType &targetType) const;

  /**
   * if type is not number type, return -1.
   */
  int getNumTypeIndex() const {
    static_assert(static_cast<unsigned int>(TYPE::Int) + 1 ==
                  static_cast<unsigned int>(TYPE::Float));
    if (this->typeId() >= static_cast<unsigned int>(TYPE::Int) &&
        this->typeId() <= static_cast<unsigned int>(TYPE::Float)) {
      return this->typeId() - static_cast<unsigned int>(TYPE::Int);
    }
    return -1;
  }
};

#define EACH_FIELD_ATTR(OP)                                                                        \
  OP(READ_ONLY, (1u << 0u))                                                                        \
  OP(GLOBAL, (1u << 1u))                                                                           \
  OP(ENV, (1u << 2u))                                                                              \
  OP(RANDOM, (1u << 3u))                                                                           \
  OP(SECONDS, (1u << 4u))                                                                          \
  OP(MOD_CONST, (1u << 5u))                                                                        \
  OP(ALIAS, (1u << 6u))                                                                            \
  OP(NAMED_MOD, (1u << 7u))                                                                        \
  OP(GLOBAL_MOD, (1u << 8u))

enum class FieldAttribute : unsigned short {
#define GEN_ENUM(E, V) E = (V),
  EACH_FIELD_ATTR(GEN_ENUM)
#undef GEN_ENUM
};

std::string toString(FieldAttribute attr);

template <>
struct allow_enum_bitop<FieldAttribute> : std::true_type {};

/**
 * represent for class field or variable. field type may be function type.
 */
class FieldHandle {
private:
  /**
   * for safe module scope abort
   */
  unsigned int commitID;

  unsigned int typeID;

  unsigned int index;

  FieldAttribute attribute;

  /**
   * if global module, id is 0.
   */
  unsigned short modID;

  FieldHandle(unsigned int commitID, const DSType &fieldType, unsigned int fieldIndex,
              FieldAttribute attribute, unsigned short modID)
      : commitID(commitID), typeID(fieldType.typeId()), index(fieldIndex), attribute(attribute),
        modID(modID) {}

  FieldHandle(unsigned int commitID, const FieldHandle &handle, FieldAttribute newAttr,
              unsigned short modID)
      : commitID(commitID), typeID(handle.getTypeID()), index(handle.getIndex()),
        attribute(newAttr), modID(modID) {}

public:
  static FieldHandle alias(unsigned int commitID, const FieldHandle &handle, unsigned short modId) {
    return FieldHandle(commitID, handle, handle.attr() | FieldAttribute::ALIAS, modId);
  }

  static FieldHandle create(unsigned int commitID, const DSType &fieldType, unsigned int fieldIndex,
                            FieldAttribute attribute, unsigned short modID = 0) {
    return FieldHandle(commitID, fieldType, fieldIndex, attribute, modID);
  }

  ~FieldHandle() = default;

  unsigned int getCommitID() const { return this->commitID; }

  unsigned int getTypeID() const { return this->typeID; }

  unsigned int getIndex() const { return this->index; }

  FieldAttribute attr() const { return this->attribute; }

  unsigned short getModID() const { return this->modID; }
};

struct CallableTypes {
  const DSType *returnType{nullptr};
  unsigned int paramSize{0};
  const DSType *const *paramTypes{nullptr};

  CallableTypes() = default;

  CallableTypes(const DSType &ret, unsigned int size, const DSType *const *params)
      : returnType(&ret), paramSize(size), paramTypes(params) {}
};

class FunctionType : public DSType {
private:
  static_assert(sizeof(DSType) <= 24);

  const DSType &returnType;

  const DSType *paramTypes[];

  FunctionType(unsigned int id, StringRef ref, const DSType &superType, const DSType &returnType,
               std::vector<const DSType *> &&paramTypes)
      : DSType(TypeKind::Function, id, ref, &superType), returnType(returnType) {
    this->meta.u32 = paramTypes.size();
    for (unsigned int i = 0; i < paramTypes.size(); i++) {
      this->paramTypes[i] = paramTypes[i];
    }
  }

public:
  static FunctionType *create(unsigned int id, StringRef ref, const DSType &superType,
                              const DSType &returnType, std::vector<const DSType *> &&paramTypes) {
    void *ptr = malloc(sizeof(FunctionType) + sizeof(DSType *) * paramTypes.size());
    return new (ptr) FunctionType(id, ref, superType, returnType, std::move(paramTypes));
  }

  ~FunctionType() = default;

  const DSType &getReturnType() const { return this->returnType; }

  /**
   * may be 0 if has no parameters
   */
  unsigned int getParamSize() const { return this->meta.u32; }

  const DSType &getParamTypeAt(unsigned int index) const { return *this->paramTypes[index]; }

  CallableTypes toCallableTypes() const {
    return CallableTypes(this->returnType, this->getParamSize(),
                         this->getParamSize() == 0 ? nullptr : &this->paramTypes[0]);
  }

  static void operator delete(void *ptr) noexcept { // NOLINT
    free(ptr);
  }

  static bool classof(const DSType *type) { return type->isFuncType(); }
};

/**
 * builtin type(any, void, value ...)
 * not support override. (if override method, must override DSObject's method)
 * so this->getFieldSize is equivalent to superType->getFieldSize() + infoSize
 */
class BuiltinType : public DSType {
protected:
  BuiltinType(TypeKind k, unsigned int id, StringRef ref, const DSType *superType,
              native_type_info_t info)
      : DSType(k, id, ref, superType) {
    this->meta.info = info;
  }

public:
  BuiltinType(unsigned int id, StringRef ref, const DSType *superType, native_type_info_t info)
      : BuiltinType(TypeKind::Builtin, id, ref, superType, info) {}

  ~BuiltinType() = default;

  native_type_info_t getNativeTypeInfo() const { return this->meta.info; }

  static bool classof(const DSType *type) { return type->isBuiltinOrDerived(); }
};

/**
 * for Array, Map type
 */
class ReifiedType : public BuiltinType {
private:
  const unsigned int size;

  const DSType *elementTypes[];

  /**
   * super type is always AnyType
   */
  ReifiedType(unsigned int id, StringRef ref, native_type_info_t info, const DSType &superType,
              std::vector<const DSType *> &&elementTypes)
      : BuiltinType(TypeKind::Reified, id, ref, &superType, info), size(elementTypes.size()) {
    for (unsigned int i = 0; i < this->size; i++) {
      this->elementTypes[i] = elementTypes[i];
    }
  }

public:
  static ReifiedType *create(unsigned int id, StringRef ref, native_type_info_t info,
                             const DSType &superType, std::vector<const DSType *> &&elementTypes) {
    void *ptr = malloc(sizeof(ReifiedType) + sizeof(DSType *) * elementTypes.size());
    return new (ptr) ReifiedType(id, ref, info, superType, std::move(elementTypes));
  }

  ~ReifiedType() = default;

  unsigned int getElementSize() const { return this->size; }

  const DSType &getElementTypeAt(unsigned int index) const { return *this->elementTypes[index]; }

  static void operator delete(void *ptr) noexcept { // NOLINT
    free(ptr);
  }

  static bool classof(const DSType *type) { return type->isReifiedType(); }
};

class TupleType : public BuiltinType {
private:
  std::unordered_map<std::string, FieldHandle> fieldHandleMap;

public:
  /**
   * superType is AnyType ot VariantType
   */
  TupleType(unsigned int id, StringRef ref, native_type_info_t info, const DSType &superType,
            std::vector<const DSType *> &&types);

  const auto &getFieldHandleMap() const { return this->fieldHandleMap; }

  /**
   * return types.size()
   */
  unsigned int getFieldSize() const { return this->fieldHandleMap.size(); }

  const FieldHandle *lookupField(const std::string &fieldName) const;

  const DSType &getFieldTypeAt(const TypePool &pool, unsigned int i) const;

  static bool classof(const DSType *type) { return type->isTupleType(); }
};

class OptionType : public DSType {
private:
  const DSType &elementType;

public:
  OptionType(unsigned int id, StringRef ref, const DSType &type)
      : DSType(TypeKind::Option, id, ref, nullptr), elementType(type) {}

  const DSType &getElementType() const { return this->elementType; }

  static bool classof(const DSType *type) { return type->isOptionType(); }
};

class ErrorType : public DSType {
public:
  ErrorType(unsigned int id, StringRef ref, const DSType &superType)
      : DSType(TypeKind::Error, id, ref, &superType) {}

  static bool classof(const DSType *type) { return type->typeKind() == TypeKind::Error; }
};

struct ImportedModEntry {
  /**
   * indicating loaded mod type id.
   *
   * | 1bit |  31bit  |
   * | flag | type id |
   *
   * if flag is 1, indicate globally imported module
   */
  unsigned int value;

  bool isGlobal() const { return static_cast<int>(this->value) < 0; }

  unsigned int typeId() const { return this->value & 0x7FFFFFFF; }
};

class ModType : public DSType {
private:
  static_assert(sizeof(ImportedModEntry) == 4, "failed!!");

  union {
    struct {
      unsigned int index;
      ImportedModEntry v[3];
    } e3;

    struct {
      unsigned int index;
      ImportedModEntry *ptr;
    } children;
  } data;

  std::unordered_map<std::string, FieldHandle> handleMap;

public:
  ModType(unsigned int id, const DSType &superType, unsigned short modID,
          std::unordered_map<std::string, FieldHandle> &&handles,
          FlexBuffer<ImportedModEntry> &&children, unsigned int index)
      : DSType(TypeKind::Mod, id, toModTypeName(modID), &superType), handleMap(std::move(handles)) {
    this->meta.u16_2.v1 = modID;
    this->meta.u16_2.v2 = children.size();
    this->data.e3.index = index;
    if (this->getChildSize() < 3) {
      for (unsigned int i = 0; i < this->getChildSize(); i++) {
        this->data.e3.v[i] = children[i];
      }
    } else {
      this->data.children.ptr = children.take();
    }
  }

  ~ModType();

  unsigned short getModID() const { return this->meta.u16_2.v1; }

  unsigned short getChildSize() const { return this->meta.u16_2.v2; }

  ImportedModEntry getChildAt(unsigned int i) const {
    assert(i < this->getChildSize());
    return this->getChildSize() < 3 ? this->data.e3.v[i] : this->data.children.ptr[i];
  }

  /**
   * get module object index
   * @return
   */
  unsigned int getIndex() const { return this->data.e3.index; }

  /**
   * for indicating module object index
   * @return
   */
  FieldHandle toHandle() const {
    return FieldHandle::create(0, *this, this->getIndex(),
                               FieldAttribute::READ_ONLY | FieldAttribute::GLOBAL,
                               this->getModID());
  }

  FieldHandle toModHolder(bool global) const {
    return FieldHandle::create(
        0, *this, this->getIndex(),
        FieldAttribute::READ_ONLY | FieldAttribute::GLOBAL |
            (global ? FieldAttribute::GLOBAL_MOD : FieldAttribute::NAMED_MOD),
        this->getModID());
  }

  ImportedModEntry toModEntry(bool global) const {
    unsigned int value = this->typeId();
    if (global) {
      value |= static_cast<unsigned int>(1 << 31);
    }
    return ImportedModEntry{value};
  }

  std::string toName() const { return this->getNameRef().toString(); }

  const std::unordered_map<std::string, FieldHandle> &getHandleMap() const {
    return this->handleMap;
  }

  const FieldHandle *lookup(const std::string &fieldName) const;

  /**
   * for runtime symbol lookup
   * lookup all visible symbols at module (including globally imported symbols)
   * @param pool
   * @param name
   * @return
   */
  const FieldHandle *lookupVisibleSymbolAtModule(const TypePool &pool,
                                                 const std::string &name) const;

  static bool classof(const DSType *type) { return type->isModType(); }
};

template <typename T, typename... Arg, enable_when<std::is_base_of_v<DSType, T>> = nullptr>
inline T *constructType(Arg &&...arg) {
  if constexpr (std::is_same_v<FunctionType, T>) {
    return T::create(std::forward<Arg>(arg)...);
  } else if constexpr (std::is_same_v<ReifiedType, T>) {
    return T::create(std::forward<Arg>(arg)...);
  } else {
    return new T(std::forward<Arg>(arg)...);
  }
}

/**
 * ReifiedType template.
 */
class TypeTemplate {
private:
  std::string name;

  std::vector<const DSType *> acceptableTypes;

  native_type_info_t info;

public:
  TypeTemplate() = default;

  TypeTemplate(std::string &&name, std::vector<const DSType *> &&elementTypes,
               native_type_info_t info)
      : name(std::move(name)), acceptableTypes(std::move(elementTypes)), info(info) {}

  ~TypeTemplate() = default;

  bool operator==(const TypeTemplate &o) const { return this->name == o.name; }

  const std::string &getName() const { return this->name; }

  unsigned int getElementTypeSize() const { return this->acceptableTypes.size(); }

  native_type_info_t getInfo() const { return this->info; }

  const std::vector<const DSType *> &getAcceptableTypes() const { return this->acceptableTypes; }
};

class MethodHandle {
private:
  friend class TypePool;

  /**
   * for safe TypePool abort
   */
  const unsigned int methodId;

  const unsigned short methodIndex;

  const unsigned char paramSize;

  const bool native{true}; // currently, only support native method

  const DSType &returnType;

  const DSType &recvType;

  /**
   * not contains receiver type
   */
  const DSType *paramTypes[];

  MethodHandle(unsigned int id, const DSType &recv, unsigned short index, const DSType &ret,
               unsigned short paramSize)
      : methodId(id), methodIndex(index), paramSize(paramSize), returnType(ret), recvType(recv) {
    assert(paramSize <= SYS_LIMIT_METHOD_PARAM_NUM);
  }

  static std::unique_ptr<MethodHandle> create(unsigned int count, const DSType &recv,
                                              unsigned int index, const DSType &ret,
                                              unsigned int paramSize) {
    void *ptr = malloc(sizeof(MethodHandle) + sizeof(uintptr_t) * paramSize);
    auto *handle = new (ptr) MethodHandle(count, recv, index, ret, paramSize);
    return std::unique_ptr<MethodHandle>(handle);
  }

public:
  NON_COPYABLE(MethodHandle);

  static void operator delete(void *ptr) noexcept { // NOLINT
    free(ptr);
  }

  unsigned int getMethodId() const { return this->methodId; }

  unsigned short getMethodIndex() const { return this->methodIndex; }

  const DSType &getReturnType() const { return this->returnType; }

  const DSType &getRecvType() const { return this->recvType; }

  unsigned short getParamSize() const { return this->paramSize; }

  const DSType &getParamTypeAt(unsigned int index) const {
    assert(index < this->getParamSize());
    return *this->paramTypes[index];
  }

  bool isNative() const { return this->native; }

  CallableTypes toCallableTypes() const {
    return CallableTypes(this->returnType, this->getParamSize(),
                         this->getParamSize() == 0 ? nullptr : &this->paramTypes[0]);
  }
};

} // namespace ydsh

#endif // YDSH_TYPE_H
