/*
 * Copyright (C) 2015-2020 Nagisa Sekiguchi
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

#ifndef YDSH_SYMBOL_TABLE_H
#define YDSH_SYMBOL_TABLE_H

#include <cassert>
#include <functional>

#include "type_pool.h"
#include "misc/resource.hpp"

namespace ydsh {

enum class SymbolError {
    DEFINED,
    LIMIT,
};

using HandleOrError = Result<const FieldHandle *, SymbolError>;

class ModuleScope;

class Scope : public RefCount<Scope> {
protected:
    friend class ModuleScope;

    enum Kind {
        BLOCK,
        GLOBAL,
    };

    IntrusivePtr<Scope> prev;

    std::unordered_map<std::string, FieldHandle> handleMap;

    const Kind kind;

    Scope(Kind kind, IntrusivePtr<Scope> prev) : prev(prev), kind(kind) {}

public:
    virtual ~Scope() = default;

    NON_COPYABLE(Scope);

    const IntrusivePtr<Scope> &getPrev() const {
        return this->prev;
    }

    const std::unordered_map<std::string, FieldHandle> &getHandleMap() const {
        return this->handleMap;
    }

    unsigned int size() const {
        return this->handleMap.size();
    }

    auto begin() const {
        return this->handleMap.begin();
    }

    auto end() const {
        return this->handleMap.end();
    }

    bool isGlobal() const {
        return this->kind == GLOBAL;
    }

    bool isBlock() const {
        return this->kind == BLOCK;
    }

    const FieldHandle *find(const std::string &symbolName) const {
        auto iter = this->handleMap.find(symbolName);
        if(iter != this->handleMap.end() && iter->second) {
            return &iter->second;
        }
        return nullptr;
    }

    const FieldHandle *lookup(const std::string &symbolName) const;

protected:
    void discard(unsigned int commitID) {
        for(auto iter = this->handleMap.begin(); iter != this->handleMap.end();) {
            if(iter->second && iter->second.getCommitID() >= commitID) {
                iter = this->handleMap.erase(iter);
            } else {
                ++iter;
            }
        }
    }

    template <typename ...Arg>
    HandleOrError add(const std::string &symbolName, Arg&& ...arg) {
        return this->add(symbolName, FieldHandle(this->handleMap.size(), std::forward<Arg>(arg)...));
    }

    virtual HandleOrError addNew(const std::string &symbolName,
                                 const DSType &type, FieldAttribute attribute, unsigned short modID) = 0;

private:
    HandleOrError add(const std::string &symbolName, FieldHandle &&handle);
};

class BlockScope : public Scope {
private:
    friend class ModuleScope;

    unsigned int curVarIndex;
    unsigned int varSize{0};

public:
    NON_COPYABLE(BlockScope);

    BlockScope(IntrusivePtr<Scope> prev, unsigned int curVarIndex = 0) :
            Scope(Scope::BLOCK, prev), curVarIndex(curVarIndex) { }

    ~BlockScope() override = default;

    unsigned int getCurVarIndex() const {
        return this->curVarIndex;
    }

    unsigned int getVarSize() const {
        return this->varSize;
    }

    unsigned int getBaseIndex() const {
        return this->getCurVarIndex() - this->getVarSize();
    }

private:
    HandleOrError addNew(const std::string &symbolName, const DSType &type,
                         FieldAttribute attribute, unsigned short modID) override;
};

class GlobalScope : public Scope {
private:
    friend class ModuleScope;

    std::reference_wrapper<unsigned int> gvarCount;

public:
    NON_COPYABLE(GlobalScope);

    explicit GlobalScope(unsigned int &gvarCount);
    ~GlobalScope() override = default;

private:
    HandleOrError addNew(const std::string &symbolName, const DSType &type,
                         FieldAttribute attribute, unsigned short modID) override;
};

/**
 * indicating loaded mod type id.
 *
 * | 1bit |  31bit  |
 * | flag | type id |
 *
 * if flag is 1, indicate globally imported module
 */
enum class ChildModEntry : unsigned int {};

class ModType : public DSType {
private:
    unsigned short modID;

    unsigned short childSize;

    unsigned int index; // module object index

    ChildModEntry *childs;

    std::unordered_map<std::string, FieldHandle> handleMap;

