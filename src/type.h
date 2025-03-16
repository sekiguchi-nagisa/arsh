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

#ifndef ARSH_TYPE_H
#define ARSH_TYPE_H

#include <array>
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "constant.h"
#include "handle_info.h"
#include "misc/buffer.hpp"
#include "misc/flag_util.hpp"
#include "misc/format.hpp"
#include "misc/noncopyable.h"
#include "misc/resource.hpp"
#include "misc/rtti.hpp"

namespace arsh {

enum class TYPE : unsigned int {
  Unresolved_, // for type error
  ProcGuard_,  // for guard parent code execution from child process

  Any,
  Void,
  Nothing,

  Value_, // super type of value type(int, float, bool, string). not directly use it.

  Int,
  Float,
  Bool,
  String,

  Regex,
  RegexMatch,
  Signal,
  Signals,
  Throwable,
  Error,
  Job,
  Jobs,
  Module,
  StringIter,
  FD,        // for Unix file descriptor
  ProcSubst, // for process substitution specific file descriptor
  Reader,
  Command,
  LineEditor,
  CLI,
  Candidates,
  StringArray, // for command argument
  OptNothing,  // for dummy invalid value

  ArithmeticError,
  OutOfRangeError,
  KeyNotFoundError,
  TypeCastError,
  SystemError, // for errno
  StackOverflowError,
  RegexSyntaxError,
  RegexMatchError,
  TildeError,
  GlobError,
  UnwrapError,
  IllegalAccessError,
  InvalidOperationError,
  ExecError,
  CLIError,
  ArgumentError,

  ShellExit_,
  AssertFail_,
};

#define EACH_TYPE_KIND(OP)                                                                         \
  OP(Function)                                                                                     \
  OP(Builtin)                                                                                      \
  OP(Array)                                                                                        \
  OP(Map)                                                                                          \
  OP(Tuple)                                                                                        \
  OP(Option)                                                                                       \
  OP(DerivedError)                                                                                 \
  OP(Record)                                                                                       \
  OP(CLIRecord)                                                                                    \
  OP(Mod)

enum class TypeKind : unsigned char {
#define GEN_ENUM(E) E,
  EACH_TYPE_KIND(GEN_ENUM)
#undef GEN_ENUM
};

class Handle;
class TypePool;
struct HandleRefCountOp;

using HandlePtr = IntrusivePtr<Handle, HandleRefCountOp>;

class Type {
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
      unsigned char v2_1;
      unsigned char v2_2;
    } u16_u8;
    struct {
      unsigned short v1;
      unsigned short v2;
    } u16_2;
  } meta{};

  const CStrPtr name;

  /**
   * if this type is Void or Any type, superType is null
   */
  const Type *superType;

  NON_COPYABLE(Type);

  /**
   * not directly call it.
   */
  Type(TypeKind k, unsigned int id, StringRef ref, const Type *superType)
      : tag(toUnderlying(k) << 24 | id), name(strdup(ref.data())), superType(superType) {}

  ~Type() = default;

