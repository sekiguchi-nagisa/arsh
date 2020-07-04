/*
 * Copyright (C) 2015-2018 Nagisa Sekiguchi
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

class Scope {
protected:
    std::unordered_map<std::string, FieldHandle> handleMap;

    ~Scope() = default;

public:
    NON_COPYABLE(Scope);

    Scope() = default;
    Scope(Scope&&) = default;

    const std::unordered_map<std::string, FieldHandle> &getHandleMap() const {
        return this->handleMap;
    }

    auto begin() const {
        return this->handleMap.begin();
    }

    auto end() const {
        return this->handleMap.end();
    }

protected:
    const FieldHandle *lookup(const std::string &symbolName) const {
        auto iter = this->handleMap.find(symbolName);
        if(iter != this->handleMap.end() && iter->second) {
            return &iter->second;
        }
        return nullptr;
    }
};

enum class SymbolError {
    DEFINED,
    LIMIT,
};

using HandleOrError = Result<const FieldHandle *, SymbolError>;

class ModuleScope;

class BlockScope : public Scope {
private:
    unsigned int curVarIndex;
    unsigned int shadowCount;

    friend class ModuleScope;

public:
    NON_COPYABLE(BlockScope);

    BlockScope(BlockScope&&) = default;

    explicit BlockScope(unsigned int curVarIndex = 0) :
            curVarIndex(curVarIndex), shadowCount(0) { }

    ~BlockScope() = default;

    unsigned int getCurVarIndex() const {
        return this->curVarIndex;
    }

    unsigned int getVarSize() const {
        return this->handleMap.size() - this->shadowCount;
    }

    unsigned int getBaseIndex() const {
        return this->getCurVarIndex() - this->getVarSize();
    }

private:
    /**
     * add FieldHandle. if adding success, increment curVarIndex.
     * return null if found duplicated handle.
     */
    HandleOrError add(const std::string &symbolName, FieldHandle handle);
};

class GlobalScope : public Scope {
private:
    std::reference_wrapper<unsigned int> gvarCount;

    friend class ModuleScope;

public:
    NON_COPYABLE(GlobalScope);

    explicit GlobalScope(unsigned int &gvarCount);
    GlobalScope(GlobalScope &&) = default;
    ~GlobalScope() = default;

private:
    HandleOrError addNew(const std::string &symbolName, const DSType &type,
                         FieldAttribute attribute, unsigned short modID);

    /**
     * before call it, reset gvarCount
     */
    void abort() {
        for(auto iter = this->handleMap.begin(); iter != this->handleMap.end();) {
            if(iter->second && iter->second.getIndex() >= this->gvarCount) {
                iter = this->handleMap.erase(iter);
            } else {
                ++iter;
            }
        }
    }
};

class ModType;

class ModuleScope {
private:
    unsigned short modID;

    bool builtin;

    GlobalScope globalScope;

    /**
     * first scope is always global scope.
     */
    std::vector<BlockScope> scopes;

    /**
     * contains max number of local variable index.
     */
    std::vector<unsigned int> maxVarIndexStack;

public:
    NON_COPYABLE(ModuleScope);

    explicit ModuleScope(unsigned int &gvarCount, unsigned short modID = 0) :
            modID(modID), builtin(modID == 0), globalScope(gvarCount) {
        this->maxVarIndexStack.push_back(0);
    }

    ModuleScope(ModuleScope&&) = default;

    ~ModuleScope() = default;

    unsigned short getModID() const {
        return this->modID;
    }

    /**
     * return null, if not found.
     */
    const FieldHandle *lookupHandle(const std::string &symbolName) const;

    /**
     * return null, if found duplicated handle.
     */
    HandleOrError newHandle(const std::string &symbolName, const DSType &type, FieldAttribute attribute);