    friend class ModuleScope;

public:
    ModType(unsigned int id, DSType &superType, unsigned short modID,
            const std::unordered_map<std::string, FieldHandle> &handleMap,
            const FlexBuffer<ChildModEntry> &childs, unsigned int index);

    ~ModType() override;

    unsigned short getModID() const {
        return this->modID;
    }

    unsigned int getChildSize() const {
        return this->childSize;
    }

    ChildModEntry getChildAt(unsigned int i) const {
        assert(i < this->childSize);
        return this->childs[i];
    }

    unsigned int getIndex() const {
        return this->index;
    }

    FieldHandle toHandle() const {
        return FieldHandle(
                0, *this, this->index,
                FieldAttribute::READ_ONLY | FieldAttribute::GLOBAL, this->modID);
    }

    std::string toName() const {
        return this->getNameRef().toString();
    }

    const FieldHandle *lookupField(const std::string &fieldName) const override;

    void walkField(std::function<bool(const std::string&, const FieldHandle&)> &walker) const override;

    static std::string toModName(unsigned short modID);
};

inline ChildModEntry toChildModEntry(const ModType &type, bool global) {
    unsigned int value = type.typeId();
    if(global) {
        value |= static_cast<unsigned int>(1 << 31);
    }
    return static_cast<ChildModEntry>(value);
}

inline bool isGlobal(ChildModEntry e) {
    return static_cast<int>(e) < 0;
}

inline unsigned int toTypeId(ChildModEntry e) {
    return static_cast<unsigned int>(e) & 0x7FFFFFFF;
}

class ModuleScope {
private:
    static_assert(sizeof(ChildModEntry) == 4, "");

    unsigned short modID;

    bool builtin;

    GlobalScope globalScope;

    IntrusivePtr<Scope> scope;

    /**
     * contains max number of local variable index.
     */
    std::vector<unsigned int> maxVarIndexStack;

    FlexBuffer<ChildModEntry> childs;

public:
    NON_COPYABLE(ModuleScope);

    explicit ModuleScope(unsigned int &gvarCount, unsigned short modID = 0) :
            modID(modID), builtin(modID == 0), globalScope(gvarCount) {
        this->maxVarIndexStack.push_back(0);
        this->resetScope();
    }

    ~ModuleScope() = default;

    unsigned short getModID() const {
        return this->modID;
    }

    /**
     * return null, if not found.
     */
    const FieldHandle *lookupHandle(const std::string &symbolName) const {
        return this->scope->lookup(symbolName);
    }

    /**
     * return null, if found duplicated handle.
     */
    HandleOrError newHandle(const std::string &symbolName, const DSType &type, FieldAttribute attribute);

    HandleOrError addAlias(const std::string &symbolName, const FieldHandle &handle);

    void closeBuiltin() {
        this->builtin = false;
    }

    /**
     * create new local scope.
     */
    void enterScope();

    /**
     * delete current local scope.
     */
    void exitScope();

    /**
     * create new function scope.
     */
    void enterFunc();

    /**
     * delete current function scope.
     */
    void exitFunc();

    /**
     * import symbol from module type
     * @param type
     * @param global
     * if true, perform global import
     * @return
     * if detect symbol name conflict, return conflicted symbol name.
     * if has no conflict, return empty string
     */
    std::string import(const ModType &type, bool global);

    /**
     * remove changed state(local scope, global FieldHandle)
     */
    void discard(unsigned int commitIdOffset) {
        this->globalScope.discard(commitIdOffset);
        this->resetScope();
        this->childs.clear();
    }

    void clear();

    /**
     * max number of local variable index.
     */
    unsigned int getMaxVarIndex() const {
        assert(!this->maxVarIndexStack.empty());
        return this->maxVarIndexStack.back();
    }

    bool inGlobalScope() const {
        return this->scope->isGlobal();
    }

    const GlobalScope &global() const {
        return this->globalScope;
    }

    const BlockScope &curScope() const {
        assert(this->scope->isBlock());
        return static_cast<const BlockScope&>(*this->scope);
    }

    const FlexBuffer<ChildModEntry> &getChilds() const {
        return this->childs;
    }

    static std::string toModName(unsigned short modID);

private:
    BlockScope &curScope() {
        assert(this->scope->isBlock());
        return static_cast<BlockScope&>(*this->scope);
    }

