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

unsigned int DSType::getFieldSize() const {
  return this->superType != nullptr ? this->superType->getFieldSize() : 0;
}

const FieldHandle *DSType::lookupField(const std::string &) const { return nullptr; }

void DSType::walkField(std::function<bool(StringRef, const FieldHandle &)> &) const {}

bool DSType::isSameOrBaseTypeOf(const DSType &targetType) const {
  if (*this == targetType) {
    return true;
  }
  if (targetType.isNothingType()) {
    return true;
  }
  if (this->isOptionType()) {
    return static_cast<const ReifiedType *>(this)->getElementTypeAt(0).isSameOrBaseTypeOf(
        targetType);
  }
  auto *type = targetType.getSuperType();
  return type != nullptr && this->isSameOrBaseTypeOf(*type);
}

// #######################
// ##     TupleType     ##
// #######################

TupleType::TupleType(unsigned int id, StringRef ref, native_type_info_t info,
                     const DSType &superType, std::vector<const DSType *> &&types)
    : ReifiedType(id, ref, info, &superType, std::move(types)) {
  const unsigned int size = this->elementTypes.size();
  const unsigned int baseIndex = this->superType->getFieldSize();
  for (unsigned int i = 0; i < size; i++) {
    FieldHandle handle(0, *this->elementTypes[i], i + baseIndex, FieldAttribute());
    this->fieldHandleMap.emplace("_" + std::to_string(i), handle);
  }
}

unsigned int TupleType::getFieldSize() const { return this->elementTypes.size(); }

const FieldHandle *TupleType::lookupField(const std::string &fieldName) const {
  auto iter = this->fieldHandleMap.find(fieldName);
  if (iter == this->fieldHandleMap.end()) {
    return this->superType->lookupField(fieldName);
  }
  return &iter->second;
}

void TupleType::walkField(std::function<bool(StringRef, const FieldHandle &)> &walker) const {
  for (auto &e : this->fieldHandleMap) {
    if (!walker(e.first, e.second)) {
      return;
    }
  }
  this->superType->walkField(walker);
}

// #####################
// ##     ModType     ##
// #####################

ModType::~ModType() { free(this->children); }

const FieldHandle *ModType::lookupField(const std::string &fieldName) const {
  auto iter = this->handleMap.find(fieldName);
  if (iter != this->handleMap.end()) {
    return &iter->second;
  }
  return nullptr;
}

const FieldHandle *ModType::lookupVisibleSymbolAtModule(const TypePool &pool,
                                                        const std::string &name) const {
  // search own symbols
  auto *handle = this->lookupField(name);
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
      handle = static_cast<const ModType &>(type).lookupField(name);
      if (handle) {
        return handle;
      }
    }
  }
  return nullptr;
}

void ModType::walkField(std::function<bool(StringRef, const FieldHandle &)> &walker) const {
  for (auto &e : this->handleMap) {
    if (!walker(e.first, e.second)) {
      return;
    }
  }
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