public:
  void destroy();

  TypeKind typeKind() const { return static_cast<TypeKind>(this->tag >> 24); }

  StringRef getNameRef() const { return {this->name.get()}; }

  const char *getName() const { return this->name.get(); }

  unsigned int typeId() const { return this->tag & 0xFFFFFF; }

  bool is(TYPE type) const { return this->typeId() == toUnderlying(type); }

  bool isUnresolved() const { return this->is(TYPE::Unresolved_); }

  /**
   * if this type is VoidType, return true.
   */
  bool isVoidType() const { return this->is(TYPE::Void); }

  bool isNothingType() const { return this->is(TYPE::Nothing); }

  bool isBuiltinOrDerived() const {
    auto k = toUnderlying(this->typeKind());
    return k >= toUnderlying(TypeKind::Builtin) && k <= toUnderlying(TypeKind::Tuple);
  }

  bool isRecordOrDerived() const {
    auto k = toUnderlying(this->typeKind());
    return k >= toUnderlying(TypeKind::Record) && k <= toUnderlying(TypeKind::CLIRecord);
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

  bool isDerivedErrorType() const { return this->typeKind() == TypeKind::DerivedError; }

  bool isCLIRecordType() const { return this->typeKind() == TypeKind::CLIRecord; }

  bool isCollectionLike() const {
    return this->isArrayType() || this->isMapType() || this->isTupleType() ||
           this->isRecordOrDerived() || this->is(TYPE::RegexMatch) || this->is(TYPE::Candidates);
  }

  /**
   * get super type of this type.
   * return null, if has no super type(ex. AnyType, VoidType).
   */
  const Type *getSuperType() const { return this->superType; }

  HandlePtr lookupField(const TypePool &pool, const std::string &fieldName) const;

  void walkField(const TypePool &pool,
                 const std::function<bool(StringRef, const Handle &)> &walker) const;

  std::vector<const Type *> getTypeParams(const TypePool &pool) const;

  bool operator==(const Type &type) const {
    return reinterpret_cast<uintptr_t>(this) == reinterpret_cast<uintptr_t>(&type);
  }

  bool operator!=(const Type &type) const { return !(*this == type); }

  /**
   * check inheritance of target type.
   * if this type is equivalent to target type or
   * the super type of target type, return true.
   */
  bool isSameOrBaseTypeOf(const Type &targetType) const;

  /**
   *
   * @return
   * if user-defined error type or user-defined record type, return belonged (defined) module id
   * otherwise, return 0
   */
  ModId resolveBelongedModId() const;

  /**
   * if type is not number type, return -1.
   */
  int getNumTypeIndex() const {
    static_assert(toUnderlying(TYPE::Int) + 1 == toUnderlying(TYPE::Float));
    if (this->typeId() >= toUnderlying(TYPE::Int) && this->typeId() <= toUnderlying(TYPE::Float)) {
      return this->typeId() - toUnderlying(TYPE::Int);
    }
    return -1;
  }
};

#define EACH_HANDLE_KIND(OP)                                                                       \
  OP(VAR)         /* other variable except for bellow */                                           \
  OP(ENV)         /* environmental variable */                                                     \
  OP(FUNC)        /* function */                                                                   \
  OP(TYPE_ALIAS)  /* type alias */                                                                 \
  OP(NATIVE)      /* native method */                                                              \
  OP(METHOD)      /* user-defined method */                                                        \
  OP(CONSTRUCTOR) /* user-defined constructor */                                                   \
  OP(UDC)         /* user-defined command */                                                       \
  OP(NAMED_MOD)   /* module holder (named imported) */                                             \
  OP(GLOBAL_MOD)  /* module holder (global imported) */                                            \
  OP(INLINED_MOD) /* module holder (inlined imported) */                                           \
  OP(MOD_CONST)   /* module specific constant */                                                   \
  OP(SYS_CONST)   /* system specific constant */                                                   \
  OP(SMALL_CONST) /* small constant */

enum class HandleKind : unsigned char {
#define GEN_ENUM(E) E,
  EACH_HANDLE_KIND(GEN_ENUM)
#undef GEN_ENUM
};

const char *toString(HandleKind kind);

#define EACH_HANDLE_ATTR(OP)                                                                       \
  OP(READ_ONLY, (1u << 0u))                                                                        \
  OP(GLOBAL, (1u << 1u))                                                                           \
  OP(BOXED, (1u << 2u))                                                                            \
  OP(UPVAR, (1u << 3u))                                                                            \
  OP(UNCAPTURED, (1u << 4u))

enum class HandleAttr : unsigned char {
#define GEN_ENUM(E, V) E = (V),
  EACH_HANDLE_ATTR(GEN_ENUM)
#undef GEN_ENUM
};

std::string toString(HandleAttr attr);

