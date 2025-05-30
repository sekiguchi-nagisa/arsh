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
#include "hasher.h"

namespace arsh::lsp {

using namespace arsh;

// ######################
// ##     Archiver     ##
// ######################

void Archiver::add(const Type &type) {
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
    case TypeKind::DerivedError: // for user-defined error type
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
        assert(ret && isa<ModType>(ret));
        this->writeModId(cast<ModType>(ret)->getModId());
      }
      break;
    case TypeKind::Record:
    case TypeKind::CLIRecord:
      if (auto iter = this->udTypeSet.find(type.typeId()); iter != this->udTypeSet.end()) {
        this->writeT(ArchiveType::CACHED);
        this->writeStr(type.getNameRef());
      } else { // not found
        this->udTypeSet.emplace(type.typeId());
        this->writeT(type.typeKind() == TypeKind::Record ? ArchiveType::RECORD
                                                         : ArchiveType::CLI_RECORD);
        auto typeName = type.getNameRef();
        const auto pos = typeName.find('.');
        assert(pos != StringRef::npos);
        this->writeStr(typeName.substr(pos + 1));
        auto ret = this->pool.getType(typeName.slice(0, pos));
        assert(ret && isa<ModType>(ret));
        this->writeModId(cast<ModType>(ret)->getModId());
        if (type.typeKind() == TypeKind::CLIRecord) {
          auto &cli = cast<CLIRecordType>(type);
          this->writeEnum(cli.getAttr());
          this->writeStr(cli.getDesc());
        }

        auto &recordType = cast<RecordType>(type);
        this->write32(recordType.getHandleMap().size());

        // pack handles by defined order
        std::vector<std::pair<StringRef, HandlePtr>> handles;
        handles.reserve(recordType.getHandleMap().size());
        for (const auto &[name, hd] : recordType.getHandleMap()) {
          handles.emplace_back(name, hd);
        }
        std::sort(handles.begin(), handles.end(), [](const auto &x, const auto &y) {
          auto xIndex = x.second->getIndex();
          auto yIndex = y.second->getIndex();
          return xIndex < yIndex || (xIndex == yIndex && x.first < y.first);
        });
        for (auto &[name, hd] : handles) {
          this->add(name, *hd);
        }

        if (type.typeKind() == TypeKind::CLIRecord) {
          auto &cliRecordType = cast<CLIRecordType>(type);
          this->write32(static_cast<uint32_t>(cliRecordType.getEntries().size()));
          for (auto &e : cliRecordType.getEntries()) {
            this->add(e);
          }
        }
      }
      break;
    case TypeKind::Mod:
      this->writeT(ArchiveType::MOD);
      auto &modType = cast<ModType>(type);
      this->writeModId(modType.getModId());
      break;
    }
  }
}

void Archiver::add(StringRef name, const Handle &handle) {
  assert(!handle.is(HandleKind::NATIVE));
  if (handle.isMethodHandle()) {
    assert(isMethodFullName(name));
    name = trimMethodFullNameSuffix(name);
  }
  this->writeStr(name);
  this->write32(handle.getIndex());
  this->writeEnum(handle.getKind());
  this->writeEnum(handle.attr());
  this->writeModId(handle.getModId());
  auto &type = this->pool.get(handle.getTypeId());
  this->add(type);
  if (handle.isMethodHandle()) {
    auto &methodHandle = cast<MethodHandle>(handle);
    const auto paramSize = methodHandle.getParamSize();
    this->write8(paramSize + 1);
    this->add(methodHandle.getReturnType());
    for (unsigned int i = 0; i < paramSize; i++) {
      this->add(methodHandle.getParamTypeAt(i));
    }
    this->writeStr(methodHandle.getPackedParamNames());
  } else {
    this->write8(0);
    if (handle.isFuncHandle()) {
      this->writeStr(cast<FuncHandle>(handle).getPackedParamNames());
    }
  }
}

