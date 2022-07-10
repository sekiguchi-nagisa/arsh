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

#ifndef YDSH_SCOPE_H
#define YDSH_SCOPE_H

#include <cassert>
#include <functional>

#include "misc/resource.hpp"
#include "sysconfig.h"
#include "type_pool.h"

namespace ydsh {

// for name lookup

enum class NameRegisterError {
  DEFINED,
  LOCAL_LIMIT,
  GLOBAL_LIMIT,
  INVALID_TYPE,
};

using NameRegisterResult = Result<HandlePtr, NameRegisterError>;

enum class NameLookupError {
  NOT_FOUND,
  MOD_PRIVATE,
  UPVAR_LIMIT,
  UNCAPTURE_ENV,
  UNCAPTURE_FIELD,
};

enum class NameRegisterOp : unsigned int {
  AS_ALIAS = 1u << 0u,
  IGNORE_CONFLICT = 1u << 1u,
};

template <>
struct allow_enum_bitop<NameRegisterOp> : std::true_type {};

struct ScopeDiscardPoint {
  unsigned int commitIdOffset;
};

class NameScope;

using NameScopePtr = IntrusivePtr<NameScope>;

class NameScope : public RefCount<NameScope> {
private:
  ModAttr modAtter{}; // for global scope (module scope)

public:
  const enum Kind : unsigned char {
    GLOBAL,
    FUNC,
    BLOCK,
  } kind;

  /**
   * indicates belonged module id
   */
  const unsigned short modId;

  /**
   * for module object
   */
  const unsigned int modIndex{0};

  /**
   * may be null
   */
  const NameScopePtr parent;

private:
  unsigned int curLocalIndex{0};

  /**
   * indicate number of local variables defined in this scope
   */
  unsigned int localSize{0};

  const std::reference_wrapper<unsigned int> maxVarCount;

  /**
   * maintain (Handle, commitId)
   */
  std::unordered_map<std::string, std::pair<HandlePtr, unsigned int>> handles;

  /**
   * captured variable handles
   */
  std::vector<HandlePtr> captures;

  /**
   * for func/block scope construction
   * only called from enterScope()
   * @param kind
   * @param parent
   * @param varCount
   */
  NameScope(Kind kind, const NameScopePtr &parent, std::reference_wrapper<unsigned int> varCount)
      : kind(kind), modId(parent->modId), parent(parent), maxVarCount(varCount) {}

  static NameScopePtr block(Kind kind, const NameScopePtr &parent,
                            std::reference_wrapper<unsigned int> varCount) {
    return NameScopePtr(new NameScope(kind, parent, varCount));
  }

public:
  /**
   * for module scope construction
   * normally called from ModuleLoader
   * @param gvarCount
   * @param modId
   */
  NameScope(std::reference_wrapper<unsigned int> gvarCount, unsigned int modIndex,
            unsigned short modId)
      : kind(GLOBAL), modId(modId), modIndex(modIndex), maxVarCount(gvarCount) {}

  /**
   * for module scope construction.
   * normally called from ModuleLoader
   * @param parent
   * @param modId
   */
  NameScope(const NameScopePtr &parent, unsigned int modIndex, unsigned short modId)
      : kind(GLOBAL), modId(modId), modIndex(modIndex), parent(parent),
        maxVarCount(parent->maxVarCount) {
    assert(this->parent->isGlobal());
  }

  /**
   * re-create global module scope from already created Mod Type
   * @param pool
   * @param parent
   * only used parent->mexVarCount
   * @param modType
   */
  static NameScopePtr reopen(const TypePool &pool, const NameScope &parent, const ModType &modType);

  /**
   * clone global scope. (must be global scope)
   * @return
   * if not global scope, return null
   */
  NameScopePtr cloneGlobal() const;

  bool isGlobal() const { return this->kind == GLOBAL; }

  bool isFunc() const { return this->kind == FUNC; }

  bool inBuiltinModule() const { return this->modId == 0; }

  bool inRootModule() const { return this->modId == 1; }

  ModAttr getModAttr() const { return this->modAtter; }

  void updateModAttr(ModAttr attr) { setFlag(this->modAtter, attr); }

  unsigned int getCurLocalIndex() const { return this->curLocalIndex; }

  unsigned int getLocalSize() const { return this->localSize; }

  unsigned int getBaseIndex() const { return this->getCurLocalIndex() - this->getLocalSize(); }

  unsigned int getMaxGlobalVarIndex() const {
    assert(this->isGlobal());
    return this->maxVarCount.get();
  }

  unsigned int getMaxLocalVarIndex() const {
    return this->kind == BLOCK ? this->maxVarCount.get() : this->curLocalIndex;
  }

  const auto &getHandles() const { return this->handles; }

  const auto &getCaptures() const { return this->captures; }

  HandlePtr find(const std::string &name) const {
    auto iter = this->handles.find(name);
    if (iter != this->handles.end()) {
      return iter->second.first;
    }
    return nullptr;
  }

  bool contains(const std::string &name) const { return this->find(name) != nullptr; }