template <>
struct allow_enum_bitop<HandleAttr> : std::true_type {};

class NameScope;

/**
 * represent for class field or variable. field type may be function type.
 */
class Handle {
private:
  friend class NameScope;
  friend struct HandleRefCountOp;

  int refCount{0};

  /**
   * |   24bit  |         8bit       |
   * |  TypeID  |  param size + recv |
   */
  const unsigned int tag{0};

  const unsigned int index;

  HandleKind kind;

  HandleAttr attribute;

  /**
   * if global module, id is 0.
   */
  const ModId modId;

protected:
  Handle(unsigned char fmaSize, unsigned int typeId, unsigned int index, HandleKind kind,
         HandleAttr attr, ModId modId)
      : tag(typeId << 8 | fmaSize), index(index), kind(kind), attribute(attr), modId(modId) {}

public:
  Handle(unsigned int typeId, unsigned int fieldIndex, HandleKind kind, HandleAttr attribute,
         ModId modId)
      : Handle(0, typeId, fieldIndex, kind, attribute, modId) {}

  Handle(const Type &fieldType, unsigned int fieldIndex, HandleKind kind, HandleAttr attribute,
         ModId modId = BUILTIN_MOD_ID)
      : Handle(fieldType.typeId(), fieldIndex, kind, attribute, modId) {}

  ~Handle() = default;

  unsigned int getTypeId() const { return this->tag >> 8; }

  bool isFuncHandle() const { return this->famSize() > 0 && this->is(HandleKind::FUNC); }

  bool isMethodHandle() const {
    return this->famSize() > 0 && (this->is(HandleKind::NATIVE) || this->is(HandleKind::METHOD) ||
                                   this->is(HandleKind::CONSTRUCTOR));
  }

  unsigned int getIndex() const { return this->index; }

  ModId getModId() const { return this->modId; }

  bool isVisibleInMod(ModId scopeModId, StringRef name) const {
    return isBuiltinMod(this->modId) || scopeModId == this->modId || name[0] != '_';
  }

  HandleKind getKind() const { return this->kind; }

  bool is(HandleKind k) const { return this->getKind() == k; }

  HandleAttr attr() const { return this->attribute; }

  bool has(HandleAttr a) const { return hasFlag(this->attr(), a); }

  bool isModHolder() const {
    return this->is(HandleKind::GLOBAL_MOD) || this->is(HandleKind::NAMED_MOD) ||
           this->is(HandleKind::INLINED_MOD);
  }

protected:
  unsigned char famSize() const { return static_cast<unsigned char>(this->tag & 0xFF); }

private:
  /**
   * only used from importForeignHandles
   * @param newKind
   */
  void setKind(HandleKind newKind) { this->kind = newKind; }

  void setAttr(HandleAttr newAttr) { this->attribute = newAttr; }

  /**
   * not directly use it
   */
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

struct CallSignature {
  const Type *returnType{nullptr};
  unsigned int paramSize{0};
  const Type *const *paramTypes{nullptr};
  const char *name{nullptr};
  const Handle *handle{nullptr};

  explicit CallSignature(const Type &retType) : returnType(&retType) {}

  CallSignature(const Type &retType, const char *name) : returnType(&retType), name(name) {}

  CallSignature(const Type &ret, unsigned int size, const Type *const *params, const char *name,
                const Handle *hd)
      : returnType(&ret), paramSize(size), paramTypes(params), name(name), handle(hd) {}
};

class PackedParamNames { // follow `Param0;Param1` form
private:
  CStrPtr value; // may be null (if no param)
  size_t len{0}; // not include sentinel (equivalent to strlen(value.get()))

public:
  PackedParamNames() = default;

  PackedParamNames(char *ptr, size_t len) : value(ptr), len(len) {}

  explicit PackedParamNames(const std::string &v) : value(strdup(v.c_str())), len(v.size()) {}

  size_t getLen() const { return this->len; }

