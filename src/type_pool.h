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

#ifndef YDSH_TYPE_POOL_H
#define YDSH_TYPE_POOL_H

#include "constant.h"
#include "misc/buffer.hpp"
#include "misc/result.hpp"
#include "misc/string_ref.hpp"
#include "tlerror.h"
#include "type.h"

namespace ydsh {

using TypeOrError = Result<const DSType *, std::unique_ptr<TypeLookupError>>;

struct TypeDiscardPoint {
  unsigned int typeIdOffset;
  unsigned int methodIdOffset;
};

class TypePool {
public:
  struct Key {
    unsigned int id;
    StringRef ref;

    Key(const DSType &recv, StringRef ref) : id(recv.typeId()), ref(ref) {}

    void dispose() { free(const_cast<char *>(this->ref.take())); }

    bool operator==(const Key &key) const { return this->id == key.id && this->ref == key.ref; }
  };

  struct Hash {
    std::size_t operator()(const Key &key) const {
      auto hash = FNVHash::compute(key.ref.begin(), key.ref.end());
      union {
        char b[4];
        unsigned int i;
      } wrap = {.i = key.id};
      for (auto b : wrap.b) {
        FNVHash::update(hash, b);
      }
      return hash;
    }
  };

  class Value {
  private:
    static constexpr uint64_t TAG = static_cast<uint64_t>(1) << 63;

    union {
      MethodHandle *handle_;
      int64_t index_;
    };

    unsigned int commitId_{0}; // for discard

  public:
    NON_COPYABLE(Value);

    Value() : index_(0) {}

    explicit Value(unsigned int index) : index_(index | TAG) {}

    /**
     *
     * @param commitId
     * @param ptr
     * must not be null
     */
    explicit Value(unsigned int commitId, MethodHandle *ptr) : index_(0), commitId_(commitId) {
      this->handle_ = ptr;
    }

    Value(Value &&v) noexcept : index_(v.index_), commitId_(v.commitId_) { v.index_ = 0; }

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

    explicit operator bool() const { return this->index_ > 0; }

    unsigned int index() const { return ~TAG & this->index_; }

    const MethodHandle *handle() const { return this->handle_; }

    unsigned int commitId() const { return this->commitId_; }
  };

private:
  unsigned int methodIdCount{0};
  FlexBuffer<DSType *> typeTable;
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

  const DSType &get(TYPE t) const { return this->get(toUnderlying(t)); }

  const DSType &get(unsigned int index) const { return *this->typeTable[index]; }

  const DSType &getUnresolvedType() const { return this->get(TYPE::Unresolved_); }

  /**
   * return null, if has no type.
   */
  const DSType *getType(StringRef typeName) const;

  const TypeTemplate &getArrayTemplate() const { return this->arrayTemplate; }

  const TypeTemplate &getMapTemplate() const { return this->mapTemplate; }

  const TypeTemplate &getTupleTemplate() const { return this->tupleTemplate; }

  const TypeTemplate &getOptionTemplate() const { return this->optionTemplate; }

  const TypeTemplate *getTypeTemplate(StringRef typeName) const;

  const auto &getTemplateMap() const { return this->templateMap; }

  TypeOrError createReifiedType(const TypeTemplate &typeTemplate,
                                std::vector<const DSType *> &&elementTypes);

  TypeOrError createArrayType(const DSType &elementType) {
    return this->createReifiedType(this->getArrayTemplate(), {&elementType});
  }

  TypeOrError createMapType(const DSType &keyType, const DSType &valueType) {
    return this->createReifiedType(this->getMapTemplate(), {&keyType, &valueType});
  }

  TypeOrError createOptionType(const DSType &elementType) {
    return this->createOptionType({&elementType});
  }

  TypeOrError createOptionType(std::vector<const DSType *> &&elementTypes);

  TypeOrError createTupleType(std::vector<const DSType *> &&elementTypes);

