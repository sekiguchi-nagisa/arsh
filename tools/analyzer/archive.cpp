/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#include <type_pool.h>

#include "index.h"

namespace ydsh::lsp {

using namespace ydsh;

// ######################
// ##     Archiver     ##
// ######################

void Archiver::add(const DSType &type) {
  if (type.typeId() < this->builtinTypeIdCount && this->builtinTypeIdCount <= UINT8_MAX) {
    this->writeT(ArchiveType::PREDEFINED);
    this->write8(static_cast<uint8_t>(type.typeId()));
  } else if (this->pool.isArrayType(type)) {
    this->writeT(ArchiveType::ARRAY);
    this->add(static_cast<const ReifiedType &>(type).getElementTypeAt(0));
  } else if (this->pool.isMapType(type)) {
    this->writeT(ArchiveType::MAP);
    this->add(static_cast<const ReifiedType &>(type).getElementTypeAt(0));
    this->add(static_cast<const ReifiedType &>(type).getElementTypeAt(1));
  } else if (this->pool.isTupleType(type)) {
    this->writeT(ArchiveType::TUPLE);
    auto &tuple = static_cast<const TupleType &>(type);
    assert(tuple.getFieldSize() <= SYS_LIMIT_TUPLE_NUM);
    this->write8(static_cast<uint8_t>(tuple.getFieldSize()));
    for (unsigned int i = 0; i < tuple.getElementSize(); i++) {
      this->add(tuple.getElementTypeAt(i));
    }
  } else if (type.isOptionType()) {
    this->writeT(ArchiveType::OPTION);
    this->add(static_cast<const ReifiedType &>(type).getElementTypeAt(0));
  } else if (type.isFuncType()) {
    this->writeT(ArchiveType::FUNC);
    auto &func = static_cast<const FunctionType &>(type);
    this->add(func.getReturnType());
    assert(func.getParamSize() <= SYS_LIMIT_FUNC_PARAM_NUM);
    this->write8(static_cast<uint8_t>(func.getParamSize()));
    for (unsigned int i = 0; i < func.getParamSize(); i++) {
      this->add(func.getParamTypeAt(i));
    }
  } else if (type.isModType()) {
    this->writeT(ArchiveType::MOD);
    auto &modType = static_cast<const ModType &>(type);
    this->write16(modType.getModID());
  }
}

void Archiver::add(const FieldHandle &handle) {
  this->write32(handle.getCommitID());
  this->write32(handle.getIndex());
  static_assert(std::is_same_v<std::underlying_type_t<FieldAttribute>, unsigned short>);
  this->write16(static_cast<unsigned short>(handle.attr()));
  this->write16(handle.getModID());
  auto &type = this->pool.get(handle.getTypeID());
  this->add(type);
}

// ########################
// ##     Unarchiver     ##
// ########################

Optional<FieldHandle> Unarchiver::unpackHandle() {
  uint32_t commitID = this->read32();
  uint32_t index = this->read32();
  uint16_t attr = this->read16();
  uint16_t modID = this->read16();
  auto type = this->unpackType();
  if (!type) {
    return Optional<FieldHandle>();
  }
  return FieldHandle(commitID, *type, index, static_cast<FieldAttribute>(attr), modID);
}

#define TRY(E)                                                                                     \
  ({                                                                                               \
    auto __v = E;                                                                                  \
    if (!__v) {                                                                                    \
      return nullptr;                                                                              \
    }                                                                                              \
    std::forward<decltype(__v)>(__v);                                                              \
  })

const DSType *Unarchiver::unpackType() {
  auto k = this->readT();
  switch (k) {
  case ArchiveType::PREDEFINED: {
    uint32_t id = this->read8();
    return &this->pool.get(id);
  }
  case ArchiveType::ARRAY: {
    auto *type = TRY(this->unpackType());
    auto ret = TRY(this->pool.createArrayType(const_cast<DSType &>(*type)));
    return std::move(ret).take();
  }
  case ArchiveType::MAP: {
    auto &key = *const_cast<DSType *>(TRY(this->unpackType()));
    auto &value = *const_cast<DSType *>(TRY(this->unpackType()));
    auto ret = TRY(this->pool.createMapType(key, value));
    return std::move(ret).take();
  }
  case ArchiveType::TUPLE: {
    uint8_t n = this->read8();
    std::vector<const DSType *> types;
    for (unsigned int i = 0; i < n; i++) {
      auto *type = TRY(this->unpackType());
      types.push_back(const_cast<DSType *>(type));
    }
    auto ret = TRY(this->pool.createTupleType(std::move(types)));
    return std::move(ret).take();
  }
  case ArchiveType::OPTION: {
    auto *type = TRY(this->unpackType());
    auto ret = TRY(this->pool.createOptionType(const_cast<DSType &>(*type)));
    return std::move(ret).take();
  }
  case ArchiveType::FUNC: {
    auto *retType = TRY(this->unpackType());
    std::vector<const DSType *> types;
    uint8_t n = this->read8();
    for (unsigned int i = 0; i < n; i++) {
      auto *type = TRY(this->unpackType());
      types.push_back(const_cast<DSType *>(type));
    }
    auto ret = TRY(this->pool.createFuncType(*retType, std::move(types)));
    return std::move(ret).take();
  }
  case ArchiveType::MOD: {
    uint16_t modID = this->read16();
    auto ret = TRY(this->pool.getModTypeById(modID));
    return std::move(ret).take();
  }
  }
  return nullptr;
}

ModuleArchive ModuleArchive::create(const TypePool &pool, const ModType &modType,
                                    unsigned int idCount) {
  std::vector<Archive> handles;
  for (auto &e : modType.getHandleMap()) {
    handles.push_back(Archive::pack(pool, idCount, e.first, e.second));
  }
  return ModuleArchive(std::move(handles));
}

Optional<std::unordered_map<std::string, FieldHandle>> ModuleArchive::unpack(TypePool &pool) const {
  std::unordered_map<std::string, FieldHandle> handleMap;
  for (auto &e : this->getHandles()) {
    auto h = e.unpack(pool);
    if (!h.hasValue()) {
      return Optional<std::unordered_map<std::string, FieldHandle>>();
    }
    if (!handleMap.emplace(e.getName(), h.unwrap()).second) {
      return Optional<std::unordered_map<std::string, FieldHandle>>();
    }
  }
  return handleMap;
}

static const ModType *getModType(const TypePool &pool, unsigned short modId) {
  auto ret = pool.getModTypeById(modId);
  if (ret) {
    auto *type = ret.asOk();
    assert(type && type->isModType());
    return static_cast<const ModType *>(type);
  }
  return nullptr;
}

static const ModType *load(TypePool &pool, const ModuleIndex &index) {
  if (const ModType * type; (type = getModType(pool, index.getModId()))) {
    return type;
  }

  FlexBuffer<ImportedModEntry> children;

  // add builtin
  auto &builtin = pool.getBuiltinModType();
  children.push_back(builtin.toModEntry(true));

  for (auto &child : index.getImportedIndexes()) {
    bool global = child.first;
    auto type = pool.getModTypeById(child.second->getModId());
    assert(type);
    auto e = static_cast<const ModType *>(type.asOk())->toModEntry(global);
    children.push_back(e);
  }

  auto ret = index.getArchive().unpack(pool);
  if (!ret.hasValue()) {
    return nullptr;
  }
  auto handleMap = ret.unwrap();
  return &pool.createModType(index.getModId(), std::move(handleMap), std::move(children), 0);
}

const ModType *loadFromModuleIndex(TypePool &pool, const ModuleIndex &index) {
  if (const ModType * type; (type = getModType(pool, index.getModId()))) {
    return type;
  }

  for (auto &dep : index.getDepsByTopologicalOrder()) {
    TRY(load(pool, *dep));
  }
  return load(pool, index);
}

} // namespace ydsh::lsp