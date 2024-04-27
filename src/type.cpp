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

#include <array>
#include <cstdarg>

#include "arg_parser_base.h"
#include "misc/num_util.hpp"
#include "tcerror.h"
#include "type_pool.h"

namespace arsh {

// ####################
// ##     DSType     ##
// ####################

void Type::destroy() {
  switch (this->typeKind()) {
#define GEN_CASE(E)                                                                                \
  case TypeKind::E:                                                                                \
    delete cast<E##Type>(this);                                                                    \
    break;
    EACH_TYPE_KIND(GEN_CASE)
#undef GEN_CASE
  }
}

HandlePtr Type::lookupField(const TypePool &pool, const std::string &fieldName) const {
  switch (this->typeKind()) {
  case TypeKind::Tuple:
    return cast<TupleType>(this)->lookupField(fieldName);
  case TypeKind::Record:
  case TypeKind::CLIRecord:
    return cast<RecordType>(this)->lookupField(fieldName);
  case TypeKind::Mod:
    return cast<ModType>(this)->lookup(pool, fieldName);
  default:
    return nullptr;
  }
}

void Type::walkField(const TypePool &pool,
                       const std::function<bool(StringRef, const Handle &)> &walker) const {
  switch (this->typeKind()) {
  case TypeKind::Tuple:
    for (auto &e : cast<TupleType>(this)->getFieldHandleMap()) {
      if (!walker(e.first, *e.second)) {
        return;
      }
    }
    break;
  case TypeKind::Record:
  case TypeKind::CLIRecord:
    for (auto &e : cast<RecordType>(this)->getHandleMap()) {
      if (!walker(e.first, *e.second)) {
        return;
      }
    }
    break;
  case TypeKind::Mod: {
    auto &modType = cast<ModType>(*this);
    for (auto &e : modType.getHandleMap()) {
      if (!walker(e.first, *e.second)) {
        return;
      }
    }
    unsigned int size = modType.getChildSize();
    for (unsigned int i = 0; i < size; i++) {
      auto child = modType.getChildAt(i);
      if (child.isInlined()) {
        auto &childType = cast<ModType>(pool.get(child.typeId()));
        for (auto &e : childType.getHandleMap()) {
          if (!walker(e.first, *e.second)) {
            return;
          }
        }
      }
    }
    break;
  }
  default:
    break;
  }
}

std::vector<const Type *> Type::getTypeParams(const TypePool &pool) const {
  std::vector<const Type *> ret;
  switch (this->typeKind()) {
  case TypeKind::Array: {
    auto &type = cast<ArrayType>(*this);
    ret.push_back(&type.getElementType());
    break;
  }
  case TypeKind::Map: {
    auto &type = cast<MapType>(*this);
    ret.push_back(&type.getKeyType());
    ret.push_back(&type.getValueType());
    break;
  }
  case TypeKind::Tuple: {
    auto &type = cast<TupleType>(*this);
    for (unsigned int i = 0; i < type.getFieldSize(); i++) {
      ret.push_back(&type.getFieldTypeAt(pool, i));
    }
    break;
  }
  case TypeKind::Option:
    ret.push_back(&cast<OptionType>(this)->getElementType());
    break;
  default:
    break;
  }
  return ret;
}

static bool isBaseTypeOf(const FunctionType &funcType1, const FunctionType &funcType2) {
  unsigned int paramSize = funcType1.getParamSize();
  if (paramSize != funcType2.getParamSize()) {
    return false;
  }
  for (unsigned int i = 0; i < paramSize; i++) {
    auto &paramType1 = funcType1.getParamTypeAt(i);
    auto &paramType2 = funcType2.getParamTypeAt(i);
    if (!paramType2.isSameOrBaseTypeOf(paramType1)) {
      return false;
    }
  }
  auto &returnType1 = funcType1.getReturnType();
  auto &returnType2 = funcType2.getReturnType();
  return returnType1.isSameOrBaseTypeOf(returnType2) || returnType1.isVoidType();
}

bool Type::isSameOrBaseTypeOf(const Type &targetType) const {
  if (*this == targetType) {
    return true;
  }
  if (targetType.isNothingType()) {
    return true;
  }
  if (this->isOptionType()) {
    return cast<OptionType>(this)->getElementType().isSameOrBaseTypeOf(
        targetType.isOptionType() ? cast<OptionType>(targetType).getElementType() : targetType);
  }
  if (this->isFuncType() && targetType.isFuncType()) {
    return isBaseTypeOf(cast<FunctionType>(*this), cast<FunctionType>(targetType));
  }
  auto *type = targetType.getSuperType();
  return type != nullptr && this->isSameOrBaseTypeOf(*type);
}

ModId Type::resolveBelongedModId() const {
  if (!this->isRecordOrDerived() && this->typeKind() != TypeKind::Error) {
    return BUILTIN_MOD_ID; // fast path
  }
  if (auto ref = this->getNameRef(); isQualifiedTypeName(this->getNameRef())) {
    auto index = ref.find('.');
    assert(index != StringRef::npos);
    auto modTypeName = ref.slice(0, index);
    modTypeName.removePrefix(strlen(MOD_SYMBOL_PREFIX));
    auto pair = convertToDecimal<uint32_t>(modTypeName.begin(), modTypeName.end());
    assert(pair && pair.value <= SYS_LIMIT_MOD_ID);
    return static_cast<ModId>(pair.value);
  }
  return BUILTIN_MOD_ID;
}

// #######################
// ##     TupleType     ##
// #######################

static std::string toTupleFieldName(unsigned int i) { return "_" + std::to_string(i); }

TupleType::TupleType(unsigned int id, StringRef ref, native_type_info_t info,
                     const Type &superType, std::vector<const Type *> &&types)
    : BuiltinType(TypeKind::Tuple, id, ref, &superType, info) {
  const unsigned int size = types.size();
  for (unsigned int i = 0; i < size; i++) {
    auto handle = HandlePtr::create(*types[i], i, HandleKind::VAR, HandleAttr());
    this->fieldHandleMap.emplace(toTupleFieldName(i), std::move(handle));
  }
}

const Type &TupleType::getFieldTypeAt(const TypePool &pool, unsigned int i) const {
  assert(i < this->getFieldSize());
  auto name = toTupleFieldName(i);
  auto handle = this->lookupField(name);
  assert(handle);
  return pool.get(handle->getTypeId());
}

HandlePtr TupleType::lookupField(const std::string &fieldName) const {
  auto iter = this->fieldHandleMap.find(fieldName);
  if (iter == this->fieldHandleMap.end()) {
    return nullptr;
  }
  return iter->second;
}

// ########################
// ##     RecordType     ##
// ########################

HandlePtr RecordType::lookupField(const std::string &fieldName) const {
  auto iter = this->handleMap.find(fieldName);
  if (iter == this->handleMap.end()) {
    return nullptr;
  }
  return iter->second;
}

// ###########################
// ##     CLIRecordType     ##
// ###########################

// for not export ArgEntry definition to type.h

CLIRecordType::CLIRecordType(unsigned int id, StringRef ref, const Type &superType, Attr attr,
                             std::string &&desc)
    : RecordType(TypeKind::CLIRecord, id, ref, superType), desc(std::move(desc)) {
  this->setExtraAttr(toUnderlying(attr));
}

void CLIRecordType::finalizeArgEntries(std::vector<ArgEntry> &&args) {
  this->entries = std::move(args);
}

// #####################
// ##     ModType     ##
// #####################

ModType::~ModType() { this->disposeChildren(); }

HandlePtr ModType::lookupImpl(const TypePool &pool, const std::string &name,
                              bool searchGlobal) const {
  if (auto handle = this->find(name); handle) {
    return handle;
  }

  // search public symbol from globally/inlined imported modules
  if (name.empty() || name[0] == '_') {
    return nullptr;
  }
  unsigned int size = this->getChildSize();
  for (unsigned int i = 0; i < size; i++) {
    auto child = this->getChildAt(i);
    bool target = searchGlobal ? child.isGlobal() : child.isInlined();
    if (target) {
      auto &type = pool.get(child.typeId());
      assert(type.isModType());
      if (auto handle = cast<ModType>(type).find(name)) {
        return handle;
      }
    }
  }
  return nullptr;
}

std::unique_ptr<TypeLookupError> createTLErrorImpl(const char *kind, const char *fmt, ...) {
  va_list arg;

  va_start(arg, fmt);
  char *str = nullptr;
  if (vasprintf(&str, fmt, arg) == -1) {
    fatal_perror("failed");
  }
  va_end(arg);

  return std::make_unique<TypeLookupError>(kind, CStrPtr(str));
}

TypeCheckError createTCErrorImpl(const Node &node, const char *kind, const char *fmt, ...) {
  va_list arg;

  va_start(arg, fmt);
  char *str = nullptr;
  if (vasprintf(&str, fmt, arg) == -1) {
    fatal_perror("failed");
  }
  va_end(arg);

  return {node.getToken(), kind, CStrPtr(str)};
}

const char *toString(HandleKind kind) {
  const char *table[] = {
#define GEN_STR(E) #E,
      EACH_HANDLE_KIND(GEN_STR)
#undef GEN_STR
  };
  return table[toUnderlying(kind)];
}

std::string toString(HandleAttr attr) {
  const char *table[] = {
#define GEN_STR(E, V) #E,
      EACH_HANDLE_ATTR(GEN_STR)
#undef GEN_STR
  };

  std::string value;
  for (unsigned int i = 0; i < std::size(table); i++) {
    if (hasFlag(attr, static_cast<HandleAttr>(1u << i))) {
      if (!value.empty()) {
        value += " | ";
      }
      value += table[i];
    }
  }
  return value;
}

void Handle::destroy() {
  if (isa<FuncHandle>(this)) {
    delete cast<FuncHandle>(this);
  } else if (isa<MethodHandle>(this)) {
    delete cast<MethodHandle>(this);
  } else {
    delete this;
  }
}

const char *TypeTemplate::getName() const {
  switch (this->kind) {
  case Kind::Array:
    return TYPE_ARRAY;
  case Kind::Map:
    return TYPE_MAP;
  case Kind::Tuple:
    return TYPE_TUPLE;
  case Kind::Option:
    return TYPE_OPTION;
  case Kind::Func:
    return TYPE_FUNC;
  }
  return nullptr;
}

// ##########################
// ##     MethodHandle     ##
// ##########################

MethodHandle::~MethodHandle() { free(this->packedParamNames); }

std::unique_ptr<MethodHandle> MethodHandle::create(const Type &recv, unsigned int index,
                                                   const Type *ret,
                                                   const std::vector<const Type *> &params,
                                                   PackedParamNames &&packed, ModId modId) {
  const size_t paramSize = params.size();
  assert(paramSize <= SYS_LIMIT_METHOD_PARAM_NUM);
  void *ptr = operator new(sizeof(MethodHandle) + sizeof(uintptr_t) * paramSize);
  const auto &actualRet = ret ? *ret : recv;
  auto *handle = new (ptr) MethodHandle(recv, index, actualRet, paramSize, modId,
                                        ret ? HandleKind::METHOD : HandleKind::CONSTRUCTOR);
  for (size_t i = 0; i < paramSize; i++) {
    handle->paramTypes[i] = params[i];
  }
  if (!handle->isNative()) {
    /**
     * normally not native, but in some situation (for method handle unpacking) will be native.
     * native method handle never maintain ptr
     */
    handle->packedParamNames = packed.take();
  }
  return std::unique_ptr<MethodHandle>(handle);
}

StringRef MethodHandle::getPackedParamNames() const {
  StringRef ref;
  if (this->isNative()) {
    ref = nativeFuncInfoTable()[this->getIndex()].params;
  } else {
    ref = this->packedParamNames;
  }
  return ref;
}

} // namespace arsh