void Archiver::add(const ArgEntry &entry) {
  this->writeEnum(entry.getIndex());
  this->write8(entry.getFieldOffset());
  this->writeEnum(entry.getParseOp());
  this->writeEnum(entry.getAttr());
  this->write8(entry.getXORGroupId());
  this->writeEnum(entry.getCheckerKind());
  this->write8(entry.getShortName());
  this->writeStr(entry.getLongName());
  this->writeStr(entry.getArgName());
  this->writeStr(entry.getDefaultValue());
  this->writeStr(entry.getDetail());
  switch (entry.getCheckerKind()) {
  case ArgEntry::CheckerKind::NOP:
    break;
  case ArgEntry::CheckerKind::INT: {
    auto [min, max] = entry.getIntRange();
    this->write64(min);
    this->write64(max);
    break;
  }
  case ArgEntry::CheckerKind::CHOICE: {
    auto &choice = entry.getChoice();
    this->write32(choice.size());
    for (auto &e : choice) {
      this->writeStr(e);
    }
    break;
  }
  }
}

std::pair<std::string, HandlePtr> Archive::unpack(TypePool &pool) const {
  Unarchiver unarchiver(pool, *this);
  return unarchiver.take();
}

// ########################
// ##     Unarchiver     ##
// ########################

#define TRY(E)                                                                                     \
  ({                                                                                               \
    auto __v = E;                                                                                  \
    if (unlikely(!__v)) {                                                                          \
      return {"", nullptr};                                                                        \
    }                                                                                              \
    std::forward<decltype(__v)>(__v);                                                              \
  })

std::pair<std::string, HandlePtr> Unarchiver::unpackHandle() {
  std::string name = this->readStr();
  uint32_t index = this->read32();
  const auto kind = this->readEnum<HandleKind>();
  const auto attr = this->readEnum<HandleAttr>();
  auto modId = this->readModId();
  auto type = TRY(this->unpackType());
  unsigned int famSize = this->read8();
  if (famSize) { // method handle
    auto *returnType = TRY(this->unpackType());
    std::vector<const Type *> paramTypes;
    for (unsigned int i = 0; i < famSize - 1; i++) {
      paramTypes.push_back(TRY(this->unpackType()));
    }
    auto packedParamNames = this->readPackedParamNames();
    std::unique_ptr<MethodHandle> handle;
    if (kind == HandleKind::METHOD) {
      handle = MethodHandle::method(*type, index, *returnType, paramTypes,
                                    std::move(packedParamNames), modId);
    } else {
      assert(kind == HandleKind::CONSTRUCTOR);
      handle =
          MethodHandle::constructor(*type, index, paramTypes, std::move(packedParamNames), modId);
    }
    return {toMethodFullName(type->typeId(), name), HandlePtr(handle.release())};
  } else if (kind == HandleKind::FUNC) {
    assert(type->isFuncType());
    auto packed = this->readPackedParamNames();
    auto handle = FuncHandle::create(cast<FunctionType>(*type), index, std::move(packed), modId);
    return {std::move(name), HandlePtr(handle.release())};
  } else {
    return {std::move(name), HandlePtr::create(*type, index, kind, attr, modId)};
  }
}

#undef TRY
#define TRY(E)                                                                                     \
  ({                                                                                               \
    auto __v = E;                                                                                  \
    if (unlikely(!__v)) {                                                                          \
      return nullptr;                                                                              \
    }                                                                                              \
    std::forward<decltype(__v)>(__v);                                                              \
  })