  // for scope construction
  /**
   * create new scope
   * @param kind
   * must not be GLOBAL
   * @return
   * if illegal kind, (ex. BLOCK->GLOBAL, GLOBAL->GLOBAL) return null
   */
  NameScopePtr enterScope(Kind kind);

  /**
   *
   * @return
   * return parent
   */
  NameScopePtr exitScope() { return this->parent; }

  void clearLocalSize() {
    assert(this->isGlobal());
    this->curLocalIndex = 0;
    this->localSize = 0;
  }

  // for name registration
  NameRegisterResult defineHandle(std::string &&name, const DSType &type, HandleAttr attr) {
    return this->defineHandle(std::move(name), type, HandleKind::VAR, attr);
  }

  NameRegisterResult defineHandle(std::string &&name, const DSType &type, HandleKind kind,
                                  HandleAttr attr);

  NameRegisterResult defineAlias(std::string &&name, const HandlePtr &handle);

  NameRegisterResult defineTypeAlias(const TypePool &pool, const std::string &name,
                                     const DSType &type);

  NameRegisterResult defineMethod(const TypePool &pool, const DSType &recvType,
                                  const std::string &name, const DSType &returnType,
                                  const std::vector<const DSType *> &paramTypes);

  NameRegisterResult defineConstructor(const TypePool &pool, const RecordType &recvType,
                                       const std::vector<const DSType *> &paramTypes) {
    return this->defineMethod(pool, recvType, OP_INIT, recvType, paramTypes);
  }

  NameRegisterResult defineConst(std::string &&name, ConstEntry entry);

  /**
   * import handle from foreign module (ModType)
   * @param type
   * @param k
   * @return
   * if found name conflict, return conflicted name
   */
  std::string importForeignHandles(const TypePool &pool, const ModType &type, ImportedModKind k);

  const ModType &toModType(TypePool &pool) const;

  // for name lookup
  /**
   * lookup handle
   * @param name
   * @return
   */
  Result<HandlePtr, NameLookupError> lookup(const std::string &name);

  Result<HandlePtr, NameLookupError> lookupField(const TypePool &pool, const DSType &recv,
                                                 const std::string &fieldName) const;

  const MethodHandle *lookupMethod(TypePool &pool, const DSType &recvType,
                                   const std::string &methodName) const;

  const MethodHandle *lookupConstructor(TypePool &pool, const DSType &recvType) const {
    return this->lookupMethod(pool, recvType, OP_INIT);
  }

  template <typename Walker>
  static constexpr bool walker_requirement_v =
      std::is_same_v<bool, std::invoke_result_t<Walker, StringRef, const Handle &>>;

  /**
   *
   * @tparam Walker
   * bool(StringRef, const Handle&)
   * @param walker
   */
  template <typename Walker, enable_when<walker_requirement_v<Walker>> = nullptr>
  void walk(Walker walker) const {
    for (const auto *cur = this; cur != nullptr; cur = cur->parent.get()) {
      for (auto &e : cur->getHandles()) {
        if (!walker(e.first, *e.second.first)) {
          return;
        }
      }
    }
  }

  // for symbol discard
  ScopeDiscardPoint getDiscardPoint() const {
    return ScopeDiscardPoint{
        .commitIdOffset = static_cast<unsigned int>(this->handles.size()),
    };
  }

  void discard(ScopeDiscardPoint discardPoint); // FIXME: discard more var index

private:
  NameScopePtr fromThis() { return NameScopePtr(this); }

  Handle *findMut(const std::string &name) {
    auto iter = this->handles.find(name);
    if (iter != this->handles.end()) {
      return iter->second.first.get();
    }
    return nullptr;
  }

  /**
   * just add newly created handle.
   * only called from addNew* api
   * @param name
   * @param handle
   * @param asAlias
   * if true, not increment internal variable index
   * @return
   */
  NameRegisterResult add(std::string &&name, HandlePtr &&handle, NameRegisterOp op = {});

  /**
   * define local/global variable name
   * @param name
   * @param type
   * @param attr
   * @return
   */
  NameRegisterResult addNewHandle(std::string &&name, const DSType &type, HandleKind k,
                                  HandleAttr attr) {
    if (this->isGlobal()) {
      setFlag(attr, HandleAttr::GLOBAL);
    } else {
      unsetFlag(attr, HandleAttr::GLOBAL);
    }
    unsigned int index = this->isGlobal() ? this->getMaxGlobalVarIndex() : this->getCurLocalIndex();
    return this->add(std::move(name), HandlePtr::create(type, index, k, attr, this->modId));
  }

  NameRegisterResult addNewAliasHandle(std::string &&name, const HandlePtr &handle) {
    return this->add(std::move(name), HandlePtr(handle), NameRegisterOp::AS_ALIAS);
  }

  NameRegisterResult addNewForeignHandle(std::string &&name, const HandlePtr &handle) {
    return this->add(std::move(name), HandlePtr(handle),
                     NameRegisterOp::AS_ALIAS | NameRegisterOp::IGNORE_CONFLICT);
  }
};

// for module loading

class ModLoadingError {
private:
  int value;

public:
  enum Kind : int {
    CIRCULAR_LOAD = 0,
    MOD_LIMIT = -1,
    VAR_LIMIT = -2,
  };

