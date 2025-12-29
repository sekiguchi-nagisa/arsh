/*
 * Copyright (C) 2020 Nagisa Sekiguchi
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

#ifndef ARSH_TYPE_POOL_H
#define ARSH_TYPE_POOL_H

#include "constant.h"
#include "misc/buffer.hpp"
#include "misc/result.hpp"
#include "misc/string_ref.hpp"
#include "tlerror.h"
#include "type.h"

namespace arsh {

using TypeOrError = Result<const Type *, std::unique_ptr<TypeLookupError>>;

struct TypeDiscardPoint {
  unsigned int typeIdOffset;
  unsigned int methodIdOffset;
};

class TypePool {
public:
  struct Key {
    unsigned int id;
    StringRef ref;

    Key(const Type &recv, StringRef ref) : id(recv.typeId()), ref(ref) {}

    bool operator==(const Key &key) const { return this->id == key.id && this->ref == key.ref; }
  };

  struct Hash {
    std::size_t operator()(const Key &key) const {
      auto hash = FNVHash::compute(key.ref.begin(), key.ref.end());
      char bb[4];
      static_assert(sizeof(key.id) == sizeof(unsigned int));
      memcpy(bb, &key.id, sizeof(unsigned int));
      for (auto b : bb) {
        FNVHash::update(hash, b);
      }
      return hash;
    }
  };

  class Value {
  private:
    union {
      MethodHandle *handle_;
      uintptr_t index_;
    };

    bool alloc{false};
    unsigned int commitId_{0}; // for discard

  public:
    NON_COPYABLE(Value);

    Value() : index_(0) {}

    explicit Value(unsigned int index) : index_(index) {}

    /**
     *
     * @param commitId
     * @param ptr
     * must not be null
     */
    explicit Value(unsigned int commitId, MethodHandle *ptr)
        : handle_(ptr), alloc(true), commitId_(commitId) {}

    Value(Value &&v) noexcept : index_(v.index_), alloc(v.alloc), commitId_(v.commitId_) {
      v.alloc = false;
    }

    ~Value() {
      if (*this) {
        delete this->handle_;
      }
    }

    Value &operator=(Value &&v) noexcept {
      if (this != std::addressof(v)) {
        this->~Value();
        new (this) Value(std::move(v));
      }
      return *this;
    }

    explicit operator bool() const { return this->alloc; }

    unsigned int index() const { return this->index_; }

    const MethodHandle *handle() const { return this->handle_; }

    unsigned int commitId() const { return this->commitId_; }
  };

private:
  unsigned int methodIdCount{0};
  FlexBuffer<Type *> typeTable;
  StrRefMap<unsigned int> nameMap;

  // for reified type
  TypeTemplate arrayTemplate;
  TypeTemplate mapTemplate;
  TypeTemplate tupleTemplate;  // pseudo template
  TypeTemplate optionTemplate; // pseudo template
  TypeTemplate funcTemplate;   // pseudo template

  /**
   * for type template
   */
  StrRefMap<const TypeTemplate *> templateMap;

  using MethodMap = std::unordered_map<Key, Value, Hash>;

  MethodMap methodMap;

  static constexpr unsigned int MAX_TYPE_NUM = SYS_LIMIT_TYPE_ID;