const Type *Unarchiver::unpackType() {
  const auto k = this->readT();
  switch (k) {
  case ArchiveType::PREDEFINED: {
    uint32_t id = this->read8();
    return &this->pool.get(id);
  }
  case ArchiveType::ARRAY: {
    auto *type = TRY(this->unpackType());
    auto ret = TRY(this->pool.createArrayType(*type));
    return std::move(ret).take();
  }
  case ArchiveType::MAP: {
    auto *key = TRY(this->unpackType());
    auto *value = TRY(this->unpackType());
    auto ret = TRY(this->pool.createMapType(*key, *value));
    return std::move(ret).take();
  }
  case ArchiveType::TUPLE: {
    uint8_t n = this->read8();
    std::vector<const Type *> types;
    for (unsigned int i = 0; i < n; i++) {
      auto *type = TRY(this->unpackType());
      types.push_back(type);
    }
    auto ret = TRY(this->pool.createTupleType(std::move(types)));
    return std::move(ret).take();
  }
  case ArchiveType::OPTION: {
    auto *type = TRY(this->unpackType());
    auto ret = TRY(this->pool.createOptionType(*type));
    return std::move(ret).take();
  }
  case ArchiveType::ERROR: {
    std::string name = this->readStr();
    auto *superType = TRY(this->unpackType());
    auto modId = this->readModId();
    auto ret = TRY(this->pool.createErrorType(name, *superType, modId));
    return std::move(ret).take();
  }
  case ArchiveType::RECORD:
  case ArchiveType::CLI_RECORD: {
    std::string name = this->readStr();
    auto modId = this->readModId();
    CLIRecordType::Attr attr{};
    std::string desc;
    if (k == ArchiveType::CLI_RECORD) {
      attr = this->readEnum<CLIRecordType::Attr>();
      desc = this->readStr();
    }
    auto ret = k == ArchiveType::RECORD
                   ? TRY(this->pool.createRecordType(name, modId))
                   : TRY(this->pool.createCLIRecordType(name, modId, attr, std::move(desc)));
    uint32_t size = this->read32();
    std::unordered_map<std::string, HandlePtr> handles;
    for (unsigned int i = 0; i < size; i++) {
      auto pair = this->unpackHandle();
      if (!pair.second) {
        return nullptr;
      }
      handles.insert(std::move(pair));
    }
    if (k == ArchiveType::RECORD) {
      ret = TRY(this->pool.finalizeRecordType(cast<RecordType>(*ret.asOk()), std::move(handles)));
    } else {
      unsigned int entrySize = this->read32();
      std::vector<ArgEntry> entries;
      entries.reserve(entrySize);
      for (unsigned int i = 0; i < entrySize; i++) {
        auto pair = this->unpackArgEntry();
        if (!pair.second) {
          return nullptr;
        }
        entries.push_back(std::move(pair.first));
      }
      ret = TRY(this->pool.finalizeCLIRecordType(cast<CLIRecordType>(*ret.asOk()),
                                                 std::move(handles), std::move(entries)));
    }
    return std::move(ret).take();
  }
  case ArchiveType::FUNC: {
    auto *retType = TRY(this->unpackType());
    std::vector<const Type *> types;
    uint8_t n = this->read8();
    for (unsigned int i = 0; i < n; i++) {
      auto *type = TRY(this->unpackType());
      types.push_back(type);
    }
    auto ret = TRY(this->pool.createFuncType(*retType, std::move(types)));
    return std::move(ret).take();
  }
  case ArchiveType::MOD: {
    auto modId = this->readModId();
    return TRY(this->pool.getModTypeById(modId));
  }
  case ArchiveType::CACHED: {
    std::string typeName = this->readStr();
    return TRY(this->pool.getType(typeName));
  }
  }
  return nullptr;
}

