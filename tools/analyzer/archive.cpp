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
  } else {
    switch (type.typeKind()) {
    case TypeKind::Function: {
      this->writeT(ArchiveType::FUNC);
      auto &func = cast<FunctionType>(type);
      this->add(func.getReturnType());
      assert(func.getParamSize() <= SYS_LIMIT_FUNC_PARAM_NUM);
      this->write8(static_cast<uint8_t>(func.getParamSize()));
      for (unsigned int i = 0; i < func.getParamSize(); i++) {
        this->add(func.getParamTypeAt(i));
      }
      break;
    }
    case TypeKind::Builtin:
      break; // unreachable
    case TypeKind::Array:
      this->writeT(ArchiveType::ARRAY);
      this->add(cast<ArrayType>(type).getElementType());
      break;
    case TypeKind::Map:
      this->writeT(ArchiveType::MAP);
      this->add(cast<MapType>(type).getKeyType());
      this->add(cast<MapType>(type).getValueType());
      break;
    case TypeKind::Tuple: {
      this->writeT(ArchiveType::TUPLE);
      auto &tuple = cast<TupleType>(type);
      unsigned int size = tuple.getFieldSize();
      assert(size <= SYS_LIMIT_TUPLE_NUM);
      this->write8(static_cast<uint8_t>(size));
      for (unsigned int i = 0; i < size; i++) {
        this->add(tuple.getFieldTypeAt(this->pool, i));
      }
      break;
    }
    case TypeKind::Option:
      this->writeT(ArchiveType::OPTION);
      this->add(cast<OptionType>(type).getElementType());
      break;
    case TypeKind::Error: // for user-defined error type
      if (auto iter = this->udTypeSet.find(type.typeId());
          iter != this->udTypeSet.end()) { // already found, write type name
        this->writeT(ArchiveType::CACHED);
        this->writeStr(type.getNameRef());
      } else { // not found
        this->udTypeSet.emplace(type.typeId());
        this->writeT(ArchiveType::ERROR);
        auto typeName = type.getNameRef();
        const auto pos = typeName.find('.');
        assert(pos != StringRef::npos);
        this->writeStr(typeName.substr(pos + 1));
        this->add(*type.getSuperType());
        auto ret = this->pool.getType(typeName.slice(0, pos));
        assert(ret);
        this->add(*ret.asOk());
      }
      break;
    case TypeKind::Record:
      if (auto iter = this->udTypeSet.find(type.typeId()); iter != this->udTypeSet.end()) {
        this->writeT(ArchiveType::CACHED);
        this->writeStr(type.getNameRef());
      } else { // not found
        this->udTypeSet.emplace(type.typeId());
        this->writeT(ArchiveType::RECORD);
        auto typeName = type.getNameRef();
        const auto pos = typeName.find('.');
        assert(pos != StringRef::npos);
        this->writeStr(typeName.substr(pos + 1));
        auto ret = this->pool.getType(typeName.slice(0, pos));
        assert(ret);
        this->add(*ret.asOk());

        auto &recordType = cast<RecordType>(type);
        this->write32(recordType.getHandleMap().size());
        for (auto &e : recordType.getHandleMap()) {
          this->writeStr(e.first);
          this->add(*e.second);
        }
      }
      break;
    case TypeKind::Mod:
      this->writeT(ArchiveType::MOD);
      auto &modType = cast<ModType>(type);
      this->write16(modType.getModId());
      break;
    }
  }
}

void Archiver::add(const Handle &handle) {
  this->write32(handle.getIndex());
  static_assert(std::is_same_v<std::underlying_type_t<HandleAttr>, unsigned short>);
  this->write16(static_cast<unsigned short>(handle.attr()));
  this->write16(handle.getModId());
  auto &type = this->pool.get(handle.getTypeId());
  this->add(type);
  if (handle.isMethod()) {
    auto &methodHandle = static_cast<const MethodHandle &>(handle);
    const auto paramSize = methodHandle.getParamSize();
    this->write8(paramSize + 1);
    this->add(methodHandle.getReturnType());
    for (unsigned int i = 0; i < paramSize; i++) {
      this->add(methodHandle.getParamTypeAt(i));
    }
  } else {
    this->write8(0);
  }
}

// ########################
// ##     Unarchiver     ##
// ########################

#define TRY(E)                                                                                     \
  ({                                                                                               \
    auto __v = E;                                                                                  \
    if (!__v) {                                                                                    \
      return nullptr;                                                                              \
    }                                                                                              \
    std::forward<decltype(__v)>(__v);                                                              \
  })