  const char *getValue() const { return this->value.get(); }

  char *take() {
    this->len = 0;
    return this->value.release();
  }
};

class PackedParamNamesBuilder {
private:
  ByteBuffer buf;

public:
  explicit PackedParamNamesBuilder(unsigned int reservingSize) : buf(reservingSize) {}

  void addParamName(StringRef name) {
    if (!this->buf.empty()) {
      this->buf += ';';
    }
    this->buf.append(name.data(), name.size());
  }

  PackedParamNames build() && {
    PackedParamNames ret;
    if (!this->buf.empty()) {
      auto len = this->buf.size();
      this->buf += '\0';
      ret = PackedParamNames(std::move(this->buf).take(), len);
    }
    return ret;
  }
};

template <typename Func, enable_when<splitter_requirement_v<Func>> = nullptr>
bool iteratePackedParamNames(const StringRef packedParamNames, Func func) {
  return splitByDelim(packedParamNames, ';', func);
}

class FunctionType : public Type {
private:
  static_assert(sizeof(Type) <= 24);

  const Type &returnType;

  const Type *paramTypes[];

  FunctionType(unsigned int id, StringRef ref, const Type &superType, const Type &returnType,
               std::vector<const Type *> &&paramTypes)
      : Type(TypeKind::Function, id, ref, &superType), returnType(returnType) {
    this->meta.u32 = paramTypes.size();
    for (unsigned int i = 0; i < paramTypes.size(); i++) {
      this->paramTypes[i] = paramTypes[i];
    }
  }

public:
  static FunctionType *create(unsigned int id, StringRef ref, const Type &superType,
                              const Type &returnType, std::vector<const Type *> &&paramTypes) {
    void *ptr = operator new(sizeof(FunctionType) + sizeof(Type *) * paramTypes.size());
    return new (ptr) FunctionType(id, ref, superType, returnType, std::move(paramTypes));
  }

  void operator delete(void *ptr) { ::operator delete(ptr); }

  ~FunctionType() = default;

  const Type &getReturnType() const { return this->returnType; }

  /**
   * may be 0 if has no parameters
   */
  unsigned int getParamSize() const { return this->meta.u32; }

  const Type &getParamTypeAt(unsigned int index) const { return *this->paramTypes[index]; }

  CallSignature toCallSignature(const char *name, const Handle *hd) const {
    return {this->returnType, this->getParamSize(),
            this->getParamSize() == 0 ? nullptr : &this->paramTypes[0], name, hd};
  }

  static bool classof(const Type *type) { return type->isFuncType(); }
};

/**
 * builtin type(any, void, value ...)
 * not support override. (if override method, must override Object's method)
 * so this->getFieldSize is equivalent to superType->getFieldSize() + infoSize
 */
class BuiltinType : public Type {
protected:
  BuiltinType(TypeKind k, unsigned int id, StringRef ref, const Type *superType,
              native_type_info_t info)
      : Type(k, id, ref, superType) {
    this->meta.info = info;
  }

public:
  BuiltinType(unsigned int id, StringRef ref, const Type *superType, native_type_info_t info)
      : BuiltinType(TypeKind::Builtin, id, ref, superType, info) {}

  ~BuiltinType() = default;

  native_type_info_t getNativeTypeInfo() const { return this->meta.info; }

  static bool classof(const Type *type) { return type->isBuiltinOrDerived(); }
};

class ArrayType : public BuiltinType {
private:
  const Type &elementType;

public:
  ArrayType(unsigned int id, StringRef ref, native_type_info_t info, const Type &superType,
            const Type &type)
      : BuiltinType(TypeKind::Array, id, ref, &superType, info), elementType(type) {}

  const Type &getElementType() const { return this->elementType; }

