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

class Handle;
class TypePool;

enum class TYPE : unsigned int {
  _Unresolved, // for type error
  _ProcGuard,  // for guard parent code execution from child process
  _Root,       // pseudo top type of all throwable type(except for option types)

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
  OP(Array)                                                                                        \
  OP(Map)                                                                                          \
  OP(Tuple)                                                                                        \
  OP(Option)                                                                                       \
  OP(Error)                                                                                        \
  OP(Record)                                                                                       \
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

  StringRef getNameRef() const { return {this->name.get()}; }

  const char *getName() const { return this->name.get(); }

  unsigned int typeId() const { return this->tag & 0xFFFFFF; }

  bool is(TYPE type) const { return this->typeId() == static_cast<unsigned int>(type); }

  bool isUnresolved() const { return this->is(TYPE::_Unresolved); }

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

  bool isArrayType() const { return this->typeKind() == TypeKind::Array; }

  bool isMapType() const { return this->typeKind() == TypeKind::Map; }

  bool isTupleType() const { return this->typeKind() == TypeKind::Tuple; }

  bool isOptionType() const { return this->typeKind() == TypeKind::Option; }

  bool isModType() const { return this->typeKind() == TypeKind::Mod; }

  bool isRecordType() const { return this->typeKind() == TypeKind::Record; }

  /**
   * get super type of this type.
   * return null, if has no super type(ex. AnyType, VoidType).
   */
  const DSType *getSuperType() const { return this->superType; }

  const Handle *lookupField(const TypePool &pool, const std::string &fieldName) const;

  void walkField(const TypePool &pool,
                 const std::function<bool(StringRef, const Handle &)> &walker) const;

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

#define EACH_HANDLE_ATTR(OP)                                                                       \
  OP(READ_ONLY, (1u << 0u))                                                                        \
  OP(GLOBAL, (1u << 1u))                                                                           \
  OP(ENV, (1u << 2u))                                                                              \
  OP(MOD_CONST, (1u << 3u))                                                                        \
  OP(TYPE_ALIAS, (1u << 4u))                                                                       \
  OP(NAMED_MOD, (1u << 5u))                                                                        \
  OP(GLOBAL_MOD, (1u << 6u))                                                                       \
  OP(INLINED_MOD, (1u << 7u))                                                                      \
  OP(NATIVE, (1u << 8u))

enum class HandleAttr : unsigned short {
#define GEN_ENUM(E, V) E = (V),
  EACH_HANDLE_ATTR(GEN_ENUM)
#undef GEN_ENUM
};

std::string toString(HandleAttr attr);

template <>
struct allow_enum_bitop<HandleAttr> : std::true_type {};

struct HandleRefCountOp;

/**
 * represent for class field or variable. field type may be function type.
 */
class Handle {
protected:
  friend struct HandleRefCountOp;

  int refCount{0};

  /**
   * |   24bit  |         8bit       |
   * |  TypeID  |  param size + recv |
   */
  unsigned int tag{0};

  unsigned int index;

  HandleAttr attribute;

  /**
   * if global module, id is 0.
   */
  unsigned short modId;

protected:
  Handle(unsigned char fmaSize, const DSType &type, unsigned int index, HandleAttr attr,
         unsigned short modId)
      : tag(type.typeId() << 8 | fmaSize), index(index), attribute(attr), modId(modId) {}

public:
  Handle(const DSType &fieldType, unsigned int fieldIndex, HandleAttr attribute)
      : Handle(fieldType, fieldIndex, attribute, 0) {}

  Handle(const DSType &fieldType, unsigned int fieldIndex, HandleAttr attribute,
         unsigned short modId)
      : Handle(0, fieldType, fieldIndex, attribute, modId) {}

  ~Handle() = default;

  unsigned int getTypeId() const { return this->tag >> 8; }

  bool isMethod() const { return this->famSize() > 0; }

  unsigned int getIndex() const { return this->index; }

  unsigned short getModId() const { return this->modId; }

  bool isVisibleInMod(unsigned short scopeModId, StringRef name) const {
    return this->modId == 0 || scopeModId == this->modId || name[0] != '_';
  }

  /**
   * normally unused
   * @param newAttr
   */
  void setAttr(HandleAttr newAttr) { this->attribute = newAttr; }

  HandleAttr attr() const { return this->attribute; }

  bool has(HandleAttr a) const { return hasFlag(this->attr(), a); }

  bool isModHolder() const {
    return !empty(this->attr() &
                  (HandleAttr::GLOBAL_MOD | HandleAttr::NAMED_MOD | HandleAttr::INLINED_MOD));
  }

protected:
  unsigned char famSize() const { return static_cast<unsigned char>(this->tag & 0xFF); }

private:
  void destroy();
};

struct HandleRefCountOp {
  static long useCount(const Handle *ptr) noexcept { return ptr->refCount; }

