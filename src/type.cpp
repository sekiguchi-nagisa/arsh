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

#include "tcerror.h"
#include "type_pool.h"

namespace ydsh {

// ####################
// ##     DSType     ##
// ####################

void DSType::destroy() {
  switch (this->typeKind()) {
#define GEN_CASE(E)                                                                                \
  case TypeKind::E:                                                                                \
    delete cast<E##Type>(this);                                                                    \
    break;
    EACH_TYPE_KIND(GEN_CASE)
#undef GEN_CASE
  }
}

const FieldHandle *DSType::lookupField(const TypePool &pool, const std::string &fieldName) const {
  switch (this->typeKind()) {
  case TypeKind::Tuple:
    return cast<TupleType>(this)->lookupField(fieldName);
  case TypeKind::Mod:
    return cast<ModType>(this)->lookup(pool, fieldName);
  default:
    return nullptr;
  }
}

void DSType::walkField(const TypePool &pool,
                       std::function<bool(StringRef, const FieldHandle &)> &walker) const {
  switch (this->typeKind()) {
  case TypeKind::Tuple:
    for (auto &e : cast<TupleType>(this)->getFieldHandleMap()) {
      if (!walker(e.first, e.second)) {
        return;
      }
    }
    break;
  case TypeKind::Mod: {
    auto &modType = cast<ModType>(*this);
    for (auto &e : modType.getHandleMap()) {
      if (!walker(e.first, e.second)) {
        return;
      }
    }
    unsigned int size = modType.getChildSize();
    for (unsigned int i = 0; i < size; i++) {
      auto child = modType.getChildAt(i);
      if (child.isInlined()) {
        auto &childType = cast<ModType>(pool.get(child.typeId()));
        for (auto &e : childType.getHandleMap()) {
          if (!walker(e.first, e.second)) {
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

std::vector<const DSType *> DSType::getTypeParams(const TypePool &pool) const {
  std::vector<const DSType *> ret;
  switch (this->typeKind()) {
  case TypeKind::Reified: {
    auto &type = cast<ReifiedType>(*this);
    for (unsigned int i = 0; i < type.getElementSize(); i++) {
      ret.push_back(&type.getElementTypeAt(i));
    }
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

bool DSType::isSameOrBaseTypeOf(const DSType &targetType) const {
  if (*this == targetType) {
    return true;
  }
  if (targetType.isNothingType()) {
    return true;
  }
  if (this->isOptionType()) {
    return cast<OptionType>(this)->getElementType().isSameOrBaseTypeOf(targetType);
  }
  auto *type = targetType.getSuperType();
  return type != nullptr && this->isSameOrBaseTypeOf(*type);
}

// #######################
// ##     TupleType     ##
// #######################

static std::string toTupleFieldName(unsigned int i) { return "_" + std::to_string(i); }

TupleType::TupleType(unsigned int id, StringRef ref, native_type_info_t info,
                     const DSType &superType, std::vector<const DSType *> &&types)
    : BuiltinType(TypeKind::Tuple, id, ref, &superType, info) {
  const unsigned int size = types.size();
  for (unsigned int i = 0; i < size; i++) {
    auto handle = FieldHandle::create(0, *types[i], i, FieldAttribute());
    this->fieldHandleMap.emplace(toTupleFieldName(i), handle);
  }
}

const DSType &TupleType::getFieldTypeAt(const TypePool &pool, unsigned int i) const {
  assert(i < this->getFieldSize());
  auto name = toTupleFieldName(i);
  auto *handle = this->lookupField(name);
  assert(handle);
  return pool.get(handle->getTypeID());
}

const FieldHandle *TupleType::lookupField(const std::string &fieldName) const {
  auto iter = this->fieldHandleMap.find(fieldName);
  if (iter == this->fieldHandleMap.end()) {
    return nullptr;
  }
  return &iter->second;
}

// #####################
// ##     ModType     ##
// #####################

ModType::~ModType() {
  if (this->getChildSize() >= 3) {
    free(this->data.children.ptr);
  }
}

const FieldHandle *ModType::lookup(const TypePool &pool, const std::string &fieldName) const {
  if (auto *handle = this->find(fieldName); handle) {
    return handle;
  }

  // search public symbol from inlined imported module
  if (fieldName.empty() || fieldName[0] == '_') {
    return nullptr;
  }
  unsigned int size = this->getChildSize();
  for (unsigned int i = 0; i < size; i++) {
    auto child = this->getChildAt(i);
    if (child.isInlined()) {
      auto &type = pool.get(child.typeId());
      assert(type.isModType());
      if (auto *handle = cast<ModType>(type).find(fieldName)) {
        return handle;
      }
    }
  }
  return nullptr;
}

const FieldHandle *ModType::lookupVisibleSymbolAtModule(const TypePool &pool,
                                                        const std::string &name) const {
  // search own symbols
  auto *handle = this->find(name);
  if (handle) {
    return handle;
  }

  // search public symbol from globally loaded module
  if (name.empty() || name[0] == '_') {
    return nullptr;
  }
  unsigned int size = this->getChildSize();
  for (unsigned int i = 0; i < size; i++) {
    auto e = this->getChildAt(i);
    if (e.isGlobal()) {
      auto &type = pool.get(e.typeId());
      assert(type.isModType());
      handle = cast<ModType>(type).find(name);
      if (handle) {
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
    abort();
  }
  va_end(arg);

  return std::make_unique<TypeLookupError>(kind, CStrPtr(str));
}

TypeCheckError createTCErrorImpl(const Node &node, const char *kind, const char *fmt, ...) {
  va_list arg;

  va_start(arg, fmt);
  char *str = nullptr;
  if (vasprintf(&str, fmt, arg) == -1) {
    abort();
  }
  va_end(arg);

  return TypeCheckError(node.getToken(), kind, CStrPtr(str));
}

std::string toString(FieldAttribute attr) {
  const char *table[] = {
#define GEN_STR(E, V) #E,
      EACH_FIELD_ATTR(GEN_STR)
#undef GEN_STR
  };

  std::string value;
  for (unsigned int i = 0; i < std::size(table); i++) {
    if (hasFlag(attr, static_cast<FieldAttribute>(1u << i))) {
      if (!value.empty()) {
        value += " | ";
      }
      value += table[i];
    }
  }
  return value;
}

} // namespace ydsh