  static bool classof(const Type *type) { return type->isArrayType(); }
};

class MapType : public BuiltinType {
private:
  const Type &keyType;
  const Type &valueType;

public:
  MapType(unsigned int id, StringRef ref, native_type_info_t info, const Type &superType,
          const Type &keyType, const Type &valueType)
      : BuiltinType(TypeKind::Map, id, ref, &superType, info), keyType(keyType),
        valueType(valueType) {}

  const Type &getKeyType() const { return this->keyType; }

  const Type &getValueType() const { return this->valueType; }

  static bool classof(const Type *type) { return type->isMapType(); }
};

class TupleType : public BuiltinType {
private:
  std::unordered_map<std::string, HandlePtr> fieldHandleMap;

public:
  /**
   * superType is AnyType ot VariantType
   */
  TupleType(unsigned int id, StringRef ref, native_type_info_t info, const Type &superType,
            std::vector<const Type *> &&types);

  const auto &getFieldHandleMap() const { return this->fieldHandleMap; }

  /**
   * return types.size()
   */
  unsigned int getFieldSize() const { return this->fieldHandleMap.size(); }

  HandlePtr lookupField(const std::string &fieldName) const;

  const Type &getFieldTypeAt(const TypePool &pool, unsigned int i) const;

  static bool classof(const Type *type) { return type->isTupleType(); }
};

class OptionType : public Type {
private:
  const Type &elementType;

public:
  OptionType(unsigned int id, StringRef ref, const Type &type)
      : Type(TypeKind::Option, id, ref, nullptr), elementType(type) {}

  const Type &getElementType() const { return this->elementType; }

  static bool classof(const Type *type) { return type->isOptionType(); }
};

class DerivedErrorType : public Type {
public:
  DerivedErrorType(unsigned int id, StringRef ref, const Type &superType)
      : Type(TypeKind::DerivedError, id, ref, &superType) {}

  static bool classof(const Type *type) { return type->isDerivedErrorType(); }
};

class RecordType : public Type {
private:
  friend class TypePool;

  /**
   * maintains field / type alias
   */
  std::unordered_map<std::string, HandlePtr> handleMap;

protected:
  RecordType(TypeKind k, unsigned int id, StringRef ref, const Type &superType)
      : Type(k, id, ref, &superType) {
    this->meta.u32 = 0;
  }

  void setExtraAttr(unsigned char attr) { this->meta.u16_u8.v2_2 = attr; }

  unsigned char getExtraAttr() const { return this->meta.u16_u8.v2_2; }

public:
  RecordType(unsigned int id, StringRef ref, const Type &superType)
      : RecordType(TypeKind::Record, id, ref, superType) {}

  const auto &getHandleMap() const { return this->handleMap; }

  unsigned int getFieldSize() const { return this->meta.u16_u8.v1; }

  bool isFinalized() const { return this->meta.u16_u8.v2_1 != 0; }

  HandlePtr lookupField(const std::string &fieldName) const;

  static bool classof(const Type *type) { return type->isRecordOrDerived(); }

protected:
  void finalize(unsigned char fieldSize, std::unordered_map<std::string, HandlePtr> &&handles) {
    this->handleMap = std::move(handles);
    this->meta.u16_u8.v1 = fieldSize;
    this->meta.u16_u8.v2_1 = 1; // finalize
  }
};

class ArgEntry;

class CLIRecordType : public RecordType {
public:
  enum class Attr : unsigned char {
    VERBOSE = 1u << 0u,    // verbose usage message
    TOPLEVEL = 1u << 1u,   // default cli name is toplevel arg0
    HAS_SUBCMD = 1u << 2u, // contains sub-commands
  };

private:
  friend class TypePool;

  std::string desc; // for description of cli
  std::vector<ArgEntry> entries;

public:
  CLIRecordType(unsigned int id, StringRef ref, const Type &superType, Attr attr,
                std::string &&desc);

  const auto &getDesc() const { return this->desc; }

  const auto &getEntries() const { return this->entries; }

  Attr getAttr() const { return static_cast<Attr>(this->getExtraAttr()); }