HandlePtr Unarchiver::unpackHandle() {
  uint32_t index = this->read32();
  uint16_t attr = this->read16();
  uint16_t modId = this->read16();
  auto type = TRY(this->unpackType());
  unsigned int famSize = this->read8();
  if (famSize) { // method handle
    auto *returnType = TRY(this->unpackType());
    std::vector<const DSType *> paramTypes;
    for (unsigned int i = 0; i < famSize - 1; i++) {
      paramTypes.push_back(TRY(this->unpackType()));
    }
    auto handle = MethodHandle::create(*type, index, *returnType, paramTypes, modId);
    return HandlePtr(handle.release());
  } else {
    return HandlePtr::create(*type, index, static_cast<HandleAttr>(attr), modId);
  }
}

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
  case ArchiveType::ERROR: {
    std::string name = this->readStr();
    auto *superType = TRY(this->unpackType());
    auto *modType = TRY(this->unpackType());
    assert(isa<ModType>(modType));
    auto ret =
        TRY(this->pool.createErrorType(name, *superType, cast<ModType>(modType)->getModId()));
    return std::move(ret).take();
  }
  case ArchiveType::RECORD: {
    std::string name = this->readStr();
    auto *modType = TRY(this->unpackType());
    assert(isa<ModType>(modType));
    auto ret = TRY(this->pool.createRecordType(name, cast<ModType>(modType)->getModId()));
    uint32_t size = this->read32();
    std::unordered_map<std::string, HandlePtr> handles;
    for (unsigned int i = 0; i < size; i++) {
      auto n = this->readStr();
      auto handle = TRY(this->unpackHandle());
      handles.emplace(std::move(n), std::move(handle));
    }
    ret = TRY(this->pool.finalizeRecordType(cast<RecordType>(*ret.asOk()), std::move(handles)));
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
  case ArchiveType::CACHED: {
    std::string typeName = this->readStr();
    auto ret = TRY(this->pool.getType(typeName));
    return std::move(ret).take();
  }
  }
  return nullptr;
}

Optional<std::unordered_map<std::string, HandlePtr>> ModuleArchive::unpack(TypePool &pool) const {
  std::unordered_map<std::string, HandlePtr> handleMap;
  for (auto &e : this->getHandles()) {
    auto h = e.unpack(pool);
    if (!h) {
      return {};
    }
    if (!handleMap.emplace(e.getName(), h).second) {
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
                                 return x->getModId() < y->getModId();
                               });
  if (iter == targets.end() || (*iter)->getModId() != archive->getModId()) {
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
  if (used[archive->getModId()]) {
    return;
  }
  used[archive->getModId()] = true;
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
  std::vector<bool> used(targets.back()->getModId() + 1, false);
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
    return cast<ModType>(type);
  }
  return nullptr;
}

static const ModType *load(TypePool &pool, const ModuleArchive &archive) {
  if (const ModType * type; (type = getModType(pool, archive.getModId()))) {
    return type;
  }

  FlexBuffer<ModType::Imported> children;

  // add builtin
  auto &builtin = pool.getBuiltinModType();
  children.push_back(builtin.toModEntry(ImportedModKind::GLOBAL));

  for (auto &child : archive.getImported()) {
    auto type = pool.getModTypeById(child.second->getModId());
    assert(type);
    auto e = cast<ModType>(type.asOk())->toModEntry(child.first);
    children.push_back(e);
  }

  auto ret = archive.unpack(pool);
  if (!ret.hasValue()) {
    return nullptr;
  }
  auto handleMap = ret.unwrap();
  return &pool.createModType(archive.getModId(), std::move(handleMap), std::move(children), 0);
}

const ModType *loadFromArchive(TypePool &pool, const ModuleArchive &archive) {
  if (const ModType * type; (type = getModType(pool, archive.getModId()))) {
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

struct ModuleArchiveEntryComp {
  bool operator()(const std::pair<unsigned short, ModuleArchivePtr> &x, unsigned short y) const {
    return x.first < y;
  }

  bool operator()(unsigned short x, const std::pair<unsigned short, ModuleArchivePtr> &y) const {
    return x < y.first;
  }
};

ModuleArchivePtr ModuleArchives::find(unsigned short modId) const {
  auto iter =
      std::lower_bound(this->values.begin(), this->values.end(), modId, ModuleArchiveEntryComp());
  if (iter != this->values.end() && iter->first == modId) {
    return iter->second;
  }
  return nullptr;
}

ModuleArchives::iterator_type ModuleArchives::reserveImpl(unsigned short modId) {
  auto iter =
      std::lower_bound(this->values.begin(), this->values.end(), modId, ModuleArchiveEntryComp());
  if (iter != this->values.end() && iter->first == modId) {
    iter->second = EMPTY_ARCHIVE;
    return iter;
  } else {
    return this->values.emplace(iter, modId, EMPTY_ARCHIVE);
  }
}

static bool isRevertedArchive(std::unordered_set<unsigned short> &revertingModIdSet,
                              const ModuleArchive &archive) {
  const auto modId = archive.getModId();
  auto iter = revertingModIdSet.find(modId);
  if (iter != revertingModIdSet.end()) {
    return true;
  }
  for (auto &e : archive.getImported()) {
    assert(e.second);
    if (isRevertedArchive(revertingModIdSet, *e.second)) {
      revertingModIdSet.emplace(modId);
      return true;
    }
  }
  return false;
}

void ModuleArchives::revert(std::unordered_set<unsigned short> &&revertingModIdSet) {
  for (auto &e : this->values) {
    if (e.second && isRevertedArchive(revertingModIdSet, *e.second)) {
      e.second = nullptr;
    }
  }
}

static bool isImported(const ModuleArchive &archive, unsigned short id) {
  for (auto &e : archive.getImported()) {
    assert(e.second);
    if (e.second->getModId() == id) {
      return true;
    }
    if (isImported(*e.second, id)) {
      return true;
    }
  }
  return false;
}

bool ModuleArchives::removeIfUnused(unsigned short id) {
  for (auto &e : this->values) {
    if (e.second && isImported(*e.second, id)) {
      return false;
    }
  }
  for (auto iter = this->values.begin(); iter != this->values.end();) {
    if (iter->second && iter->second->getModId() == id) {
      this->values.erase(iter);
      return true;
    } else {
      ++iter;
    }
  }
  return false;
}

} // namespace ydsh::lsp