std::pair<ArgEntry, bool> Unarchiver::unpackArgEntry() {
  auto index = this->readEnum<ArgEntryIndex>();
  ArgEntry entry(index, this->read8());
  entry.setParseOp(this->readEnum<OptParseOp>());
  entry.setAttr(this->readEnum<ArgEntryAttr>());
  entry.setXORGroupId(this->read8());
  const auto kind = this->readEnum<ArgEntry::CheckerKind>();
  entry.setShortName(static_cast<char>(this->read8()));
  if (auto str = this->readStr(); !str.empty()) {
    entry.setLongName(str.c_str());
  }
  if (auto str = this->readStr(); !str.empty()) {
    entry.setArgName(str.c_str());
  }
  if (auto str = this->readStr(); !str.empty()) {
    entry.setDefaultValue(std::move(str));
  }
  if (auto str = this->readStr(); !str.empty()) {
    entry.setDetail(str.c_str());
  }

  switch (kind) {
  case ArgEntry::CheckerKind::NOP:
    break;
  case ArgEntry::CheckerKind::INT: {
    auto min = static_cast<int64_t>(this->read64());
    auto max = static_cast<int64_t>(this->read64());
    entry.setIntRange(min, max);
    break;
  }
  case ArgEntry::CheckerKind::CHOICE: {
    unsigned int size = this->read32();
    for (unsigned int i = 0; i < size; i++) {
      auto str = this->readStr();
      entry.addChoice(strdup(str.c_str()));
    }
    break;
  }
  }
  return {std::move(entry), true};
}
// ###########################
// ##     ModuleArchive     ##
// ###########################

ModuleArchive::ModuleArchive(ModId modId, ModAttr attr, std::vector<Archive> &&handles,
                             std::vector<Imported> &&imported, uint64_t seed)
    : modId(modId), attr(attr), handles(std::move(handles)), imported(std::move(imported)) {
  // compute hash
  XXHasher hasher(seed);

  const union {
    struct {
      ModId modId;
      ModAttr attr;
      uint8_t dummy;
    } pack;
    char buf[sizeof(pack)];
  } meta = {.pack = {.modId = this->modId, .attr = this->attr, .dummy = 0}};
  static_assert(sizeof(meta) == sizeof(uint32_t));
  hasher.update(meta.buf, std::size(meta.buf));

  for (auto &e : this->handles) {
    hasher.update(e.getData().c_str(), e.getData().size());
  }

  static_assert(sizeof(Imported) == sizeof(uint64_t));
  for (const auto &e : this->imported) {
    union {
      Imported imported;
      char buf[sizeof(Imported)];
    } data = {.imported = e};
    hasher.update(data.buf, std::size(data.buf));
  }
  this->hash = std::move(hasher).digest();
}

Optional<std::unordered_map<std::string, HandlePtr>> ModuleArchive::unpack(TypePool &pool) const {
  std::unordered_map<std::string, HandlePtr> handleMap;
  for (auto &e : this->getHandles()) {
    auto h = e.unpack(pool);
    if (!h.second) {
      return {};
    }
    if (!handleMap.insert(std::move(h)).second) {
      return {};
    }
  }
  return handleMap;
}

static void resolveTargets(const ModuleArchives &archives, std::vector<ModuleArchivePtr> &targets,
                           const ModuleArchivePtr &archive) {
  targets.push_back(archive);
  for (auto &e : archive->getImported()) {
    auto child = archives.find(e.modId);
    assert(child);
    resolveTargets(archives, targets, child);
  }
}

static void visit(const ModuleArchives &archives, std::vector<ModuleArchivePtr> &ret,
                  std::vector<bool> &used, const ModuleArchivePtr &archive) {
  if (used[toUnderlying(archive->getModId())]) {
    return;
  }
  used[toUnderlying(archive->getModId())] = true;
  for (auto &e : archive->getImported()) {
    auto child = archives.find(e.modId);
    assert(child);
    visit(archives, ret, used, child);
  }
  ret.push_back(archive);
}

static std::vector<ModuleArchivePtr> topologicalSort(const ModuleArchives &archives,
                                                     std::vector<ModuleArchivePtr> &&targets) {
  std::vector<ModuleArchivePtr> ret;
  if (targets.empty()) {
    return ret;
  }

  // sort and uniq
  std::sort(targets.begin(), targets.end(),
            [](const ModuleArchivePtr &x, const ModuleArchivePtr &y) {
              return x->getModId() < y->getModId();
            });
  const auto iter = std::unique(targets.begin(), targets.end(),
                                [](const ModuleArchivePtr &x, const ModuleArchivePtr &y) {
                                  return x->getModId() == y->getModId();
                                });
  targets.erase(iter, targets.end());

  // topological sort
  std::vector<bool> used(toUnderlying(targets.back()->getModId()) + 1, false);
  for (auto &e : targets) {
    visit(archives, ret, used, e);
  }
  return ret;
}

