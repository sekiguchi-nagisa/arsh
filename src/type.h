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
#include "misc/string_ref.hpp"

struct DSState;

namespace ydsh {

class FieldHandle;
class DSValue;
using native_func_t = DSValue (*)(DSState &);

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

enum class TypeAttr : unsigned char {
  EXTENDIBLE = 1u << 0u,
  FUNC_TYPE = 1u << 1u,    // function type
  RECORD_TYPE = 1u << 2u,  // indicate user defined type
  REIFIED_TYPE = 1u << 3u, // reified type (Array, Map, Tuple, Option)
  OPTION_TYPE = 1u << 4u,  // Option<T>
  MODULE_TYPE = 1u << 5u,  // Module type
};

template <>
struct allow_enum_bitop<TypeAttr> : std::true_type {};

class DSType {
protected:
  const CStrPtr name;

  unsigned int nameSize;

  /**
   * |   8bit   | 24bit  |
   * | TypeAttr | TypeID |
   */
  const unsigned int tag;

  /**
   * if this type is Void or Any type, superType is null
   */
  const DSType *superType;

public:
  NON_COPYABLE(DSType);

  /**
   * not directly call it.
   */
  DSType(unsigned int id, StringRef ref, const DSType *superType, TypeAttr attribute)
      : name(strdup(ref.data())), nameSize(ref.size()),
        tag(static_cast<unsigned int>(attribute) << 24 | id), superType(superType) {}

  virtual ~DSType() = default;

  StringRef getNameRef() const { return StringRef(this->name.get(), this->nameSize); }

  const char *getName() const { return this->name.get(); }

  unsigned int typeId() const { return this->tag & 0xFFFFFF; }

  bool is(TYPE type) const { return this->typeId() == static_cast<unsigned int>(type); }

  TypeAttr attr() const { return static_cast<TypeAttr>(this->tag >> 24); }

  /**
   * if true, can extend this type
   */
  bool isExtendible() const { return hasFlag(this->attr(), TypeAttr::EXTENDIBLE); }

  /**
   * if this type is VoidType, return true.
   */
  bool isVoidType() const { return this->is(TYPE::Void); }

  /**
   * if this type is FunctionType, return true.
   */
  bool isFuncType() const { return hasFlag(this->attr(), TypeAttr::FUNC_TYPE); }

  bool isRecordType() const { return hasFlag(this->attr(), TypeAttr::RECORD_TYPE); }

  bool isNothingType() const { return this->is(TYPE::Nothing); }

  bool isReifiedType() const { return hasFlag(this->attr(), TypeAttr::REIFIED_TYPE); }

  bool isOptionType() const { return hasFlag(this->attr(), TypeAttr::OPTION_TYPE); }

  bool isModType() const { return hasFlag(this->attr(), TypeAttr::MODULE_TYPE); }

  /**
   * get super type of this type.
   * return null, if has no super type(ex. AnyType, VoidType).
   */
  const DSType *getSuperType() const { return this->superType; }

  /**
   * get size of the all fields(include superType fieldSize).
   */
  virtual unsigned int getFieldSize() const;

  virtual const FieldHandle *lookupField(const std::string &fieldName) const;

  virtual void walkField(std::function<bool(StringRef, const FieldHandle &)> &walker) const;

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
  OP(FUNC_HANDLE, (1u << 3u))                                                                      \
  OP(RANDOM, (1u << 4u))                                                                           \
  OP(SECONDS, (1u << 5u))                                                                          \
  OP(MOD_CONST, (1u << 6u))                                                                        \
  OP(ALIAS, (1u << 7u))                                                                            \
  OP(NAMED_MOD, (1u << 8u))                                                                        \
  OP(GLOBAL_MOD, (1u << 9u))

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

public:
  FieldHandle(unsigned int commitID, const DSType &fieldType, unsigned int fieldIndex,
              FieldAttribute attribute, unsigned short modID = 0)
      : commitID(commitID), typeID(fieldType.typeId()), index(fieldIndex), attribute(attribute),
        modID(modID) {}

  FieldHandle(unsigned int commitID, const FieldHandle &handle, unsigned short modId)
      : commitID(commitID), typeID(handle.typeID), index(handle.index), attribute(handle.attribute),
        modID(modId) {}

  FieldHandle(unsigned int commitID, const FieldHandle &handle, FieldAttribute newAttr,
              unsigned short modID)
      : commitID(commitID), typeID(handle.getTypeID()), index(handle.getIndex()),
        attribute(newAttr), modID(modID) {}

  ~FieldHandle() = default;

  unsigned int getCommitID() const { return this->commitID; }

  unsigned int getTypeID() const { return this->typeID; }

  unsigned int getIndex() const { return this->index; }

  FieldAttribute attr() const { return this->attribute; }

  unsigned short getModID() const { return this->modID; }
};

class FunctionType : public DSType {
private:
  const DSType &returnType;

  /**
   * may be empty vector, if has no parameter
   */
  std::vector<const DSType *> paramTypes;

public:
  FunctionType(unsigned int id, StringRef ref, const DSType &superType, const DSType &returnType,
               std::vector<const DSType *> &&paramTypes)
      : DSType(id, ref, &superType, TypeAttr::FUNC_TYPE), returnType(returnType),
        paramTypes(std::move(paramTypes)) {}

  ~FunctionType() override = default;

  const DSType &getReturnType() const { return this->returnType; }

  /**
   * may be 0 if has no parameters
   */
  unsigned int getParamSize() const {
    return this->paramTypes.size();
  }