  /**
   *
   * @param returnType
   * @param paramTypes
   * @return
   * must be FunctionType
   */
  TypeOrError createFuncType(const DSType &returnType, std::vector<const DSType *> &&paramTypes);

  /**
   * create new error type.
   * created error type has error context (ex.  stacktrace)
   * @param typeName
   * @param superType
   * must be subtype of Error type
   * @return
   */
  TypeOrError createErrorType(const std::string &typeName, const DSType &superType,
                              ModId belongedModId);

  TypeOrError createRecordType(const std::string &typeName, ModId belongedModId);

  TypeOrError finalizeRecordType(const RecordType &recordType,
                                 std::unordered_map<std::string, HandlePtr> &&handles);

  TypeOrError createCLIRecordType(const std::string &typeName, ModId belongedModId);

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

  const MethodHandle *lookupMethod(const DSType &recvType, const std::string &methodName);

  /**
   * instantiate native method handle from native method index
   * normally unused (for method name completion)
   * @param recv
   * @param methodIndex
   * must be valid method handle index of receiver type
   * @return
   * if failed, return null
   * // FIXME: error reporting
   */
  std::unique_ptr<MethodHandle> allocNativeMethodHandle(const DSType &recv,
                                                        unsigned int methodIndex);

  bool hasMethod(const DSType &recvType, const std::string &methodName) const;

  const MethodMap &getMethodMap() const { return this->methodMap; }

  const FlexBuffer<DSType *> &getTypeTable() const { return this->typeTable; }

  TypeDiscardPoint getDiscardPoint() const {
    return TypeDiscardPoint{
        .typeIdOffset = this->typeTable.size(),
        .methodIdOffset = this->methodIdCount,
    };
  }

  void discard(TypeDiscardPoint point);

private:
  const DSType *get(StringRef typeName) const {
    auto iter = this->nameMap.find(typeName);
    if (iter == this->nameMap.end()) {
      return nullptr;
    }
    return &this->get(iter->second);
  }

  DSType *getMut(unsigned int typeId) const { return this->typeTable[typeId]; }

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
  DSType *addType(DSType *type);

  /**
   * create reified type name
   * equivalent to toReifiedTypeName(typeTemplate->getName(), elementTypes)
   */
  std::string toReifiedTypeName(const TypeTemplate &typeTemplate,
                                const std::vector<const DSType *> &elementTypes) const;

  static std::string toTupleTypeName(const std::vector<const DSType *> &elementTypes);

  /**
   * create function type name
   */
  static std::string toFunctionTypeName(const DSType &returnType,
                                        const std::vector<const DSType *> &paramTypes);

  /**
   *
   * @param elementTypes
   * @return
   * if success, return null
   */
  static TypeOrError checkElementTypes(const TypeTemplate &t,
                                       const std::vector<const DSType *> &elementTypes,
                                       size_t limit);

  /**
   *
   * @param t
   * @param elementTypes
   * @return
   * if success, return null
   */
  static TypeOrError checkElementTypes(const TypeTemplate &t,
                                       const std::vector<const DSType *> &elementTypes);

  void initBuiltinType(TYPE t, const char *typeName, bool extendible, native_type_info_t info) {
    this->initBuiltinType(t, typeName, extendible, nullptr, info);
  }

  void initBuiltinType(TYPE t, const char *typeName, bool extendible, TYPE super,
                       native_type_info_t info) {
    this->initBuiltinType(t, typeName, extendible, &this->get(super), info);
  }

  void initBuiltinType(TYPE t, const char *typeName, bool extendible, const DSType *super,
                       native_type_info_t info);

  void initTypeTemplate(TypeTemplate &temp, TypeTemplate::Kind kind,
                        std::vector<const DSType *> &&elementTypes, native_type_info_t info);

  void initErrorType(TYPE t, const char *typeName);

  void registerHandle(const BuiltinType &recv, const char *name, unsigned int index);

  void registerHandles(const BuiltinType &type);
};

} // namespace ydsh

#endif // YDSH_TYPE_POOL_H