std::vector<ModuleArchivePtr>
ModuleArchive::getDepsByTopologicalOrder(const ModuleArchives &archives) const {
  std::vector<ModuleArchivePtr> targets;
  for (auto &e : this->imported) {
    auto child = archives.find(e.modId);
    assert(child);
    resolveTargets(archives, targets, child);
  }
  return topologicalSort(archives, std::move(targets));
}

static const ModType *getModType(const TypePool &pool, ModId modId) {
  return pool.getModTypeById(modId);
}

static const ModType *load(TypePool &pool, const ModuleArchive &archive) {
  if (const ModType *type = getModType(pool, archive.getModId())) {
    return type;
  }

  FlexBuffer<ModType::Imported> children;

  // add builtin
  auto &builtin = pool.getBuiltinModType();
  children.push_back(builtin.toModEntry(ImportedModKind::GLOBAL));

  for (auto &child : archive.getImported()) {
    auto type = pool.getModTypeById(child.modId);
    assert(type);
    auto e = type->toModEntry(child.kind);
    children.push_back(e);
  }

  auto ret = archive.unpack(pool);
  if (!ret.hasValue()) {
    return nullptr;
  }
  auto handleMap = ret.unwrap();
  return &pool.createModType(archive.getModId(), std::move(handleMap), std::move(children), 0,
                             archive.getModAttr());
}

const ModType *loadFromArchive(const ModuleArchives &archives, TypePool &pool,
                               const ModuleArchive &archive) {
  if (const ModType *type = getModType(pool, archive.getModId())) {
    return type;
  }
  for (auto &dep : archive.getDepsByTopologicalOrder(archives)) {
    TRY(load(pool, *dep));
  }
  return load(pool, archive);
}

ModuleArchivePtr buildArchive(Archiver &&archiver, const ModType &modType,
                              ModuleArchives &archives) {
  // collect handles
  std::vector<std::pair<StringRef, HandlePtr>> udErrors; // for user-defined error type definition
  std::vector<std::pair<StringRef, HandlePtr>> udTypes;  // for user-defined record type definition
  std::vector<std::pair<StringRef, HandlePtr>> handles;  // other handles
  for (const auto &[name, handle] : modType.getHandleMap()) {
    if (handle->isMethodHandle() && cast<MethodHandle>(*handle).isConstructor()) { // record
      udTypes.emplace_back(name, handle);
      continue;
    }
    if (auto &type = archiver.getPool().get(handle->getTypeId());
        handle->is(HandleKind::TYPE_ALIAS) && type.isDerivedErrorType()) {
      if (const auto modId = type.resolveBelongedModId(); !isBuiltinMod(modId)) {
        if (toQualifiedTypeName(name, modId) == type.getNameRef()) { // user-defined error def
          udErrors.emplace_back(name, handle);
          continue;
        }
      }
    }
    handles.emplace_back(name, handle);
  }

  // sort by declaration order
  std::sort(udErrors.begin(), udErrors.end(), [](const auto &x, const auto &y) {
    return x.second->getTypeId() < y.second->getTypeId();
  });
  std::sort(udTypes.begin(), udTypes.end(), [](const auto &x, const auto &y) {
    return x.second->getTypeId() < y.second->getTypeId();
  });
  std::sort(handles.begin(), handles.end(), [](const auto &x, const auto &y) {
    return x.first < y.first; // sort by name
  });

  // pack handles
  std::vector<Archive> handleArchives;
  handleArchives.reserve(udErrors.size() + udTypes.size() + handles.size());
  for (auto &[name, handle] : udErrors) {
    handleArchives.push_back(archiver.pack(name, *handle));
  }
  for (auto &[name, handle] : udTypes) {
    handleArchives.push_back(archiver.pack(name, *handle));
  }
  for (auto &[name, handle] : handles) {
    handleArchives.push_back(archiver.pack(name, *handle));
  }

  // resolve imported modules
  std::vector<ModuleArchive::Imported> imported;
  const unsigned int size = modType.getChildSize();
  for (unsigned int i = 0; i < size; i++) {
    auto e = modType.getChildAt(i);
    auto &type = cast<ModType>(archiver.getPool().get(e.typeId()));
    if (isBuiltinMod(type.getModId())) { // skip builtin module
      continue;
    }
    auto archive = archives.find(type.getModId());
    assert(archive);
    imported.push_back(
        ModuleArchive::Imported::create(e.kind(), archive->getModId(), archive->getHash()));
  }
  std::sort(imported.begin(), imported.end(),
            [](const auto &x, const auto &y) { return x.modId < y.modId; });

  auto archive = std::make_shared<ModuleArchive>(modType.getModId(), modType.getAttr(),
                                                 std::move(handleArchives), std::move(imported),
                                                 archives.getSeed());
  archives.add(archive);
  return archive;
}