  explicit ModLoadingError(int value) : value(value) {}

  explicit ModLoadingError(Kind k) : ModLoadingError(static_cast<int>(k)) {}

  int getErrNo() const { return this->value; }

  bool isFileNotFound() const { return this->getErrNo() == ENOENT; }

  bool isCircularLoad() const { return this->getErrNo() == static_cast<int>(CIRCULAR_LOAD); }

  bool isModLimit() const { return this->getErrNo() == static_cast<int>(MOD_LIMIT); }

  bool isVarLimit() const { return this->getErrNo() == static_cast<int>(VAR_LIMIT); }
};

using ModResult = Union<const char *, unsigned int, ModLoadingError>;

enum class ModLoadOption {
  IGNORE_NON_REG_FILE = 1u << 0u,
};

template <>
struct allow_enum_bitop<ModLoadOption> : std::true_type {};

class ModuleLoaderBase {
protected:
  const SysConfig &sysConfig;

  explicit ModuleLoaderBase(const SysConfig &conf) : sysConfig(conf) {}

public:
  static constexpr unsigned int MAX_MOD_NUM = SYS_LIMIT_MOD_ID;

  virtual ~ModuleLoaderBase() = default;

  const SysConfig &getSysConfig() const { return this->sysConfig; }

  /**
   * resolve module path or module type
   * @param scriptDir
   * may be null
   * @param modPath
   * not null
   * @param filePtr
   * write resolved file pointer
   * @return
   */
  ModResult loadImpl(const char *scriptDir, const char *modPath, FilePtr &filePtr,
                     ModLoadOption option);

  /**
   * search module from scriptDir => LOCAL_MOD_DIR => SYSTEM_MOD_DIR
   * @param scriptDir
   * may be null. if not full path, not search next module path
   * @param path
   * if full path, not search next module path
   * @param filePtr
   * if module loading failed, will be null
   * @param option
   * @return
   */
  ModResult load(const char *scriptDir, const char *path, FilePtr &filePtr, ModLoadOption option);

private:
  virtual ModResult addNewModEntry(CStrPtr &&ptr) = 0;
};

class ModEntry {
private:
  unsigned int typeId;

public:
  static ModEntry create() {
    ModEntry e; // NOLINT
    e.typeId = 0;
    return e;
  }

  void setModType(const ModType &type) { this->typeId = type.typeId(); }

  /**
   * get type id of module type
   * @return
   * if not set mod type (not sealed), return 0.
   */
  unsigned int getTypeId() const { return this->typeId; }

  /**
   *
   * @return
   * return true if already sealed (set module type id)
   */
  bool isSealed() const { return this->getTypeId() > 0; }
};

struct ModDiscardPoint {
  unsigned int idCount;
  unsigned int gvarCount;
};

class ModuleLoader : public ModuleLoaderBase {
private:
  static_assert(sizeof(ModEntry) == sizeof(uint32_t));

  StrRefMap<unsigned int> indexMap;
  std::vector<std::pair<CStrPtr, ModEntry>> entries;

  unsigned int gvarCount{0};

public:
  NON_COPYABLE(ModuleLoader);

  explicit ModuleLoader(const SysConfig &config) : ModuleLoaderBase(config) {}

  ~ModuleLoader() override = default;

  ModDiscardPoint getDiscardPoint() const {
    return {
        .idCount = this->modSize(),
        .gvarCount = this->gvarCount,
    };
  }

  void discard(ModDiscardPoint discardPoint);

  NameScopePtr createGlobalScope(const TypePool &pool, const char *name,
                                 const ModType *modType = nullptr);

  NameScopePtr createGlobalScopeFromFullPath(const TypePool &pool, StringRef fullPath,
                                             const ModType &modType);

  const ModType &createModType(TypePool &pool, const NameScope &scope);

  unsigned int getGvarCount() const { return this->gvarCount; }

  unsigned int modSize() const { return this->indexMap.size(); }

  const ModEntry *find(StringRef key) const {
    auto iter = this->indexMap.find(key);
    if (iter == this->indexMap.end()) {
      return nullptr;
    }
    return &this->entries[iter->second].second;
  }

  const auto &operator[](unsigned int modId) const { return this->entries[modId]; }

  auto begin() const { return this->entries.begin(); }

  auto end() const { return this->entries.end(); }

private:
  ModResult addNewModEntry(CStrPtr &&ptr) override;
};

struct DiscardPoint {
  const ModDiscardPoint mod;
  const ScopeDiscardPoint scope;
  const TypeDiscardPoint type;
};

inline void discardAll(ModuleLoader &loader, NameScope &scope, TypePool &typePool,
                       const DiscardPoint &discardPoint) {
  loader.discard(discardPoint.mod);
  scope.discard(discardPoint.scope);
  typePool.discard(discardPoint.type);
}

} // namespace ydsh

#endif // YDSH_SCOPE_H