    bool disallowShadowing(const std::string &symbolName) {
        assert(!this->inGlobalScope());
        return static_cast<bool>(this->scopes.back().add(symbolName, FieldHandle()));
    }

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
     *
     * @param type
     * @return
     * if detect symbol name conflict, return conflicted symbol name.
     * if has no conflict, return null
     */
    const char *import(const ModType &type);


    /**
     * remove changed state(local scope, global FieldHandle)
     */
    void abort() {
        this->globalScope.abort();
        this->scopes.clear();
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
        return this->scopes.empty();
    }

    const GlobalScope &global() const {
        return this->globalScope;
    }

    const BlockScope &curScope() const {
        return this->scopes.back();
    }
};

class SymbolTable;

class ModType : public DSType {
private:
    unsigned short modID;
    std::unordered_map<std::string, FieldHandle> handleMap;

    friend class ModuleScope;

public:
    ModType(unsigned int id, DSType &superType, unsigned short modID,
            const std::unordered_map<std::string, FieldHandle> &handleMap);

    ~ModType() override = default;

    unsigned short getModID() const {
        return this->modID;
    }

    std::string toName() const {
        return toModName(this->modID);
    }

    const FieldHandle *lookupFieldHandle(SymbolTable &symbolTable, const std::string &fieldName) const override;

    static std::string toModName(unsigned short modID);
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
        this->typeId = type.getTypeID();
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
};


class ModuleLoader {
private:
    unsigned int oldModSize{0};

    std::unordered_map<StringRef, ModEntry> indexMap;

public:
    NON_COPYABLE(ModuleLoader);

    ModuleLoader() = default;

    ~ModuleLoader() {
        for(auto &e : this->indexMap) {
            free(const_cast<char*>(e.first.data()));
        }
    }

    void commit() {
        this->oldModSize = this->modSize();
    }

    void abort();

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

    auto begin() const {
        return this->indexMap.begin();
    }

    auto end() const {
        return this->indexMap.end();
    }

private:
    ModResult addModPath(CStrPtr &&ptr) {
        StringRef key(ptr.get());
        auto pair = this->indexMap.emplace(key, ModEntry(this->indexMap.size()));
        if(!pair.second) {  // already registered
            auto &e = pair.first->second;
            if(e) {
                return e.getTypeId();
            }
            return ModLoadingError(0);
        }
        return ptr.release();
    }
};

class SymbolTable {
private:
    ModuleLoader modLoader;
    unsigned int oldGvarCount{0};
    unsigned int gvarCount{0};
    ModuleScope rootModule;
    ModuleScope *curModule;

    unsigned int termHookIndex{0};

    TypePool typePool;

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
    ModuleScope createModuleScope() {
        return ModuleScope(this->gvarCount, this->modLoader.modSize());
    }

    /**
     * after call it, assign null to curModule
     * @param fullpath
     * @return
     */
    ModType &createModType(const std::string &fullpath);

