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
#include "misc/files.hpp"
#include "paths.h"
#include "scope.h"

namespace arsh {

// #######################
// ##     NameScope     ##
// #######################

NameScopePtr NameScope::reopen(const TypePool &pool, const NameScope &parent,
                               const ModType &modType) {
  assert(parent.isGlobal());
  assert(parent.inRootModule());
  auto scope = NameScopePtr::create(parent.maxVarCount, modType.getIndex(), modType.getModId());
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

NameScopePtr NameScope::cloneGlobal() const {
  if (!this->isGlobal()) {
    return nullptr;
  }
  auto newScope = NameScopePtr ::create(this->maxVarCount, this->modIndex, this->modId);
  newScope->handles = this->handles;
  return newScope;
}

NameScopePtr NameScope::enterScope(Kind newKind) {
  if (this->kind == NameScope::GLOBAL && newKind == NameScope::FUNC) {
    return NameScope::block(newKind, this->fromThis(), this->maxVarCount);
  } else if (this->kind == NameScope::GLOBAL && newKind == NameScope::BLOCK) {
    return NameScope::block(newKind, this->fromThis(), std::ref(this->curLocalIndex));
  } else if (this->kind == NameScope::FUNC && newKind == NameScope::BLOCK) {
    return NameScope::block(newKind, this->fromThis(), std::ref(this->curLocalIndex));
  } else if (this->kind == NameScope::BLOCK && newKind == NameScope::FUNC) { // for closure
    return NameScope::block(newKind, this->fromThis(), this->maxVarCount);
  } else if (this->kind == NameScope::BLOCK && newKind == NameScope::BLOCK) {
    auto scope = NameScope::block(newKind, this->fromThis(), this->maxVarCount);
    scope->curLocalIndex = this->curLocalIndex;
    return scope;
  } else {
    return nullptr;
  }
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

NameRegisterResult NameScope::defineHandle(std::string &&name, const Type &type, HandleKind k,
                                           HandleAttr attr) {
  if (definedInBuiltin(*this, name)) {
    return Err(NameRegisterError::DEFINED);
  }
  if (type.isUnresolved()) {
    return Err(NameRegisterError::INVALID_TYPE);
  }
  return this->addNewHandle(std::move(name), type, k, attr);
}

NameRegisterResult NameScope::defineAlias(std::string &&name, const HandlePtr &handle) {
  if (definedInBuiltin(*this, name)) {
    return Err(NameRegisterError::DEFINED);
  }
  return this->addNewAliasHandle(std::move(name), handle);
}

NameRegisterResult NameScope::defineTypeAlias(const TypePool &pool, const std::string &name,
                                              const Type &type) {
  if (this->isGlobal()) {
    if (pool.getType(name) || pool.getTypeTemplate(name)) {
      return Err(NameRegisterError::DEFINED);
    }
  }
  return this->defineAlias(
      toTypeAliasFullName(name),
      HandlePtr::create(type, 0, HandleKind::TYPE_ALIAS, HandleAttr{}, this->modId));
}

NameRegisterResult NameScope::defineNamedFunction(const std::string &name,
                                                  const FunctionType &funcType,
                                                  PackedParamNames &&packed) {
  if (!this->isGlobal()) {
    return Err(NameRegisterError::INVALID_TYPE); // normally unreachable
  }
  if (definedInBuiltin(*this, name)) {
    return Err(NameRegisterError::DEFINED);
  }
  const unsigned int index = this->getMaxGlobalVarIndex();
  auto handle = FuncHandle::create(funcType, index, std::move(packed), this->modId);
  return this->add(std::string(name), HandlePtr(handle.release()));
}

NameRegisterResult NameScope::defineMethod(const TypePool &pool, const Type &recvType,
                                           const std::string &name, const Type &returnType,
                                           const std::vector<const Type *> &paramTypes,
                                           PackedParamNames &&packed) {
  if (!this->isGlobal() || recvType.isNothingType() || recvType.isVoidType() ||
      recvType.isUnresolved()) {
    return Err(NameRegisterError::INVALID_TYPE);
  }
  if (isBuiltinMod(recvType.resolveBelongedModId())) {
    if (pool.hasMethod(recvType, name)) {
      return Err(NameRegisterError::DEFINED);
    }
  }
  std::string fullname = toMethodFullName(recvType.typeId(), name);
  const unsigned int index = this->getMaxGlobalVarIndex();
  std::unique_ptr<MethodHandle> handle;
  if (name == OP_INIT) {
    handle = MethodHandle::constructor(recvType, index, paramTypes, std::move(packed), this->modId);
  } else {
    handle = MethodHandle::method(recvType, index, returnType, paramTypes, std::move(packed),
                                  this->modId);
  }
  return this->add(std::move(fullname), HandlePtr(handle.release()));
}

NameRegisterResult NameScope::defineConst(std::string &&name, ConstEntry entry) {
  if (definedInBuiltin(*this, name)) {
    return Err(NameRegisterError::DEFINED);
  }
  HandleAttr attr = HandleAttr::READ_ONLY;
  if (this->isGlobal()) {
    setFlag(attr, HandleAttr::GLOBAL);
  }

  TYPE typeId = TYPE::Any;
  switch (entry.data.k) {
  case ConstEntry::BOOL:
    typeId = TYPE::Bool;
    break;
  case ConstEntry::SIG:
    typeId = TYPE::Signal;
    break;
  case ConstEntry::NONE:
    typeId = TYPE::OptNothing;
    break;
  }
  auto handle = HandlePtr::create(toUnderlying(typeId), entry.u32, HandleKind::SMALL_CONST, attr,
                                  this->modId);
  return this->add(std::move(name), std::move(handle), NameRegisterOp::AS_ALIAS);
}

static bool isSameModuleMethod(const TypePool &pool, const Handle &handle) {
  if (!handle.isMethodHandle()) {
    return false;
  }
  auto &methodHandle = cast<MethodHandle>(handle);
  auto &recv = pool.get(methodHandle.getRecvTypeId());
  return recv.resolveBelongedModId() == handle.getModId();
}

std::string NameScope::importForeignHandles(const TypePool &pool, const ModType &type,
                                            ImportedModKind k) {
  const bool global = hasFlag(k, ImportedModKind::GLOBAL);
  auto holderName = toModHolderName(type.getModId(), global);
  if (auto *handle = this->findMut(holderName)) { // check if already imported
    assert(handle->isModHolder());
    if (!handle->is(HandleKind::INLINED_MOD) && hasFlag(k, ImportedModKind::INLINED)) {
      handle->setKind(HandleKind::INLINED_MOD);
    }
    return "";
  }

  // define module holder
  this->addNewAliasHandle(std::move(holderName), type.toModHolder(k, this->modId));

  // import actual handles
  for (auto &e : type.getHandleMap()) {
    assert(this->modId != e.second->getModId());
    if (!global && !e.second->is(HandleKind::CONSTRUCTOR) && !isSameModuleMethod(pool, *e.second)) {
      /**
       * in named import, does not import handles except for the following
       *   * constructor
       *   * user-defined method that defined in same module as receiver type
       */
      continue;
    }
    StringRef name = e.first;
    if (name.startsWith("_") && !e.second->isMethodHandle()) { // not skip method handle
      continue;
    }
    const auto &handle = e.second;
    if (!this->addNewForeignHandle(name.toString(), handle)) {
      const char *suffix = "";
      std::string message;
      if (isCmdFullName(name)) {
        suffix = " command";
        name.removeSuffix(strlen(CMD_SYMBOL_SUFFIX));
      } else if (isTypeAliasFullName(name)) {
        suffix = " type";
        name.removeSuffix(strlen(TYPE_ALIAS_SYMBOL_SUFFIX));
      } else if (isMethodFullName(name)) {
        suffix = " method";
        name = trimMethodFullNameSuffix(name);
      }
      message += "`";
      message += name;
      message += "'";
      message += suffix;
      return message;
    }
  }

  // resolve inlined imported symbols
  const unsigned int size = type.getChildSize();
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
      if (handle->is(HandleKind::INLINED_MOD)) {
        setFlag(k, ImportedModKind::GLOBAL | ImportedModKind::INLINED);
      } else if (handle->is(HandleKind::GLOBAL_MOD)) {
        setFlag(k, ImportedModKind::GLOBAL);
      }
      tryInsertByAscendingOrder(newChildren, cast<ModType>(modType).toModEntry(k));
    } else if (handle->getModId() == this->modId) {
      newHandles.emplace(e.first, handle);
    }
  }
  return pool.createModType(this->modId, std::move(newHandles), std::move(newChildren),
                            this->modIndex, this->modAttr);
}

Result<HandlePtr, NameLookupError> NameScope::lookup(const std::string &name) const {
  for (auto *scope = this; scope != nullptr; scope = scope->parent.get()) {
    auto handle = scope->find(name);
    if (handle) {
      return Ok(std::move(handle));
    }
  }
  return Err(NameLookupError::NOT_FOUND);
}

Result<HandlePtr, NameLookupError> NameScope::lookupAndCaptureUpVar(const std::string &name) {
  constexpr unsigned int funcScopeSize = SYS_LIMIT_FUNC_DEPTH + 1;
  NameScope *funcScopes[funcScopeSize];
  unsigned int funcScopeDepth = 0;

  for (auto *scope = this; scope != nullptr; scope = scope->parent.get()) {
    auto handle = scope->find(name);
    if (!handle) {
      if (scope->isFunc()) {
        assert(funcScopeDepth < funcScopeSize);
        funcScopes[funcScopeDepth++] = scope;
      }
      continue;
    }

    if (!handle->has(HandleAttr::GLOBAL) && !handle->is(HandleKind::TYPE_ALIAS)) {
      for (; funcScopeDepth; funcScopeDepth--) {
        if (handle->has(HandleAttr::UNCAPTURED)) {
          if (scope->parent->isFunc()) { // may be constructor
            return Err(NameLookupError::UNCAPTURE_FIELD);
          } else {
            assert(handle->is(HandleKind::ENV));
            return Err(NameLookupError::UNCAPTURE_ENV);
          }
        }

        auto *curFuncScope = funcScopes[funcScopeDepth - 1];
        unsigned int captureIndex = curFuncScope->captures.size();
        if (captureIndex == SYS_LIMIT_UPVAR_NUM) {
          return Err(NameLookupError::UPVAR_LIMIT);
        }

        auto attr = handle->attr();
        setFlag(attr, HandleAttr::UPVAR);
        if (!handle->has(HandleAttr::READ_ONLY)) {
          auto newAttr = handle->attr();
          setFlag(newAttr, HandleAttr::BOXED);
          handle->setAttr(newAttr);
        }
        curFuncScope->captures.push_back(handle);
        auto upVar = HandlePtr::create(handle->getTypeId(), captureIndex, handle->getKind(), attr,
                                       handle->getModId());
        auto ret = curFuncScope->add(std::string(name), std::move(upVar), NameRegisterOp::AS_ALIAS);
        assert(ret);
        handle = std::move(ret).take();
      }
    }
    return Ok(std::move(handle));
  }
  return Err(NameLookupError::NOT_FOUND);
}

Result<HandlePtr, NameLookupError> NameScope::lookupField(const TypePool &pool, const Type &recv,
                                                          const std::string &fieldName) const {
  auto handle = recv.lookupField(pool, fieldName);
  if (handle) {
    if (handle->isVisibleInMod(this->modId, fieldName)) {
      return Ok(std::move(handle));
    } else {
      return Err(NameLookupError::MOD_PRIVATE);
    }
  }
  return Err(NameLookupError::NOT_FOUND);
}

Result<const MethodHandle *, NameLookupError>
NameScope::lookupMethod(TypePool &pool, const Type &recvType, const std::string &methodName) const {
  auto *globalScope = this->getGlobalScope();
  for (const Type *type = &recvType; type != nullptr; type = type->getSuperType()) {
    std::string name = toMethodFullName(type->typeId(), methodName);
    if (auto handle = globalScope->find(name)) {
      assert(handle->isMethodHandle());
      if (handle->isVisibleInMod(this->modId, methodName)) {
        return Ok(cast<MethodHandle>(handle.get()));
      }
      return Err(NameLookupError::MOD_PRIVATE);
    }
  }
  if (auto *handle = pool.lookupMethod(recvType, methodName)) {
    return Ok(handle);
  }
  return Err(NameLookupError::NOT_FOUND);
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
  // check var index limit
  if (!hasFlag(op, NameRegisterOp::AS_ALIAS)) {
    if (handle->has(HandleAttr::GLOBAL)) {
      if (this->maxVarCount.get() == SYS_LIMIT_GLOBAL_NUM) {
        return Err(NameRegisterError::GLOBAL_LIMIT);
      }
    } else { // local
      assert(!this->isGlobal());
      assert(!this->isFunc());
      if (this->curLocalIndex == SYS_LIMIT_LOCAL_NUM) {
        return Err(NameRegisterError::LOCAL_LIMIT);
      }
    }
  }

  const auto commitId = this->handles.size();
  auto pair = this->handles.emplace(std::move(name), std::make_pair(handle, commitId));
  if (!pair.second) {
    if (hasFlag(op, NameRegisterOp::IGNORE_CONFLICT) && pair.first->second.first == handle &&
        handle->isMethodHandle()) {
      return Ok(std::move(handle));
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
      this->maxVarCount.get() = std::max(this->maxVarCount.get(), this->curLocalIndex);
    }
  }
  return Ok(pair.first->second.first);
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
    if (st.st_size > static_cast<off_t>(SYS_LIMIT_INPUT_SIZE)) {
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
  int fd = open(str.c_str(), O_RDONLY | O_NONBLOCK);
  if (fd < 0 || !remapFDCloseOnExec(fd)) {
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
  if (const int s = checkFileType(st, option)) {
    close(fd);
    LOG(TRACE_MODULE, "checkFileType failed: `%s'", strerror(s));
    return ModLoadingError(s);
  }
  setFDFlag(fd, O_NONBLOCK, false);

  // resolve full-path
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
      iter = this->indexMap.erase(iter);
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
    return this->createGlobalScopeFromFullPath(pool, get<const char *>(ret), *modType);
  } else {
    unsigned int modIndex = this->gvarCount++;
    return NameScopePtr::create(std::ref(this->gvarCount), modIndex, BUILTIN_MOD_ID);
  }
}

NameScopePtr ModuleLoader::createGlobalScopeFromFullPath(const TypePool &pool, StringRef fullPath,
                                                         const ModType &modType) {
  auto iter = this->indexMap.find(fullPath);
  if (iter != this->indexMap.end()) {
    unsigned int modIndex = this->gvarCount++;
    auto scope = NameScopePtr::create(std::ref(this->gvarCount), modIndex,
                                      ModId{static_cast<unsigned short>(iter->second)});
    scope->importForeignHandles(pool, modType, ImportedModKind::GLOBAL);
    return scope;
  }
  return nullptr;
}

const ModType &ModuleLoader::createModType(TypePool &pool, const NameScope &scope) {
  assert(toUnderlying(scope.modId) < this->entries.size());
  assert(scope.isGlobal());
  auto &modType = scope.toModType(pool);
  bool reopened = scope.inRootModule() && (*this)[scope.modId].second.isSealed();
  if (!reopened) {
    auto &e = this->entries[toUnderlying(scope.modId)].second;
    assert(!e.isSealed());
    e.setModType(modType);
  }
  return modType;
}

ModResult ModuleLoader::addNewModEntry(CStrPtr &&ptr) {
  StringRef key(ptr.get());
  auto pair = this->indexMap.emplace(key, this->indexMap.size());
  if (pair.second) {
    if (this->indexMap.size() > MAX_MOD_NUM) {
      this->indexMap.erase(key);
      return ModLoadingError(ModLoadingError::MOD_LIMIT);
    }
    this->entries.emplace_back(std::move(ptr), ModEntry());
  } else { // already registered
    auto &e = this->entries[pair.first->second].second;
    if (e.isSealed()) {
      return e.getTypeId();
    }
    return ModLoadingError(ModLoadingError::CIRCULAR_LOAD);
  }
  return key.data();
}

} // namespace arsh