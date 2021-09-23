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
#include "type_pool.h"

namespace ydsh {

// for name lookup

enum class NameLookupError {
  DEFINED,
  LIMIT,
  INVALID_TYPE,
};

using NameLookupResult = Result<const FieldHandle *, NameLookupError>;

struct ScopeDiscardPoint {
  unsigned int commitIdOffset;
};

class NameScope : public RefCount<NameScope> {
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
   * may be null
   */
  const IntrusivePtr<NameScope> parent;

private:
  unsigned int curLocalIndex{0};

  /**
   * indicate number of local variables defined in this scope
   */
  unsigned int localSize{0};

  const std::reference_wrapper<unsigned int> maxVarCount;

  std::unordered_map<std::string, FieldHandle> handles;

public:
  /**
   * for module scope construction
   * normally called from ModuleLoader
   * @param gvarCount
   * @param modId
   */
  explicit NameScope(std::reference_wrapper<unsigned int> gvarCount, unsigned short modId = 0)
      : kind(GLOBAL), modId(modId), maxVarCount(gvarCount) {}

  /**
   * for module scope construction.
   * normally called from ModuleLoader
   * @param parent
   * @param modId
   */
  NameScope(const IntrusivePtr<NameScope> &parent, unsigned short modId)
      : kind(GLOBAL), modId(modId), parent(parent), maxVarCount(parent->maxVarCount) {
    assert(this->parent->isGlobal());
  }

  /**
   * re-create global module scope from already created Mod Type
   * @param parent
   * @param modType
   */
  NameScope(const TypePool &pool, const IntrusivePtr<NameScope> &parent, const ModType &modType);

  /**
   * for func/block scope construction
   * only called from enterScope()
   * @param kind
   * @param parent
   * @param varCount
   */
  NameScope(Kind kind, const IntrusivePtr<NameScope> &parent,
            std::reference_wrapper<unsigned int> varCount)
      : kind(kind), modId(parent->modId), parent(parent), maxVarCount(varCount) {}

  bool isGlobal() const { return this->kind == GLOBAL; }

  bool inBuiltinModule() const { return this->modId == 0; }

  bool inRootModule() const { return this->modId == 1; }

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

  const std::unordered_map<std::string, FieldHandle> &getHandles() const { return this->handles; }

  auto begin() const { return this->handles.begin(); }

  auto end() const { return this->handles.end(); }

  const FieldHandle *find(const std::string &name) const {
    auto iter = this->handles.find(name);
    if (iter != this->handles.end()) {
      return &iter->second;
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
  IntrusivePtr<NameScope> enterScope(Kind kind);

  /**
   *
   * @return
   * return parent
   */
  IntrusivePtr<NameScope> exitScope() { return this->parent; }

  void clearLocalSize() {
    assert(this->isGlobal());
    this->curLocalIndex = 0;
    this->localSize = 0;
  }

  // for name registration
  NameLookupResult defineHandle(std::string &&name, const DSType &type, FieldAttribute attr);

  NameLookupResult defineAlias(std::string &&name, const FieldHandle &handle);

  NameLookupResult defineTypeAlias(const TypePool &pool, std::string &&name, const DSType &type);

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
  const FieldHandle *lookup(const std::string &name) const;

  const FieldHandle *lookupField(const TypePool &pool, const DSType &recv,
                                 const std::string &fieldName) const {
    auto *handle = recv.lookupField(pool, fieldName);
    if (handle) {
      if (handle->getModID() == 0 || this->modId == handle->getModID() || fieldName[0] != '_') {
        return handle;
      }
    }
    return nullptr;
  }

  // for symbol discard
  ScopeDiscardPoint getDiscardPoint() const {
    return ScopeDiscardPoint{
        .commitIdOffset = static_cast<unsigned int>(this->handles.size()),
    };
  }

  void discard(ScopeDiscardPoint discardPoint); // FIXME: discard more var index

private:
  unsigned int commitId() const { return this->handles.size(); }

  IntrusivePtr<NameScope> fromThis() { return IntrusivePtr<NameScope>(this); }

  FieldHandle *findMut(const std::string &name) {
    auto iter = this->handles.find(name);
    if (iter != this->handles.end()) {
      return &iter->second;
    }
    return nullptr;
  }

  /**
   * just add newly created handle.
   * only called from addNew* api
   * @param name
   * @param handle
   * @return
   */
  NameLookupResult add(std::string &&name, FieldHandle &&handle);

  /**
   * define local/global variable name
   * @param name
   * @param type
   * @param attr
   * @return
   */
  NameLookupResult addNewHandle(std::string &&name, const DSType &type, FieldAttribute attr) {
    if (this->isGlobal()) {
      setFlag(attr, FieldAttribute::GLOBAL);
    } else {
      unsetFlag(attr, FieldAttribute::GLOBAL);
    }
    unsigned int index = this->isGlobal() ? this->getMaxGlobalVarIndex() : this->getCurLocalIndex();
    return this->add(std::move(name),
                     FieldHandle::create(this->commitId(), type, index, attr, this->modId));
  }

  /**
   * for alias definition
   * @param name
   * @param handle
   * @return
   */
  NameLookupResult addNewAlias(std::string &&name, const FieldHandle &handle) {
    return this->add(std::move(name), FieldHandle::alias(this->commitId(), handle, this->modId));
  }

  NameLookupResult addNewForeignHandle(std::string &&name, const FieldHandle &handle) {
    return this->add(std::move(name),
                     FieldHandle::alias(this->commitId(), handle, handle.getModID()));
  }
};

// for module loading

class ModLoadingError {
private:
  int value;

public:
  explicit ModLoadingError(int value) : value(value) {}

  int getErrNo() const { return this->value; }

  bool isFileNotFound() const { return this->getErrNo() == ENOENT; }

  bool isCircularLoad() const { return this->getErrNo() == 0; }
};

using ModResult = Union<const char *, unsigned int, ModLoadingError>;

enum class ModLoadOption {
  IGNORE_NON_REG_FILE = 1u << 0u,
};

template <>
struct allow_enum_bitop<ModLoadOption> : std::true_type {};

class ModuleLoaderBase {
public:
  static constexpr unsigned int MAX_MOD_NUM = SYS_LIMIT_MOD_ID;

  virtual ~ModuleLoaderBase() = default;

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
  FlexBuffer<ModEntry> entries;

  unsigned int gvarCount{0};

public:
  NON_COPYABLE(ModuleLoader);

  ModuleLoader() = default;

  ~ModuleLoader() override {
    for (auto &e : this->indexMap) {
      free(const_cast<char *>(e.first.data()));
    }
  }

  ModDiscardPoint getDiscardPoint() const {
    return {
        .idCount = this->modSize(),
        .gvarCount = this->gvarCount,
    };
  }

  void discard(ModDiscardPoint discardPoint);

  IntrusivePtr<NameScope> createGlobalScope(const TypePool &pool, const char *name,
                                            const ModType *modType = nullptr);

  IntrusivePtr<NameScope> createGlobalScopeFromFullpath(const TypePool &pool, StringRef fullpath,
                                                        const ModType &modType);

  const ModType &createModType(TypePool &pool, const NameScope &scope, const std::string &fullpath);

  unsigned int modSize() const { return this->indexMap.size(); }

  const ModEntry *find(StringRef key) const {
    auto iter = this->indexMap.find(key);
    if (iter == this->indexMap.end()) {
      return nullptr;
    }
    return &this->entries[iter->second];
  }

  auto begin() const { return this->indexMap.begin(); }

  auto end() const { return this->indexMap.end(); }

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
