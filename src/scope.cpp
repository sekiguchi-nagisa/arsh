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

#include <algorithm>

#include "logger.h"
#include "misc/files.h"
#include "paths.h"
#include "scope.h"

namespace ydsh {

// #######################
// ##     NameScope     ##
// #######################

NameScope::NameScope(const TypePool &pool, const IntrusivePtr<NameScope> &parent,
                     const ModType &modType)
    : kind(GLOBAL), modId(modType.getModID()), maxVarCount(parent->maxVarCount) {
  assert(parent->isGlobal());
  assert(parent->inBuiltinModule());

  // copy own handle
  for (auto &e : modType.getHandleMap()) {
    this->addNewAlias(std::string(e.first), e.second);
  }

  // import foreign module
  unsigned int size = modType.getChildSize();
  for (unsigned int i = 0; i < size; i++) {
    auto e = modType.getChildAt(i);
    auto &type = cast<ModType>(pool.get(e.typeId()));
    this->importForeignHandles(type, e.kind());
  }
}

static bool isAllowedScopePair(NameScope::Kind parent, NameScope::Kind child) {
  const struct {
    NameScope::Kind parent;
    NameScope::Kind child;
  } table[] = {
      {NameScope::GLOBAL, NameScope::FUNC},
      {NameScope::GLOBAL, NameScope::BLOCK},
      {NameScope::FUNC, NameScope::BLOCK},
      {NameScope::BLOCK, NameScope::BLOCK},
  };
  for (auto &e : table) {
    if (e.parent == parent && e.child == child) {
      return true;
    }
  }
  return false;
}

IntrusivePtr<NameScope> NameScope::enterScope(Kind newKind) {
  if (isAllowedScopePair(this->kind, newKind)) {
    if (this->kind == NameScope::GLOBAL && newKind == NameScope::FUNC) {
      return IntrusivePtr<NameScope>::create(newKind, this->fromThis(), this->maxVarCount);
    } else if (this->kind == NameScope::GLOBAL && newKind == NameScope::BLOCK) {
      return IntrusivePtr<NameScope>::create(newKind, this->fromThis(),
                                             std::ref(this->curLocalIndex));
    } else if (this->kind == NameScope::FUNC && newKind == NameScope::BLOCK) {
      return IntrusivePtr<NameScope>::create(newKind, this->fromThis(),
                                             std::ref(this->curLocalIndex));
    } else if (this->kind == NameScope::BLOCK && newKind == NameScope::BLOCK) {
      auto scope = IntrusivePtr<NameScope>::create(newKind, this->fromThis(), this->maxVarCount);
      scope->curLocalIndex = this->curLocalIndex;
      return scope;
    }
  }
  return nullptr;
}

static bool definedInBuiltin(const NameScope &scope, const std::string &name) {
  if (scope.isGlobal() && scope.parent) {
    if (scope.parent->inBuiltinModule() && scope.parent->contains(name)) {
      assert(scope.parent->isGlobal());
      return true;
    }
  }
  return false;
}

NameLookupResult NameScope::defineHandle(std::string &&name, const DSType &type,
                                         FieldAttribute attr) {
  if (definedInBuiltin(*this, name)) {
    return Err(NameLookupError::DEFINED);
  }
  return this->addNewHandle(std::move(name), type, attr);
}

NameLookupResult NameScope::defineAlias(std::string &&name, const FieldHandle &handle) {
  if (definedInBuiltin(*this, name)) {
    return Err(NameLookupError::DEFINED);
  }
  return this->addNewAlias(std::move(name), handle);
}

NameLookupResult NameScope::defineTypeAlias(const TypePool &pool, std::string &&name,
                                            const DSType &type) {
  if (this->isGlobal()) {
    auto ret = pool.getType(name);
    if (ret) {
      return Err(NameLookupError::DEFINED);
    }
  }
  return this->defineAlias(toTypeAliasFullName(name),
                           FieldHandle::create(0, type, 0, FieldAttribute{}, 0));
}

std::string NameScope::importForeignHandles(const ModType &type, ImportedModKind k) {
  const bool global = hasFlag(k, ImportedModKind::GLOBAL);
  auto holderName = toModHolderName(type.getModID(), global);
  // check if already imported
  {
    auto *handle = this->find(holderName);
    if (handle) {
      assert(handle->isModHolder());
      return "";
    }
  }

  // define module holder
  this->addNewAlias(std::move(holderName), type.toModHolder(k));
  if (!global) {
    return "";
  }

  for (auto &e : type.getHandleMap()) {
    assert(this->modId != e.second.getModID());
    StringRef name = e.first;
    if (name.startsWith("_")) {
      continue;
    }
    const auto &handle = e.second;
    auto ret = this->addNewForeignHandle(name.toString(), handle);
    if (!ret) {
      if (isCmdFullName(name)) {
        name.removeSuffix(strlen(CMD_SYMBOL_SUFFIX));
      } else if (isTypeAliasFullName(name)) {
        name.removeSuffix(strlen(TYPE_ALIAS_SYMBOL_SUFFIX));
      }
      return name.toString();
    }
  }
  return "";
}

static void tryInsertByAscendingOrder(FlexBuffer<ModType::Imported> &children,
                                      ModType::Imported entry) {
  auto iter = std::lower_bound(
      children.begin(), children.end(), entry,
      [](ModType::Imported x, ModType::Imported y) { return x.typeId() < y.typeId(); });
  if (iter != children.end() && iter->typeId() == entry.typeId()) {
    if (entry.isGlobal()) {
      *iter = entry;
    }
  } else {
    children.insert(iter, entry);
  }
}

const ModType &NameScope::toModType(TypePool &pool) const {
  std::unordered_map<std::string, FieldHandle> newHandles;
  FlexBuffer<ModType::Imported> newChildren;

  for (auto &e : this->getHandles()) {
    if (e.second.isModHolder()) {
      auto &modType = pool.get(e.second.getTypeID());
      assert(isa<ModType>(modType));
      ImportedModKind k{};
      if (e.second.has(FieldAttribute::INLINED_MOD)) {
        setFlag(k, ImportedModKind::GLOBAL | ImportedModKind::INLINED);
      } else if (e.second.has(FieldAttribute::GLOBAL_MOD)) {
        setFlag(k, ImportedModKind::GLOBAL);
      }
      tryInsertByAscendingOrder(newChildren, cast<ModType>(modType).toModEntry(k));
    } else if (e.second.getModID() == this->modId) {
      newHandles.emplace(e.first, e.second);
    }
  }
  return pool.createModType(this->modId, std::move(newHandles), std::move(newChildren),
                            this->getMaxGlobalVarIndex());
}

const FieldHandle *NameScope::lookup(const std::string &name) const {
  for (auto *scope = this; scope != nullptr; scope = scope->parent.get()) {
    auto *handle = scope->find(name);
    if (handle) {
      return handle;
    }
  }
  return nullptr;
}

void NameScope::discard(ScopeDiscardPoint discardPoint) {
  for (auto iter = this->handles.begin(); iter != this->handles.end();) {
    if (iter->second.getCommitID() >= discardPoint.commitIdOffset) {
      iter = this->handles.erase(iter);
    } else {
      ++iter;
    }
  }
  if (this->isGlobal()) {
    this->clearLocalSize();
  }
}

NameLookupResult NameScope::add(std::string &&name, FieldHandle &&handle) {
  assert(this->kind != FUNC);

  if (handle.getTypeID() == static_cast<unsigned int>(TYPE::Nothing) ||
      handle.getTypeID() == static_cast<unsigned int>(TYPE::Void)) {
    return Err(NameLookupError::INVALID_TYPE);
  }

  // check var index limit
  if (!handle.has(FieldAttribute::ALIAS)) {
    if (!handle.has(FieldAttribute::GLOBAL)) {
      assert(!this->isGlobal());
      if (this->curLocalIndex == SYS_LIMIT_LOCAL_NUM) {
        return Err(NameLookupError::LIMIT);
      }
    }
  }

  auto pair = this->handles.emplace(std::move(name), handle);
  if (!pair.second) {
    return Err(NameLookupError::DEFINED);
  }

  // increment var index count
  if (!handle.has(FieldAttribute::ALIAS)) {
    assert(this->isGlobal() == handle.has(FieldAttribute::GLOBAL));
    if (handle.has(FieldAttribute::GLOBAL)) {
      this->maxVarCount.get()++;
    } else { // local
      assert(this->kind == BLOCK);
      this->curLocalIndex++;
      this->localSize++;

      if (this->curLocalIndex > this->maxVarCount.get()) {
        this->maxVarCount.get() = this->curLocalIndex;
      }
    }
  }
  return Ok(&pair.first->second);
}

// ##############################
// ##     ModuleLoaderBase     ##
// ##############################

static std::string concatPath(const char *baseDir, const char *path) {
  if (!*path) {
    return "";
  }
  std::string value;
  if (!baseDir || *path == '/') {
    value = path;
  } else {
    assert(*baseDir);
    value = baseDir;
    value += '/';
    value += path;
  }
  return value;
}

static CStrPtr tryToRealpath(const std::string &path) {
  auto ret = getRealpath(path.c_str());
  if (!ret) {
    ret.reset(strdup(path.c_str()));
  }
  return ret;
}

static int checkFileType(const struct stat &st, ModLoadOption option) {
  if (S_ISDIR(st.st_mode)) {
    return EISDIR;
  } else if (S_ISREG(st.st_mode)) {
    if (st.st_size > static_cast<off_t>(static_cast<uint32_t>(-1) >> 2)) {
      return EFBIG;
    }
  } else if (hasFlag(option, ModLoadOption::IGNORE_NON_REG_FILE)) {
    return EINVAL;
  }
  return 0;
}

ModResult ModuleLoaderBase::loadImpl(const char *scriptDir, const char *modPath, FilePtr &filePtr,
                                     ModLoadOption option) {
  assert(modPath);

  auto str = concatPath(scriptDir, modPath);
  LOG(TRACE_MODULE, "\n    scriptDir: `%s'\n    modPath: `%s'\n    fullPath: `%s'",
      (scriptDir == nullptr ? "(null)" : scriptDir), modPath,
      !str.empty() ? str.c_str() : "(null)");
  // check file type
  if (str.empty()) {
    return ModLoadingError(ENOENT);
  }

  /**
   * set O_NONBLOCK due to prevent named pipe blocking
   */
  int fd = open(str.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    LOG(TRACE_MODULE, "open failed: `%s'", strerror(errno));
    return ModLoadingError(errno);
  }

  struct stat st; // NOLINT
  if (fstat(fd, &st) != 0) {
    int old = errno;
    close(fd);
    LOG(TRACE_MODULE, "fstat failed: `%s'", strerror(old));
    return ModLoadingError(old);
  }
  int s = checkFileType(st, option);
  if (s) {
    close(fd);
    LOG(TRACE_MODULE, "checkFileType failed: `%s'", strerror(s));
    return ModLoadingError(s);
  }
  setFDFlag(fd, O_NONBLOCK, false);

  // resolve fullpath
  auto path = tryToRealpath(str);
  auto ret = this->addNewModEntry(std::move(path));
  if (is<ModLoadingError>(ret)) {
    close(fd);
  } else {
    filePtr = createFilePtr(fdopen, fd, "rb");
    assert(filePtr);
  }
  return ret;
}

static bool isFileNotFound(const ModResult &ret) {
  return is<ModLoadingError>(ret) && get<ModLoadingError>(ret).isFileNotFound();
}

ModResult ModuleLoaderBase::load(const char *scriptDir, const char *path, FilePtr &filePtr,
                                 ModLoadOption option) {
  auto ret = this->loadImpl(scriptDir, path, filePtr, option);
  if (path[0] == '/' || scriptDir == nullptr ||
      scriptDir[0] != '/') { // if full path, not search next path
    return ret;
  }
  if (strcmp(scriptDir, SYSTEM_MOD_DIR) == 0) {
    return ret;
  }

  if (isFileNotFound(ret)) {
    const char *localModDir = getFullLocalModDir();
    if (strcmp(scriptDir, localModDir) != 0) {
      ret = this->loadImpl(localModDir, path, filePtr, option);
    }
    if (isFileNotFound(ret)) {
      ret = this->loadImpl(SYSTEM_MOD_DIR, path, filePtr, option);
    }
  }
  return ret;
}

// ##########################
// ##     ModuleLoader     ##
// ##########################

void ModuleLoader::discard(const ModDiscardPoint discardPoint) {
  if (discardPoint.idCount >= this->modSize()) {
    return; // do nothing
  }

  this->entries.erase(this->entries.begin() + discardPoint.idCount, this->entries.end());
  for (auto iter = this->indexMap.begin(); iter != this->indexMap.end();) {
    if (iter->second >= discardPoint.idCount) {
      const char *ptr = iter->first.data();
      iter = this->indexMap.erase(iter);
      free(const_cast<char *>(ptr));
    } else {
      ++iter;
    }
  }
  this->gvarCount = discardPoint.gvarCount;
}

IntrusivePtr<NameScope> ModuleLoader::createGlobalScope(const char *name, const ModType *modType) {
  auto str = CStrPtr(strdup(name));
  auto ret = this->addNewModEntry(std::move(str));
  assert(is<const char *>(ret));

  if (modType) {
    return this->createGlobalScopeFromFullpath(get<const char *>(ret), *modType);
  } else {
    return IntrusivePtr<NameScope>::create(std::ref(this->gvarCount));
  }
}

IntrusivePtr<NameScope> ModuleLoader::createGlobalScopeFromFullpath(StringRef fullpath,
                                                                    const ModType &modType) {
  auto iter = this->indexMap.find(fullpath);
  if (iter != this->indexMap.end()) {
    auto scope = IntrusivePtr<NameScope>::create(std::ref(this->gvarCount), iter->second);
    scope->importForeignHandles(modType, ImportedModKind::GLOBAL);
    return scope;
  }
  return nullptr;
}

const ModType &ModuleLoader::createModType(TypePool &pool, const NameScope &scope,
                                           const std::string &fullpath) {
  auto &modType = scope.toModType(pool);
  this->gvarCount++; // reserve module object entry
  auto iter = this->indexMap.find(fullpath);
  assert(iter != this->indexMap.end());
  auto &e = this->entries[iter->second];
  assert(!e.isSealed());
  e.setModType(modType);
  return modType;
}

ModResult ModuleLoader::addNewModEntry(CStrPtr &&ptr) {
  StringRef key(ptr.get());
  auto pair = this->indexMap.emplace(key, this->indexMap.size());
  if (pair.second) {
    this->entries.push_back(ModEntry::create());
  } else { // already registered
    auto &e = this->entries[pair.first->second];
    if (e.isSealed()) {
      return e.getTypeId();
    }
    return ModLoadingError(0);
  }
  if (this->indexMap.size() == MAX_MOD_NUM) {
    fatal("module id reaches limit(%u)\n", MAX_MOD_NUM);
  }
  return ptr.release();
}

} // namespace ydsh