  std::pair<const CLIRecordType *, unsigned int> findSubCmdInfo(const TypePool &pool,
                                                                StringRef cmdName) const;

  static bool classof(const Type *type) { return type->isCLIRecordType(); }

private:
  void setAttr(Attr attr) { this->setExtraAttr(toUnderlying(attr)); }

  void finalizeArgEntries(std::vector<ArgEntry> &&args);
};

template <>
struct allow_enum_bitop<CLIRecordType::Attr> : std::true_type {};

enum class ImportedModKind : unsigned char {
  GLOBAL = 1u << 0u,
  INLINED = 1u << 1u,
};

template <>
struct allow_enum_bitop<ImportedModKind> : std::true_type {};

enum class ModAttr : unsigned char {
  HAS_ERRORS = 1u << 0u,  // there any errors in this module
  UNREACHABLE = 1u << 1u, // last statement in this module is Nothing type
};

template <>
struct allow_enum_bitop<ModAttr> : std::true_type {};

class ModType : public Type {
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

    Imported(const ModType &type, ImportedModKind k) : value(type.typeId() << 8 | toUnderlying(k)) {
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
  static_assert(sizeof(ModId) == sizeof(unsigned short));

  union {
    struct {
      unsigned short values[2]; // (index, ModAttr)
      Imported v[3];
    } e3;

    struct {
      unsigned short values[2]; // (index, ModAttr)
      Imported *ptr;
    } children;
  } data;

  /**
   * FieldHandle modId is equivalent to this.modId
   */
  std::unordered_map<std::string, HandlePtr> handleMap;

public:
  ModType(unsigned int id, const Type &superType, ModId modId,
          std::unordered_map<std::string, HandlePtr> &&handles, FlexBuffer<Imported> &&children,
          unsigned int index, ModAttr attr)
      : Type(TypeKind::Mod, id, toModTypeName(modId), &superType) {
    this->meta.u16_2.v1 = toUnderlying(modId);
    this->meta.u16_2.v2 = 0;
    this->data.e3.values[0] = index;
    this->reopen(std::move(handles), std::move(children), attr);
  }

  ~ModType();

  ModId getModId() const { return static_cast<ModId>(this->meta.u16_2.v1); }

  unsigned short getChildSize() const { return this->meta.u16_2.v2; }

  Imported getChildAt(unsigned int i) const {
    assert(i < this->getChildSize());
    return this->getChildSize() < 3 ? this->data.e3.v[i] : this->data.children.ptr[i];
  }

  /**
   * get module object index
   * @return
   */
  unsigned int getIndex() const { return this->data.e3.values[0]; }

  ModAttr getAttr() const { return static_cast<ModAttr>(this->data.e3.values[1]); }

  /**
   * for indicating module object index
   * @return
   */
  HandlePtr toAliasHandle(ModId importedModId) const {
    return HandlePtr::create(*this, this->getIndex(), HandleKind::VAR,
                             HandleAttr::READ_ONLY | HandleAttr::GLOBAL, importedModId);
  }

  HandlePtr toModHolder(ImportedModKind k, ModId importedModId) const {
    HandleAttr attr = HandleAttr::READ_ONLY | HandleAttr::GLOBAL;
    HandleKind kind = HandleKind::NAMED_MOD;
    if (hasFlag(k, ImportedModKind::INLINED)) {
      kind = HandleKind::INLINED_MOD;
    } else if (hasFlag(k, ImportedModKind::GLOBAL)) {
      kind = HandleKind::GLOBAL_MOD;
    }
    return HandlePtr::create(*this, this->getIndex(), kind, attr, importedModId);
  }

  Imported toModEntry(ImportedModKind k) const { return {*this, k}; }

  std::string toName() const { return this->getNameRef().toString(); }

  const auto &getHandleMap() const { return this->handleMap; }

  HandlePtr lookup(const TypePool &pool, const std::string &fieldName) const {
    return this->lookupImpl(pool, fieldName, false);
  }

  /**
   * for runtime symbol lookup
   * lookup all visible symbols at module (including globally imported symbols)
   * @param pool
   * @param name
   * @return
   */
  const Handle *lookupVisibleSymbolAtModule(const TypePool &pool, const std::string &name) const {
    if (auto handle = this->lookupImpl(pool, name, true); handle) {
      return handle.get();
    }
    return nullptr;
  }

  static bool classof(const Type *type) { return type->isModType(); }

private:
  HandlePtr find(const std::string &name) const {
    auto iter = this->handleMap.find(name);
    if (iter != this->handleMap.end()) {
      return iter->second;
    }
    return nullptr;
  }

  /**
   * lookup visible symbol at module
   * @param pool
   * @param name
   * @param searchGlobal
   * if true, also lookup globally imported modules
   * if false, only lookup from inlined imported modules
   * @return
   */
  HandlePtr lookupImpl(const TypePool &pool, const std::string &name, bool searchGlobal) const;

  void reopen(std::unordered_map<std::string, HandlePtr> &&handles, FlexBuffer<Imported> &&children,
              ModAttr attr) {
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
    this->data.e3.values[1] = toUnderlying(attr);
  }

  void disposeChildren() {
    if (this->getChildSize() >= 3) {
      free(this->data.children.ptr);
    }
  }
};

template <typename T, typename... Arg, enable_when<std::is_base_of_v<Type, T>> = nullptr>
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
public:
  enum class Kind : unsigned char {
#define GEN_ENUM(E) E,
    EACH_HANDLE_INFO_TYPE_TEMP(GEN_ENUM) EACH_HANDLE_INFO_FUNC_TYPE(GEN_ENUM)
#undef GEN_ENUM
  };

private:
  std::vector<const Type *> acceptableTypes;