    void resetScope() {
        this->scope = IntrusivePtr<Scope>(&this->globalScope);
    }

    bool needGlobalImport(ChildModEntry entry);
};

class ModLoadingError {
private:
    int value;

public:
    explicit ModLoadingError(int value) : value(value) {}

    int getErrNo() const {
        return this->value;
    }

    bool isFileNotFound() const {
        return this->getErrNo() == ENOENT;
    }

    bool isCircularLoad() const {
        return this->getErrNo() == 0;
    }
};

using ModResult = Union<const char *, unsigned int, ModLoadingError>;

enum class ModLoadOption {
    IGNORE_NON_REG_FILE = 1 << 0,
};

template <> struct allow_enum_bitop<ModLoadOption> : std::true_type {};

class ModEntry {
private:
    unsigned int index;
    unsigned int typeId;

public:
    explicit ModEntry(unsigned int index) : index(index), typeId(0) {}

    void setModType(const ModType &type) {
        this->typeId = type.typeId();
    }

    unsigned int getIndex() const {
        return this->index;
    }

    /**
     *
     * @return
     * if not set mod type, return 0.
     */
    unsigned int getTypeId() const {
        return this->typeId;
    }

    explicit operator bool() const {
        return this->getTypeId() > 0;
    }

    bool isModule() const {
        return this->getTypeId() > 0;
    }
};


class ModuleLoader {
private:
    std::unordered_map<StringRef, ModEntry> indexMap;

    static constexpr unsigned int MAX_MOD_NUM = UINT16_MAX;

public:
    NON_COPYABLE(ModuleLoader);

    ModuleLoader() = default;

    ~ModuleLoader() {
        for(auto &e : this->indexMap) {
            free(const_cast<char*>(e.first.data()));
        }
    }

    unsigned int getDiscardPoint() const {
        return this->modSize();
    }

    void discard(unsigned int discardPoint);

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
    ModResult load(const char *scriptDir, const char *modPath, FilePtr &filePtr, ModLoadOption option);

    unsigned int modSize() const {
        return this->indexMap.size();
    }

    void addModType(const std::string &fullpath, const ModType &type) {
        auto iter = this->indexMap.find(fullpath);
        assert(iter != this->indexMap.end());
        assert(!iter->second);
        iter->second.setModType(type);
    }

    const ModEntry *find(StringRef key) const {
        auto iter = this->indexMap.find(key);
        if(iter == this->indexMap.end()) {
            return nullptr;
        }
        return &iter->second;
    }

    auto begin() const {
        return this->indexMap.begin();
    }

    auto end() const {
        return this->indexMap.end();
    }

private:
    ModResult addNewModEntry(CStrPtr &&ptr) {
        StringRef key(ptr.get());
        auto pair = this->indexMap.emplace(key, ModEntry(this->indexMap.size()));
        if(!pair.second) {  // already registered
            auto &e = pair.first->second;
            if(e) {
                return e.getTypeId();
            }
            return ModLoadingError(0);
        }
        if(this->indexMap.size() == MAX_MOD_NUM) {
            fatal("module id reaches limit(%u)\n", MAX_MOD_NUM);
        }
        return ptr.release();
    }
};

struct SymbolDiscardPoint {
    unsigned int modIdOffset;
    unsigned int gvarOffset;
    unsigned int commitIdOffset;
};

class SymbolTable {
private:
    ModuleLoader modLoader;
    unsigned int gvarCount{0};
    ModuleScope rootModule;
    ModuleScope *curModule;

    unsigned int termHookIndex{0};

public:
    NON_COPYABLE(SymbolTable);

    SymbolTable() : rootModule(this->gvarCount), curModule(&this->rootModule) {}

    ~SymbolTable() = default;

private:
    ModuleScope &cur() {
        return *this->curModule;
    }

    const ModuleScope &cur() const {
        return *this->curModule;
    }

    ModuleScope &root() {
        return this->rootModule;
    }

    const ModuleScope &root() const {
        return this->rootModule;
    }

public:
    // for module scope

    void setModuleScope(ModuleScope &module) {
        this->curModule = &module;
    }

    void resetCurModule() {
        this->curModule = &this->rootModule;
    }