  static void increase(Handle *ptr) noexcept {
    if (ptr != nullptr) {
      ptr->refCount++;
    }
  }

  static void decrease(Handle *ptr) noexcept {
    if (ptr != nullptr && --ptr->refCount == 0) {
      ptr->destroy();
    }
  }
};

using HandlePtr = IntrusivePtr<Handle, HandleRefCountOp>;

struct CallableTypes {
  const DSType *returnType{nullptr};
  unsigned int paramSize{0};
  const DSType *const *paramTypes{nullptr};

  explicit CallableTypes(const DSType &retType) : returnType(&retType) {}

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
    return {this->returnType, this->getParamSize(),
            this->getParamSize() == 0 ? nullptr : &this->paramTypes[0]};
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

class ArrayType : public BuiltinType {
private:
  const DSType &elementType;

public:
  ArrayType(unsigned int id, StringRef ref, native_type_info_t info, const DSType &superType,
            const DSType &type)
      : BuiltinType(TypeKind::Array, id, ref, &superType, info), elementType(type) {}

  const DSType &getElementType() const { return this->elementType; }

  static bool classof(const DSType *type) { return type->isArrayType(); }
};

class MapType : public BuiltinType {
private:
  const DSType &keyType;
  const DSType &valueType;

public:
  MapType(unsigned int id, StringRef ref, native_type_info_t info, const DSType &superType,
          const DSType &keyType, const DSType &valueType)
      : BuiltinType(TypeKind::Map, id, ref, &superType, info), keyType(keyType),
        valueType(valueType) {}

  const DSType &getKeyType() const { return this->keyType; }

  const DSType &getValueType() const { return this->valueType; }

  static bool classof(const DSType *type) { return type->isMapType(); }
};

class TupleType : public BuiltinType {
private:
  std::unordered_map<std::string, Handle> fieldHandleMap;

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

  const Handle *lookupField(const std::string &fieldName) const;

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

class RecordType : public DSType {
private:
  friend class TypePool;

  /**
   * maintains field / type alias
   */
  std::unordered_map<std::string, HandlePtr> handleMap;

public:
  RecordType(unsigned int id, StringRef ref, const DSType &superType)
      : DSType(TypeKind::Record, id, ref, &superType) {
    this->meta.u32 = 0;
  }

  const auto &getHandleMap() const { return this->handleMap; }

  unsigned int getFieldSize() const { return this->meta.u16_2.v1; }

  bool isFinalized() const { return this->meta.u16_2.v2 != 0; }

  const Handle *lookupField(const std::string &fieldName) const;

  static bool classof(const DSType *type) { return type->typeKind() == TypeKind::Record; }

private:
  void finalize(const DSType &superType, unsigned char fieldSize,
                std::unordered_map<std::string, HandlePtr> &&handles) {
    this->handleMap = std::move(handles);
    this->meta.u16_2.v1 = fieldSize;
    this->meta.u16_2.v2 = 1; // finalize
    this->superType = &superType;
  }
};

enum class ImportedModKind : unsigned char {
  GLOBAL = 1u << 0u,
  INLINED = 1u << 1u,
};

template <>
struct allow_enum_bitop<ImportedModKind> : std::true_type {};

class ModType : public DSType {
public:
  friend class TypePool;

  class Imported {
  private:
    /**
     * | 24bit  | 8bit |
     * | TypeID | kind |
     */
    unsigned int value;

  public:
    Imported() = default;

    Imported(const ModType &type, ImportedModKind k)
        : value(type.typeId() << 8 | static_cast<unsigned char>(k)) {
      static_assert(sizeof(decltype(type.typeId())) == 4);
      static_assert(sizeof(k) == 1);
    }

    unsigned int typeId() const { return this->value >> 8; }

    ImportedModKind kind() const { return static_cast<ImportedModKind>(this->value & 0xFF); }

    bool isGlobal() const { return hasFlag(this->kind(), ImportedModKind::GLOBAL); }

    bool isInlined() const { return hasFlag(this->kind(), ImportedModKind::INLINED); }
  };

private:
  static_assert(sizeof(Imported) == 4, "failed!!");

  union {
    struct {
      unsigned int index;
      Imported v[3];
    } e3;

    struct {
      unsigned int index;
      Imported *ptr;
    } children;
  } data;

  /**
   * FieldHandle modId is equivalent to this.modId
   */
  std::unordered_map<std::string, HandlePtr> handleMap;

public:
  ModType(unsigned int id, const DSType &superType, unsigned short modID,
          std::unordered_map<std::string, HandlePtr> &&handles, FlexBuffer<Imported> &&children,
          unsigned int index)
      : DSType(TypeKind::Mod, id, toModTypeName(modID), &superType) {
    this->meta.u16_2.v1 = modID;
    this->meta.u16_2.v2 = 0;
    this->data.e3.index = index;
    this->reopen(std::move(handles), std::move(children));
  }

  ~ModType();