  Kind kind{};

  native_type_info_t info{};

public:
  TypeTemplate() = default;

  TypeTemplate(Kind k, std::vector<const Type *> &&elementTypes, native_type_info_t info)
      : acceptableTypes(std::move(elementTypes)), kind(k), info(info) {}

  ~TypeTemplate() = default;

  bool operator==(const TypeTemplate &o) const { return this->kind == o.kind; }

  const char *getName() const;

  Kind getKind() const { return this->kind; }

  unsigned int getElementTypeSize() const { return this->acceptableTypes.size(); }

  native_type_info_t getInfo() const { return this->info; }

  const std::vector<const Type *> &getAcceptableTypes() const { return this->acceptableTypes; }
};

class FuncHandle : public Handle {
private:
  static_assert(sizeof(Handle) == 16);

  char packedParamNames[];

  FuncHandle(const FunctionType &funcType, unsigned int index, PackedParamNames &&packed,
             ModId modId)
      : Handle((funcType.getParamSize() ? 1 : 0) + 1, funcType.typeId(), index, HandleKind::FUNC,
               HandleAttr::GLOBAL | HandleAttr::READ_ONLY, modId) {
    if (packed.getLen()) {
      const char *ptr = packed.getValue();
      const auto len = packed.getLen();
      memcpy(this->packedParamNames, ptr, len);
      this->packedParamNames[len] = '\0';
    }
  }

public:
  NON_COPYABLE(FuncHandle);

  static std::unique_ptr<FuncHandle> create(const FunctionType &funcType, unsigned int index,
                                            PackedParamNames &&packed, ModId modId) {
    unsigned int famSize = packed.getLen();
    if (famSize) {
      famSize++; // reserve sentinel
    }
    void *ptr = operator new(sizeof(FuncHandle) + sizeof(char) * famSize);
    auto *handle = new (ptr) FuncHandle(funcType, index, std::move(packed), modId);
    return std::unique_ptr<FuncHandle>(handle);
  }

  void operator delete(void *ptr) { ::operator delete(ptr); }

