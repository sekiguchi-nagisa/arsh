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

NameScopePtr NameScope::reopen(const TypePool &pool, const NameScope &parent,
                               const ModType &modType) {
  assert(parent.isGlobal());
  assert(parent.inRootModule());
  auto scope = NameScopePtr::create(parent.maxVarCount, modType.getModId());
  assert(scope->modId == modType.getModId());

  // copy own handle
  for (auto &e : modType.getHandleMap()) {
    assert(scope->modId == e.second->getModId());
    scope->addNewAliasHandle(std::string(e.first), e.second);
  }

  // import foreign module
  unsigned int size = modType.getChildSize();
  for (unsigned int i = 0; i < size; i++) {
    auto e = modType.getChildAt(i);
    auto &type = cast<ModType>(pool.get(e.typeId()));
    scope->importForeignHandles(pool, type, e.kind());
  }
  return scope;
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
  return std::any_of(std::begin(table), std::end(table),
                     [&](auto &e) { return e.parent == parent && e.child == child; });
}

NameScopePtr NameScope::enterScope(Kind newKind) {
  if (isAllowedScopePair(this->kind, newKind)) {
    if (this->kind == NameScope::GLOBAL && newKind == NameScope::FUNC) {
      return NameScope::block(newKind, this->fromThis(), this->maxVarCount);
    } else if (this->kind == NameScope::GLOBAL && newKind == NameScope::BLOCK) {
      return NameScope::block(newKind, this->fromThis(), std::ref(this->curLocalIndex));
    } else if (this->kind == NameScope::FUNC && newKind == NameScope::BLOCK) {
      return NameScope::block(newKind, this->fromThis(), std::ref(this->curLocalIndex));
    } else if (this->kind == NameScope::BLOCK && newKind == NameScope::BLOCK) {
      auto scope = NameScope::block(newKind, this->fromThis(), this->maxVarCount);
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

NameRegisterResult NameScope::defineHandle(std::string &&name, const DSType &type,
                                           HandleAttr attr) {
  if (definedInBuiltin(*this, name)) {
    return Err(NameRegisterError::DEFINED);
  }
  if (type.isUnresolved()) {
    return Err(NameRegisterError::INVALID_TYPE);
  }
  return this->addNewHandle(std::move(name), type, attr);
}

NameRegisterResult NameScope::defineAlias(std::string &&name, const HandlePtr &handle) {
  if (definedInBuiltin(*this, name)) {
    return Err(NameRegisterError::DEFINED);
  }
  return this->addNewAliasHandle(std::move(name), handle);
}

NameRegisterResult NameScope::defineTypeAlias(const TypePool &pool, const std::string &name,
                                              const DSType &type) {
  if (this->isGlobal()) {
    auto ret = pool.getType(name);
    if (ret) {
      return Err(NameRegisterError::DEFINED);
    }
  }
  return this->defineAlias(toTypeAliasFullName(name),
                           HandlePtr::create(type, 0, HandleAttr::TYPE_ALIAS, this->modId));
}

NameRegisterResult NameScope::defineMethod(const DSType &recvType, const std::string &name,
                                           const DSType &returnType,
                                           const std::vector<const DSType *> &paramTypes) {
  if (!this->isGlobal() || recvType.isNothingType() || recvType.isVoidType() ||
      recvType.isUnresolved()) {
    return Err(NameRegisterError::INVALID_TYPE);
  }
  std::string fullname = toMethodFullName(recvType.typeId(), name);
  const unsigned int index = this->getMaxGlobalVarIndex();
  auto hande = MethodHandle::create(recvType, index, returnType, paramTypes, this->modId);
  return this->add(std::move(fullname), HandlePtr(hande.release()));
}

std::string NameScope::importForeignHandles(const TypePool &pool, const ModType &type,
                                            ImportedModKind k) {
  const bool global = hasFlag(k, ImportedModKind::GLOBAL);
  auto holderName = toModHolderName(type.getModId(), global);
  if (auto *handle = this->findMut(holderName); handle) { // check if already imported
    assert(handle->isModHolder());
    if (!handle->has(HandleAttr::INLINED_MOD) && hasFlag(k, ImportedModKind::INLINED)) {
      auto attr = handle->attr();
      unsetFlag(attr, HandleAttr::GLOBAL_MOD);
      setFlag(attr, HandleAttr::INLINED_MOD);
      handle->setAttr(attr);
    }
    return "";
  }

  // define module holder
  this->addNewAliasHandle(std::move(holderName), type.toModHolder(k, this->modId));

  // import actual handles
  for (auto &e : type.getHandleMap()) {
    assert(this->modId != e.second->getModId());
    if (!global && !e.second->isMethod()) {
      continue; // in named import, not import Handle (except for MethodHandle)
    }
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

  // resolve inlined imported symbols
  unsigned int size = type.getChildSize();
  for (unsigned int i = 0; i < size; i++) {
    auto child = type.getChildAt(i);
    if (child.isInlined()) {
      auto &childType = pool.get(child.typeId());
      assert(isa<ModType>(childType));
      auto ret = this->importForeignHandles(pool, cast<ModType>(childType), k);
      if (!ret.empty()) {
        return ret;
      }
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
  std::unordered_map<std::string, HandlePtr> newHandles;
  FlexBuffer<ModType::Imported> newChildren;

  for (auto &e : this->getHandles()) {
    auto &handle = e.second.first;
    if (handle->isModHolder()) {
      auto &modType = pool.get(handle->getTypeId());
      assert(isa<ModType>(modType));
      ImportedModKind k{};
      if (handle->has(HandleAttr::INLINED_MOD)) {
        setFlag(k, ImportedModKind::GLOBAL | ImportedModKind::INLINED);
      } else if (handle->has(HandleAttr::GLOBAL_MOD)) {
        setFlag(k, ImportedModKind::GLOBAL);
      }
      tryInsertByAscendingOrder(newChildren, cast<ModType>(modType).toModEntry(k));
    } else if (handle->getModId() == this->modId) {
      newHandles.emplace(e.first, handle);
    }
  }
  return pool.createModType(this->modId, std::move(newHandles), std::move(newChildren),
                            this->getMaxGlobalVarIndex());
}

const Handle *NameScope::lookup(const std::string &name) const {
  for (auto *scope = this; scope != nullptr; scope = scope->parent.get()) {
    auto *handle = scope->find(name);
    if (handle) {
      return handle;
    }
  }
  return nullptr;
}

Result<const Handle *, NameLookupError> NameScope::lookupField(const TypePool &pool,
                                                               const DSType &recv,
                                                               const std::string &fieldName) const {
  auto *handle = recv.lookupField(pool, fieldName);
  if (handle) {
    if (handle->getModId() == 0 || this->modId == handle->getModId() || fieldName[0] != '_') {
      return Ok(handle);
    } else {
      return Err(NameLookupError::MOD_PRIVATE);
    }
  }
  return Err(NameLookupError::NOT_FOUND);
}

const MethodHandle *NameScope::lookupMethod(TypePool &pool, const DSType &recvType,
                                            const std::string &methodName) const {
  auto scope = this;
  while (!scope->isGlobal()) {
    scope = scope->parent.get();
  }
  for (const DSType *type = &recvType; type != nullptr; type = type->getSuperType()) {
    std::string name = toMethodFullName(type->typeId(), methodName);
    if (auto *handle = scope->find(name)) {
      assert(handle->isMethod());
      if (handle->getModId() == 0 || this->modId == handle->getModId() || methodName[0] != '_') {
        return static_cast<const MethodHandle *>(handle);
      }
      return nullptr;
    }
  }
  return pool.lookupMethod(recvType, methodName);
}

void NameScope::discard(ScopeDiscardPoint discardPoint) {
  for (auto iter = this->handles.begin(); iter != this->handles.end();) {
    if (iter->second.second >= discardPoint.commitIdOffset) {
      iter = this->handles.erase(iter);
    } else {
      ++iter;
    }
  }
  if (this->isGlobal()) {
    this->clearLocalSize();
  }
}

NameRegisterResult NameScope::add(std::string &&name, HandlePtr &&handle, NameRegisterOp op) {
  assert(this->kind != FUNC);

  // check var index limit
  if (!hasFlag(op, NameRegisterOp::AS_ALIAS)) {
    if (!handle->has(HandleAttr::GLOBAL)) {
      assert(!this->isGlobal());
      if (this->curLocalIndex == SYS_LIMIT_LOCAL_NUM) {
        return Err(NameRegisterError::LIMIT);
      }
    }
  }

  const auto comitId = this->handles.size();
  auto pair = this->handles.emplace(std::move(name), std::make_pair(handle, comitId));
  if (!pair.second) {
    if (hasFlag(op, NameRegisterOp::IGNORE_CONFLICT) && pair.first->second.first == handle &&
        handle->isMethod()) {
      return Ok(handle.get());
    }
    return Err(NameRegisterError::DEFINED);
  }

  // increment var index count
  if (!hasFlag(op, NameRegisterOp::AS_ALIAS)) {
    assert(this->isGlobal() == handle->has(HandleAttr::GLOBAL));
    if (handle->has(HandleAttr::GLOBAL)) {
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
  return Ok(pair.first->second.first.get());
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
  if (this->sysConfig.getModuleDir() == scriptDir) {
    return ret;
  }

  if (isFileNotFound(ret)) {
    const auto &localModDir = this->sysConfig.getModuleHome();
    if (localModDir != scriptDir) {
      ret = this->loadImpl(localModDir.c_str(), path, filePtr, option);
    }
    if (isFileNotFound(ret)) {
      ret = this->loadImpl(this->sysConfig.getModuleDir().c_str(), path, filePtr, option);
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

NameScopePtr ModuleLoader::createGlobalScope(const TypePool &pool, const char *name,
                                             const ModType *modType) {
  auto str = CStrPtr(strdup(name));
  auto ret = this->addNewModEntry(std::move(str));
  assert(is<const char *>(ret));

  if (modType) {
    return this->createGlobalScopeFromFullpath(pool, get<const char *>(ret), *modType);
  } else {
    return NameScopePtr::create(std::ref(this->gvarCount));
  }
}

NameScopePtr ModuleLoader::createGlobalScopeFromFullpath(const TypePool &pool, StringRef fullpath,
                                                         const ModType &modType) {
  auto iter = this->indexMap.find(fullpath);
  if (iter != this->indexMap.end()) {
    auto scope = NameScopePtr::create(std::ref(this->gvarCount), iter->second);
    scope->importForeignHandles(pool, modType, ImportedModKind::GLOBAL);
    return scope;
  }
  return nullptr;
}

const ModType &ModuleLoader::createModType(TypePool &pool, const NameScope &scope) {
  assert(scope.modId < this->entries.size());
  assert(scope.isGlobal());
  auto &modType = scope.toModType(pool);
  bool reopened = scope.inRootModule() && this->entries[scope.modId].isSealed();
  if (!reopened) {
    this->gvarCount++; // reserve module object entry
    auto &e = this->entries[scope.modId];
    assert(!e.isSealed());
    e.setModType(modType);
  }
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