    const char *import(const ModType &type) {
        return this->cur().import(type);
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

    bool disallowShadowing(const std::string &symbolName) {
        return this->cur().disallowShadowing(symbolName);
    }

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

    /**
     * if already registered, return null.
     * type must be any type
     */
    HandleOrError registerUdc(const std::string &cmdName, const DSType &type) {
        assert(this->root().inGlobalScope());
        std::string name = CMD_SYMBOL_PREFIX;
        name += cmdName;
        return this->root().newHandle(name, type, FieldAttribute::READ_ONLY);
    }

    /**
     * if not found, return null.
     */
    const FieldHandle *lookupUdc(const char *cmdName) const {
        std::string name = CMD_SYMBOL_PREFIX;
        name += cmdName;
        return this->root().lookupHandle(name);
    }

    const FieldHandle *lookupModHandle(const ModType &type) const {
        return this->root().lookupHandle(type.toName());
    }

    HandleOrError newModHandle(const ModType &type) {
        return this->root().newHandle(type.toName(), type, FieldAttribute::READ_ONLY);
    }

    const FieldHandle *lookupField(DSType &recvType, const std::string &fieldName);

    const MethodHandle *lookupMethod(const DSType &recvType, const std::string &methodName) {
        return this->typePool.lookupMethod(recvType, methodName);
    }

    const MethodHandle *lookupMethod(unsigned int typeId, const std::string &methodName) {
        return this->lookupMethod(this->get(typeId), methodName);
    }

    const MethodHandle *lookupMethod(TYPE type, const std::string &methodName) {
        return this->lookupMethod(static_cast<unsigned int>(type), methodName);
    }

    const MethodHandle *lookupConstructor(const DSType &recvType) {
        return this->typePool.lookupConstructor(recvType);
    }

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

    void commit() {
        this->typePool.commit();
        this->modLoader.commit();
        this->oldGvarCount = this->gvarCount;
    }

    void abort() {
        this->modLoader.abort();
        this->gvarCount = this->oldGvarCount;
        this->typePool.abort();
        this->resetCurModule();
        this->cur().abort();
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

    // for type lookup
    const TypePool &getTypePool() const {
        return this->typePool;
    }

    TypePool &getTypePool() {
        return this->typePool;
    }

    /**
     * unsafe api. normally unused
     * @param index
     * @return
     */
    DSType &get(unsigned int index) const {
        return *this->typePool.get(index);
    }

    DSType &get(TYPE type) const {
        return this->get(static_cast<unsigned int>(type));
    }

    // for reified type.
    const TypeTemplate &getArrayTemplate() const {
        return this->typePool.getArrayTemplate();
    }

    const TypeTemplate &getMapTemplate() const {
        return this->typePool.getMapTemplate();
    }

    const TypeTemplate &getTupleTemplate() const {
        return this->typePool.getTupleTemplate();
    }

    const TypeTemplate &getOptionTemplate() const {
        return this->typePool.getOptionTemplate();
    }

    /**
     *
     * @param typeName
     * @return
     */
    TypeOrError getType(const std::string &typeName) const {
        return this->typePool.getType(typeName);
    }

    /**
     * get template type.
     * @param typeName
     * @return
     */
    TypeTempOrError getTypeTemplate(const std::string &typeName) const {
        return this->typePool.getTypeTemplate(typeName);
    }

    /**
     * if type template is Tuple, call createTupleType()
     */
    TypeOrError createReifiedType(const TypeTemplate &typeTemplate, std::vector<DSType *> &&elementTypes) {
        return this->typePool.createReifiedType(typeTemplate, std::move(elementTypes));
    }

    TypeOrError createArrayType(DSType &elementType) {
        return this->createReifiedType(this->getArrayTemplate(), {&elementType});
    }

    TypeOrError createMapType(DSType &keyType, DSType &valueType) {
        return this->createReifiedType(this->getMapTemplate(), {&keyType, &valueType});
    }

    TypeOrError createOptionType(DSType &elementType) {
        return this->createReifiedType(this->getOptionTemplate(), {&elementType});
    }

    TypeOrError createTupleType(std::vector<DSType *> &&elementTypes) {
        return this->typePool.createTupleType(std::move(elementTypes));
    }

    /**
     *
     * @param returnType
     * @param paramTypes
     * @return
     * must be FunctionType
     */
    TypeOrError createFuncType(DSType *returnType, std::vector<DSType *> &&paramTypes) {
        return this->typePool.createFuncType(returnType, std::move(paramTypes));
    }

    /**
     * set type name alias. if alias name has alreadt defined, return false
     * @param alias
     * @param targetType
     * @return
     */
    bool setAlias(const std::string &alias, const DSType &targetType) {
        return this->typePool.setAlias(std::string(alias), targetType);
    }

    const char *getTypeName(const DSType &type) const {
        return this->typePool.getTypeName(type).c_str();
    }
};

} // namespace ydsh

#endif //YDSH_SYMBOL_TABLE_H
