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

#include "archive.h"

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
    return {};
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

Optional<std::unordered_map<std::string, FieldHandle>> ModuleArchive::unpack(TypePool &pool) const {
  std::unordered_map<std::string, FieldHandle> handleMap;
  for (auto &e : this->getHandles()) {
    auto h = e.unpack(pool);
    if (!h.hasValue()) {
      return {};
    }
    if (!handleMap.emplace(e.getName(), h.unwrap()).second) {
      return {};
    }
  }
  return handleMap;
}

static void tryInsertByAscendingOrder(std::vector<ModuleArchivePtr> &targets,
                                      const ModuleArchivePtr &archive) {
  assert(archive);
  auto iter = std::lower_bound(targets.begin(), targets.end(), archive,
                               [](const ModuleArchivePtr &x, const ModuleArchivePtr &y) {
                                 return x->getModID() < y->getModID();
                               });
  if (iter == targets.end() || (*iter)->getModID() != archive->getModID()) {
    targets.insert(iter, archive);
  }
}

static void resolveTargets(std::vector<ModuleArchivePtr> &targets,
                           const ModuleArchivePtr &archive) {
  tryInsertByAscendingOrder(targets, archive);
  for (auto &e : archive->getImported()) {
    resolveTargets(targets, e.second);
  }
}

static void visit(std::vector<ModuleArchivePtr> &ret, std::vector<bool> &used,
                  const ModuleArchivePtr &archive) {
  if (used[archive->getModID()]) {
    return;
  }
  used[archive->getModID()] = true;
  for (auto &e : archive->getImported()) {
    visit(ret, used, e.second);
  }
  ret.push_back(archive);
}

static std::vector<ModuleArchivePtr> topologcalSort(const std::vector<ModuleArchivePtr> &targets) {
  std::vector<ModuleArchivePtr> ret;
  if (targets.empty()) {
    return ret;
  }
  std::vector<bool> used(targets.back()->getModID() + 1, false);
  for (auto &e : targets) {
    visit(ret, used, e);
  }
  return ret;
}

std::vector<ModuleArchivePtr> ModuleArchive::getDepsByTopologicalOrder() const {
  std::vector<ModuleArchivePtr> targets;
  for (auto &e : this->imported) {
    resolveTargets(targets, e.second);
  }
  return topologcalSort(targets);
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

static const ModType *load(TypePool &pool, const ModuleArchive &archive) {
  if (const ModType * type; (type = getModType(pool, archive.getModID()))) {
    return type;
  }

  FlexBuffer<ImportedModEntry> children;

  // add builtin
  auto &builtin = pool.getBuiltinModType();
  children.push_back(builtin.toModEntry(true));

  for (auto &child : archive.getImported()) {
    bool global = child.first;
    auto type = pool.getModTypeById(child.second->getModID());
    assert(type);
    auto e = static_cast<const ModType *>(type.asOk())->toModEntry(global);
    children.push_back(e);
  }

  auto ret = archive.unpack(pool);
  if (!ret.hasValue()) {
    return nullptr;
  }
  auto handleMap = ret.unwrap();
  return &pool.createModType(archive.getModID(), std::move(handleMap), std::move(children), 0);
}

const ModType *loadFromArchive(TypePool &pool, const ModuleArchive &archive) {
  if (const ModType * type; (type = getModType(pool, archive.getModID()))) {
    return type;
  }
  for (auto &dep : archive.getDepsByTopologicalOrder()) {
    TRY(load(pool, *dep));
  }
  return load(pool, archive);
}

// ############################
// ##     ModuleArchives     ##
// ############################

const ModuleArchivePtr ModuleArchives::EMPTY_ARCHIVE = std::make_shared<ModuleArchive>(); // NOLINT

static bool isRevertedArchive(std::unordered_set<unsigned short> &revertingModIdSet,
                              const ModuleArchivePtr &archive) {
  assert(archive);
  const auto modId = archive->getModID();
  auto iter = revertingModIdSet.find(modId);
  if (iter != revertingModIdSet.end()) {
    return true;
  }
  for (auto &e : archive->getImported()) {
    if (isRevertedArchive(revertingModIdSet, e.second)) {
      revertingModIdSet.emplace(modId);
      return true;
    }
  }
  return false;
}

void ModuleArchives::revert(std::unordered_set<unsigned short> &&revertingModIdSet) {
  for (auto iter = this->map.begin(); iter != this->map.end();) {
    if (isRevertedArchive(revertingModIdSet, iter->second)) {
      iter = this->map.erase(iter);
    } else {
      ++iter;
    }
  }
}

static bool isImported(const ModuleArchivePtr &archive, unsigned short id) {
  assert(archive);
  for (auto &e : archive->getImported()) {
    assert(e.second);
    if (e.second->getModID() == id) {
      return true;
    }
    if (isImported(e.second, id)) {
      return true;
    }
  }
  return false;
}

bool ModuleArchives::revertIfUnused(unsigned short id) {
  for (auto &e : this->map) {
    if (isImported(e.second, id)) {
      return false;
    }
  }
  for (auto iter = this->map.begin(); iter != this->map.end();) {
    if (iter->second && iter->second->getModID() == id) {
      this->map.erase(iter);
      return true;
    } else {
      ++iter;
    }
  }
  return false;
}

} // namespace ydsh::lsp