    unsigned int currentModID() const {
        return this->cur().getModID();
    }

    bool isRootModule() const {
        return &this->root() == &this->cur();
    }

    /**
     * search module from scriptDir => LOCAL_MOD_DIR => SYSTEM_MOD_DIR
     * @param scriptDir
     * may be null. if not full path, not search next module path
     * @param modPath
     * if full path, not search next module path
     * @param filePtr
     * if module loading failed, will be null
     * @param option
     * @return
     */
    ModResult tryToLoadModule(const char *scriptDir, const char *modPath,
            FilePtr &filePtr, ModLoadOption option);

    /**
     * create new module scope and assign it to curModule
     * @return
     */
    std::unique_ptr<ModuleScope> createModuleScope() {
        return std::make_unique<ModuleScope>(this->gvarCount, this->modLoader.modSize());
    }

    /**
     * after call it, assign null to curModule
     * @param fullpath
     * @return
     */
    ModType &createModType(TypePool &pool, const std::string &fullpath);

    std::string import(const ModType &type, bool global) {
        return this->cur().import(type, global);
    }

    // for FieldHandle lookup

    /**
     * return null, if not found.
     */
    const FieldHandle *lookupHandle(const std::string &symbolName) const;

    /**
     * return null, if found duplicated handle.
     */
    HandleOrError newHandle(const std::string &symbolName, const DSType &type, FieldAttribute attribute);

    HandleOrError addAlias(const std::string &symbolName, const FieldHandle &handle);

    HandleOrError addTypeAlias(const TypePool &pool, const std::string &alias, const DSType &type);

    void closeBuiltin() {
        this->root().closeBuiltin();
    }

    /**
     *
     * @return
     * offset + 0 EXIT_HOOK
     * offset + 1 ERR_HOOK
     * offset + 2 ASSERT_HOOK
     */
    unsigned int getTermHookIndex();

    const FieldHandle *lookupField(const DSType &recvType, const std::string &fieldName) const;

    /**
     * lookup user-defined command at runtime
     * if type is not null, saerch from module.
     * @param pool
     * @param belongModType
     * may be null
     * @param cmdName
     * not null. may be empty string
     * @return
     * if not found, return null
     */
    const FieldHandle *lookupUdc(const TypePool &pool, const ModType *belongModType, const char *cmdName) const;

    /**
     * create new local scope.
     */
    void enterScope() {
        this->cur().enterScope();
    }

    /**
     * delete current local scope.
     */
    void exitScope() {
        this->cur().exitScope();
    }

    /**
     * create new function scope.
     */
    void enterFunc() {
        this->cur().enterFunc();
    }

    /**
     * delete current function scope.
     */
    void exitFunc() {
        this->cur().exitFunc();
    }

    SymbolDiscardPoint getDiscardPoint() const {
        return SymbolDiscardPoint {
            .modIdOffset = this->modLoader.modSize(),
            .gvarOffset = this->gvarCount,
            .commitIdOffset = this->root().global().size(),
        };
    }

    void discard(const SymbolDiscardPoint discardPoint) {
        this->modLoader.discard(discardPoint.modIdOffset);
        this->gvarCount = discardPoint.gvarOffset;
        this->resetCurModule();
        this->cur().discard(discardPoint.commitIdOffset);
    }

    void clear() {
        this->cur().clear();
    }

    /**
     * max number of local variable index.
     */
    unsigned int getMaxVarIndex() const {
        return this->cur().getMaxVarIndex();
    }

    /**
     * max number of global variable index.
     */
    unsigned int getMaxGVarIndex() const {
        return this->gvarCount;
    }

    const GlobalScope &globalScope() const {
        return this->cur().global();
    }

    const BlockScope &curScope() const {
        return this->cur().curScope();
    }

    const ModuleLoader &getModLoader() const {
        return this->modLoader;
    }
};

struct DiscardPoint {
    SymbolDiscardPoint symbol;
    TypeDiscardPoint type;
};

inline void discardAll(SymbolTable &symbolTable, TypePool &typePool,
                       const DiscardPoint &discardPoint) {
    symbolTable.discard(discardPoint.symbol);
    typePool.discard(discardPoint.type);
}


} // namespace ydsh

#endif //YDSH_SYMBOL_TABLE_H