  unsigned short getModId() const { return this->meta.u16_2.v1; }

  bool isBuiltin() const { return this->getModId() == 0; }

  bool isRoot() const { return this->getModId() == 1; }

  unsigned short getChildSize() const { return this->meta.u16_2.v2; }

  Imported getChildAt(unsigned int i) const {
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
  HandlePtr toAliasHandle(unsigned short importedModId) const {
    return HandlePtr::create(*this, this->getIndex(), HandleAttr::READ_ONLY | HandleAttr::GLOBAL,
                             importedModId);
  }

  HandlePtr toModHolder(ImportedModKind k, unsigned short importedModId) const {
    HandleAttr attr = HandleAttr::NAMED_MOD;
    if (hasFlag(k, ImportedModKind::INLINED)) {
      attr = HandleAttr::INLINED_MOD;
    } else if (hasFlag(k, ImportedModKind::GLOBAL)) {
      attr = HandleAttr::GLOBAL_MOD;
    }
    setFlag(attr, HandleAttr::READ_ONLY | HandleAttr::GLOBAL);
    return HandlePtr::create(*this, this->getIndex(), attr, importedModId);
  }

  Imported toModEntry(ImportedModKind k) const { return {*this, k}; }

  std::string toName() const { return this->getNameRef().toString(); }

  const auto &getHandleMap() const { return this->handleMap; }

  const Handle *lookup(const TypePool &pool, const std::string &fieldName) const;

  /**
   * for runtime symbol lookup
   * lookup all visible symbols at module (including globally imported symbols)
   * @param pool
   * @param name
   * @return
   */
  const Handle *lookupVisibleSymbolAtModule(const TypePool &pool, const std::string &name) const;

  static bool classof(const DSType *type) { return type->isModType(); }

private:
  const Handle *find(const std::string &name) const {
    auto iter = this->handleMap.find(name);
    if (iter != this->handleMap.end()) {
      return iter->second.get();
    }
    return nullptr;
  }

  void reopen(std::unordered_map<std::string, HandlePtr> &&handles,
              FlexBuffer<Imported> &&children) {
    this->disposeChildren();
    this->handleMap = std::move(handles);
    this->meta.u16_2.v2 = children.size();
    if (this->getChildSize() < 3) {
      for (unsigned int i = 0; i < this->getChildSize(); i++) {
        this->data.e3.v[i] = children[i];
      }
    } else {
      this->data.children.ptr = children.take();
    }
  }

  void disposeChildren() {
    if (this->getChildSize() >= 3) {
      free(this->data.children.ptr);
    }
  }
};

template <typename T, typename... Arg, enable_when<std::is_base_of_v<DSType, T>> = nullptr>
inline T *constructType(Arg &&...arg) {
  if constexpr (std::is_same_v<FunctionType, T>) {
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

class MethodHandle : public Handle {
private:
  static_assert(sizeof(Handle) == 16);

  friend class TypePool;

  const DSType &returnType;

  /**
   * not contains receiver type
   */
  const DSType *paramTypes[];

  MethodHandle(const DSType &recv, unsigned short index, const DSType &ret, unsigned char paramSize,
               unsigned short modId)
      : Handle(paramSize + 1, recv, index, HandleAttr::GLOBAL | HandleAttr::READ_ONLY, modId),
        returnType(ret) {
    assert(paramSize <= SYS_LIMIT_METHOD_PARAM_NUM);
  }

  static std::unique_ptr<MethodHandle> create(const DSType &recv, unsigned int index,
                                              const DSType &ret, unsigned char paramSize) {
    void *ptr = malloc(sizeof(MethodHandle) + sizeof(uintptr_t) * paramSize);
    auto *handle = new (ptr) MethodHandle(recv, index, ret, paramSize, 0);
    setFlag(handle->attribute, HandleAttr::NATIVE);
    return std::unique_ptr<MethodHandle>(handle);
  }

public:
  NON_COPYABLE(MethodHandle);

  static void operator delete(void *ptr) noexcept { // NOLINT
    free(ptr);
  }

  static std::unique_ptr<MethodHandle> create(const DSType &recv, unsigned int index,
                                              const DSType &ret,
                                              const std::vector<const DSType *> &params,
                                              unsigned short modId);

  const DSType &getReturnType() const { return this->returnType; }

  unsigned int getRecvTypeId() const { return this->getTypeId(); }

  unsigned char getParamSize() const { return this->famSize() - 1; }

  const DSType &getParamTypeAt(unsigned int index) const {
    assert(index < this->getParamSize());
    return *this->paramTypes[index];
  }

  bool isNative() const { return this->has(HandleAttr::NATIVE); }

  CallableTypes toCallableTypes() const {
    return {this->returnType, this->getParamSize(),
            this->getParamSize() == 0 ? nullptr : &this->paramTypes[0]};
  }
};

} // namespace ydsh

#endif // YDSH_TYPE_H