public:
  NON_COPYABLE(TypePool);

  TypePool();
  ~TypePool();

  const Type &get(TYPE t) const { return this->get(toUnderlying(t)); }

  const Type &get(unsigned int index) const { return *this->typeTable[index]; }

  const Type &getUnresolvedType() const { return this->get(TYPE::Unresolved_); }

  /**
   * return null, if has no type.
   */
  const Type *getType(StringRef typeName) const;

  const TypeTemplate &getArrayTemplate() const { return this->arrayTemplate; }

  const TypeTemplate &getMapTemplate() const { return this->mapTemplate; }

  const TypeTemplate &getTupleTemplate() const { return this->tupleTemplate; }

  const TypeTemplate &getOptionTemplate() const { return this->optionTemplate; }

  const TypeTemplate *getTypeTemplate(StringRef typeName) const;

  const auto &getTemplateMap() const { return this->templateMap; }

  /**
   * resolve common super type from `types`
   * if `types` follow [T, Nothing?]
   *   if T is Void => Void
   *   if T is U? => U?
   *   otherwise => T?
   * @param types
   * after called, may be modified
   * @return
   * if not resolve common support type, return null
   */
  const Type *resolveCommonSuperType(std::vector<const Type *> &types);

  TypeOrError createReifiedType(const TypeTemplate &typeTemplate,
                                std::vector<const Type *> &&elementTypes);

  TypeOrError createArrayType(const Type &elementType) {
    return this->createReifiedType(this->getArrayTemplate(), {&elementType});
  }

  TypeOrError createMapType(const Type &keyType, const Type &valueType) {
    return this->createReifiedType(this->getMapTemplate(), {&keyType, &valueType});
  }

  TypeOrError createOptionType(const Type &elementType) {
    return this->createOptionType({&elementType});
  }

  TypeOrError createTupleType(std::vector<const Type *> &&elementTypes);

  /**
   *
   * @param returnType
   * @param paramTypes
   * @return
   * must be FunctionType
   */
  TypeOrError createFuncType(const Type &returnType, std::vector<const Type *> &&paramTypes);

  /**
   * create new error type.
   * created error type has error context (ex.  stacktrace)
   * @param typeName
   * @param superType
   * must be subtype of Error type
   * @param belongedModId
   * @return
   */
  TypeOrError createErrorType(const std::string &typeName, const Type &superType,
                              ModId belongedModId);

  TypeOrError createRecordType(const std::string &typeName, ModId belongedModId);

  TypeOrError finalizeRecordType(const RecordType &recordType,
                                 std::unordered_map<std::string, HandlePtr> &&handles);

  TypeOrError createCLIRecordType(const std::string &typeName, ModId belongedModId,
                                  CLIRecordType::Attr attr, std::string &&desc);

  TypeOrError finalizeCLIRecordType(const CLIRecordType &recordType,
                                    std::unordered_map<std::string, HandlePtr> &&handles,
                                    std::vector<ArgEntry> &&entries);

  const ModType &createModType(ModId modId, std::unordered_map<std::string, HandlePtr> &&handles,
                               FlexBuffer<ModType::Imported> &&children, unsigned int index,
                               ModAttr modAttr);

  const ModType &getBuiltinModType() const {
    auto *type = this->getModTypeById(BUILTIN_MOD_ID);
    assert(type && type->isModType());
    return *type;
  }

  const ModType *getModTypeById(ModId modId) const;

  const MethodHandle *lookupMethod(const Type &recvType, const std::string &methodName);

  /**
   * instantiate native method handle from native method index
   * normally unused (for method name completion)
   * @param recv
   * @param methodIndex
   * must be valid method handle index of receiver type
   * @return
   * if failed, return null
   * // TODO: error reporting
   */
  std::unique_ptr<MethodHandle> allocNativeMethodHandle(const Type &recv, unsigned int methodIndex);

  bool hasMethod(const Type &recvType, const std::string &methodName) const;

  const MethodMap &getMethodMap() const { return this->methodMap; }

  const FlexBuffer<Type *> &getTypeTable() const { return this->typeTable; }

  TypeDiscardPoint getDiscardPoint() const {
    return TypeDiscardPoint{
        .typeIdOffset = this->typeTable.size(),
        .methodIdOffset = this->methodIdCount,
    };
  }

  void discard(TypeDiscardPoint point);

private:
  const Type *get(StringRef typeName) const {
    auto iter = this->nameMap.find(typeName);
    if (iter == this->nameMap.end()) {
      return nullptr;
    }
    return &this->get(iter->second);
  }

  Type *getMut(unsigned int typeId) const { return this->typeTable[typeId]; }

  template <typename T, typename... A>
  T *newType(A &&...arg) {
    unsigned int id = this->typeTable.size();
    return static_cast<T *>(this->addType(constructType<T>(id, std::forward<A>(arg)...)));
  }

  /**
   *
   * @param type
   * must not be null
   * @return
   * if type is already defined, return null
   */
  Type *addType(Type *type);

  /**
   * create reified type name
   * equivalent to toReifiedTypeName(typeTemplate->getName(), elementTypes)
   */
  std::string toReifiedTypeName(const TypeTemplate &typeTemplate,
                                const std::vector<const Type *> &elementTypes) const;

  static std::string toTupleTypeName(const std::vector<const Type *> &elementTypes);

  /**
   * create function type name
   */
  static std::string toFunctionTypeName(const Type &returnType,
                                        const std::vector<const Type *> &paramTypes);

  TypeOrError createOptionType(std::vector<const Type *> &&elementTypes);

  /**
   * @param t
   * @param elementTypes
   * @param limit
   * @return
   * if success, return null
   */
  static TypeOrError checkElementTypes(const TypeTemplate &t,
                                       const std::vector<const Type *> &elementTypes, size_t limit);

  /**
   *
   * @param t
   * @param elementTypes
   * @return
   * if success, return null
   */
  static TypeOrError checkElementTypes(const TypeTemplate &t,
                                       const std::vector<const Type *> &elementTypes);

  void initBuiltinType(TYPE t, const char *typeName, native_type_info_t info) {
    this->initBuiltinType(t, typeName, nullptr, info);
  }

  void initBuiltinType(TYPE t, const char *typeName, TYPE super, native_type_info_t info) {
    this->initBuiltinType(t, typeName, &this->get(super), info);
  }

  void initBuiltinType(TYPE t, const char *typeName, const Type *super, native_type_info_t info);

  void initTypeTemplate(TypeTemplate &temp, TypeTemplate::Kind kind,
                        std::vector<const Type *> &&elementTypes, native_type_info_t info);

  void initDerivedErrorType(TYPE t, const char *typeName);

  void registerHandle(const BuiltinType &recv, const char *name, unsigned int index);

  void registerHandles(const BuiltinType &type);
};

} // namespace arsh

#endif // ARSH_TYPE_POOL_H