  const DSType &getParamTypeAt(unsigned int index) const {
    return *this->paramTypes[index];
  }
};

/**
 * for method handle creation.
 */
struct NativeFuncInfo {
  /**
   * if empty string, treat as constructor.
   */
  const char *funcName;

  /**
   * serialized function handle
   */
  const HandleInfo handleInfo[30];

  /**
   * bool func(RuntimeContext &ctx)
   */
  const native_func_t func_ptr;

  const bool hasRet;
};

const NativeFuncInfo *nativeFuncInfoTable();

struct native_type_info_t {
  unsigned short offset;

  unsigned short methodSize;

  unsigned int getActualMethodIndex(unsigned int index) const { return this->offset + index; }

  const NativeFuncInfo &getMethodInfo(unsigned int index) const {
    return nativeFuncInfoTable()[this->getActualMethodIndex(index)];
  }

  bool operator==(native_type_info_t info) const {
    return this->offset == info.offset && this->methodSize == info.methodSize;
  }

  bool operator!=(native_type_info_t info) const { return !(*this == info); }
};

/**
 * builtin type(any, void, value ...)
 * not support override. (if override method, must override DSObject's method)
 * so this->getFieldSize is equivalent to superType->getFieldSize() + infoSize
 */
class BuiltinType : public DSType {
protected:
  const native_type_info_t info;

public:
  BuiltinType(unsigned int id, StringRef ref, const DSType *superType, native_type_info_t info,
              TypeAttr attribute)
      : DSType(id, ref, superType, attribute), info(info) {}

  ~BuiltinType() override = default;

  native_type_info_t getNativeTypeInfo() const { return this->info; }
};

/**
 * not support override.
 */
class ReifiedType : public BuiltinType {
protected:
  /**
   * size is 1 or 2.
   */
  std::vector<const DSType *> elementTypes;

public:
  /**
   * super type is AnyType or null (if represents Option type)
   */
  ReifiedType(unsigned int id, StringRef ref, native_type_info_t info, const DSType *superType,
              std::vector<const DSType *> &&elementTypes, TypeAttr attribute = TypeAttr())
      : BuiltinType(id, ref, superType, info, attribute | TypeAttr::REIFIED_TYPE),
        elementTypes(std::move(elementTypes)) {}

  ~ReifiedType() override = default;

  const std::vector<const DSType *> &getElementTypes() const { return this->elementTypes; }

  unsigned int getElementSize() const {
    return this->elementTypes.size();
  }

  const DSType &getElementTypeAt(unsigned int index) const {
    return *this->elementTypes[index];
  }
};

class TupleType : public ReifiedType {
private:
  std::unordered_map<std::string, FieldHandle> fieldHandleMap;

public:
  /**
   * superType is AnyType ot VariantType
   */
  TupleType(unsigned int id, StringRef ref, native_type_info_t info, const DSType &superType,
            std::vector<const DSType *> &&types);

  /**
   * return types.size()
   */
  unsigned int getFieldSize() const override;

  const FieldHandle *lookupField(const std::string &fieldName) const override;

  void walkField(std::function<bool(StringRef, const FieldHandle &)> &walker) const override;
};

class ErrorType : public DSType {
public:
  ErrorType(unsigned int id, StringRef ref, const DSType &superType)
      : DSType(id, ref, &superType, TypeAttr::EXTENDIBLE) {}
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

  unsigned short modID;

  unsigned short childSize;

  unsigned int index; // module object index

  ImportedModEntry *children;

  std::unordered_map<std::string, FieldHandle> handleMap;

public:
  ModType(unsigned int id, const DSType &superType, unsigned short modID,
          std::unordered_map<std::string, FieldHandle> &&handles,
          FlexBuffer<ImportedModEntry> &&children, unsigned int index)
      : DSType(id, toModTypeName(modID), &superType, TypeAttr::MODULE_TYPE), modID(modID),
        index(index), handleMap(std::move(handles)) {
    this->childSize = children.size();
    this->children = children.take();
  }

  ~ModType() override;

  unsigned short getModID() const { return this->modID; }

  unsigned short getChildSize() const { return this->childSize; }

  ImportedModEntry getChildAt(unsigned int i) const {
    assert(i < this->childSize);
    return this->children[i];
  }

  unsigned int getIndex() const { return this->index; }

  /**
   * for indicating module object index
   * @return
   */
  FieldHandle toHandle() const {
    return FieldHandle(0, *this, this->index, FieldAttribute::READ_ONLY | FieldAttribute::GLOBAL,
                       this->modID);
  }

  FieldHandle toModHolder(bool global) const {
    return FieldHandle(0, *this, this->index,
                       FieldAttribute::READ_ONLY | FieldAttribute::GLOBAL |
                           (global ? FieldAttribute::GLOBAL_MOD : FieldAttribute::NAMED_MOD),
                       this->modID);
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

  const FieldHandle *lookupField(const std::string &fieldName) const override;

  void walkField(std::function<bool(StringRef, const FieldHandle &)> &walker) const override;

  /**
   * for runtime symbol lookup
   * lookup all visible symbols at module (including globally imported symbols)
   * @param pool
   * @param name
   * @return
   */
  const FieldHandle *lookupVisibleSymbolAtModule(const TypePool &pool,
                                                 const std::string &name) const;
};

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

  const bool native{true}; // currently only support native method

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
};

} // namespace ydsh

#endif // YDSH_TYPE_H