// ############################
// ##     ModuleArchives     ##
// ############################

const ModuleArchivePtr ModuleArchives::EMPTY_ARCHIVE = std::make_shared<ModuleArchive>(); // NOLINT

struct ModuleArchiveEntryComp {
  bool operator()(const std::pair<ModId, ModuleArchivePtr> &x, ModId y) const {
    return x.first < y;
  }

  bool operator()(ModId x, const std::pair<ModId, ModuleArchivePtr> &y) const {
    return x < y.first;
  }
};

ModuleArchivePtr ModuleArchives::find(ModId modId) const {
  auto iter =
      std::lower_bound(this->values.begin(), this->values.end(), modId, ModuleArchiveEntryComp());
  if (iter != this->values.end() && iter->first == modId) {
    return iter->second;
  }
  return nullptr;
}

ModuleArchives::iterator_type ModuleArchives::reserveImpl(ModId modId) {
  auto iter =
      std::lower_bound(this->values.begin(), this->values.end(), modId, ModuleArchiveEntryComp());
  if (iter != this->values.end() && iter->first == modId) {
    iter->second = EMPTY_ARCHIVE;
    return iter;
  } else {
    return this->values.emplace(iter, modId, EMPTY_ARCHIVE);
  }
}

static bool isRevertedArchive(const ModuleArchives &archives,
                              std::unordered_set<ModId> &revertingModIdSet,
                              const ModuleArchive &archive) {
  const auto modId = archive.getModId();
  if (revertingModIdSet.find(modId) != revertingModIdSet.end()) {
    return true;
  }
  for (auto &e : archive.getImported()) {
    auto child = archives.find(e.modId);
    if (!child || isRevertedArchive(archives, revertingModIdSet, *child)) {
      revertingModIdSet.emplace(modId);
      return true;
    }
  }
  return false;
}

void ModuleArchives::revert(std::unordered_set<ModId> &&revertingModIdSet) {
  if (!revertingModIdSet.empty()) {
    for (auto &e : this->values) {
      if (e.second && isRevertedArchive(*this, revertingModIdSet, *e.second)) {
        e.second = nullptr;
      }
    }
  }
}

static bool isImported(const ModuleArchives &archives, const ModuleArchive &archive, ModId id) {
  for (auto &e : archive.getImported()) {
    if (e.modId == id) {
      return true;
    }
    if (auto child = archives.find(e.modId); isImported(archives, *child, id)) {
      return true;
    }
  }
  return false;
}

bool ModuleArchives::removeIfUnused(ModId id) {
  for (auto &e : this->values) {
    if (e.second && isImported(*this, *e.second, id)) {
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

} // namespace arsh::lsp