  StringRef getPackedParamNames() const {
    StringRef ref;
    if (this->hasParams()) {
      ref = this->packedParamNames;
    }
    return ref;
  }

  static bool classof(const Handle *handle) { return handle->isFuncHandle(); }

private:
  bool hasParams() const { return this->famSize() - 1 > 0; }
};

class MethodHandle : public Handle {
private:
  static_assert(sizeof(Handle) == 16);

  char *packedParamNames{nullptr}; // may be null (if native method handle, always null)

  const Type &returnType;

  /**
   * not contains receiver type
   */
  const Type *paramTypes[];

  MethodHandle(const Type &recv, unsigned short index, const Type &ret, unsigned char paramSize,
               ModId modId, HandleKind hk)
      : Handle(paramSize + 1, recv.typeId(), index, hk, HandleAttr::GLOBAL | HandleAttr::READ_ONLY,
               modId),
        returnType(ret) {
    assert(paramSize <= SYS_LIMIT_METHOD_PARAM_NUM);
  }

  /**
   * create user-defined method or constructor handle
   * @param recv
   * @param index
   * @param ret
   * if null, indicate constructor
   * @param params
   * @param packed
   * @param modId
   * @return
   */
  static std::unique_ptr<MethodHandle> create(const Type &recv, unsigned int index, const Type *ret,
                                              const std::vector<const Type *> &params,
                                              PackedParamNames &&packed, ModId modId);

public:
  NON_COPYABLE(MethodHandle);

  ~MethodHandle();

  static std::unique_ptr<MethodHandle>
  native(const Type &recv, unsigned int index, const Type &ret, unsigned char paramSize,
         const std::array<const Type *, HandleInfoParamNumMax()> &paramTypes) {
    void *ptr = operator new(sizeof(MethodHandle) + sizeof(uintptr_t) * paramSize);
    auto *handle =
        new (ptr) MethodHandle(recv, index, ret, paramSize, BUILTIN_MOD_ID, HandleKind::NATIVE);
    for (unsigned int i = 0; i < static_cast<unsigned int>(paramSize); i++) {
      handle->paramTypes[i] = paramTypes[i];
    }
    return std::unique_ptr<MethodHandle>(handle);
  }

  static std::unique_ptr<MethodHandle> method(const Type &recv, unsigned int index, const Type &ret,
                                              const std::vector<const Type *> &params,
                                              PackedParamNames &&packed, ModId modId) {
    return create(recv, index, &ret, params, std::move(packed), modId);
  }

  /**
   * for constructor
   * @param recv
   * @param index
   * @param params
   * @param modId
   * @return
   */
  static std::unique_ptr<MethodHandle> constructor(const Type &recv, unsigned int index,
                                                   const std::vector<const Type *> &params,
                                                   PackedParamNames &&packed, ModId modId) {
    return create(recv, index, nullptr, params, std::move(packed), modId);
  }

  void operator delete(void *ptr) { ::operator delete(ptr); }

  const Type &getReturnType() const { return this->returnType; }

  unsigned int getRecvTypeId() const { return this->getTypeId(); }

  unsigned char getParamSize() const { return this->famSize() - 1; }

  const Type &getParamTypeAt(unsigned int index) const {
    assert(index < this->getParamSize());
    return *this->paramTypes[index];
  }

  bool isNative() const { return this->is(HandleKind::NATIVE); }

  bool isConstructor() const { return this->is(HandleKind::CONSTRUCTOR); }

  CallSignature toCallSignature(const char *name) const {
    return {this->returnType, this->getParamSize(),
            this->getParamSize() == 0 ? nullptr : &this->paramTypes[0], name, this};
  }

  /**
   * get packed parameter names like the following notation
   *   `p1;p2'
   * @return
   * if no param, return empty string
   */
  StringRef getPackedParamNames() const;

  static bool classof(const Handle *handle) { return handle->isMethodHandle(); }
};

} // namespace arsh

#endif // ARSH_TYPE_H
