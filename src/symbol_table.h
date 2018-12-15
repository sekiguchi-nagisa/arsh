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

#include "type.h"
#include "handle.h"
#include "constant.h"
#include "tlerror.h"
#include "misc/buffer.hpp"
#include "misc/resource.hpp"
#include "misc/result.hpp"

namespace ydsh {

class Scope {
protected:
    std::unordered_map<std::string, FieldHandle> handleMap;

    ~Scope() = default;

public:
    NON_COPYABLE(Scope);

    Scope() = default;
    Scope(Scope&&) = default;

    using const_iterator = decltype(handleMap)::const_iterator;

    const std::unordered_map<std::string, FieldHandle> &getHandleMap() const {
        return this->handleMap;
    }

    const_iterator begin() const {
        return this->handleMap.begin();
    }

    const_iterator end() const {
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
    HandleOrError addNew(const std::string &symbolName, DSType &type,
                         FieldAttributes attribute, unsigned short modID);

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
    HandleOrError newHandle(const std::string &symbolName, DSType &type, FieldAttributes attribute);

    bool disallowShadowing(const std::string &symbolName) {
        assert(!this->inGlobalScope());
        return static_cast<bool>(this->scopes.back().add(symbolName, FieldHandle()));
    }

    void setBuiltin(bool set) {
        this->builtin = set;
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

class TypeMap {
private:
    unsigned int oldIDCount{0};
    FlexBuffer<DSType *> typeTable;
    std::vector<std::string> nameTable; //   maintain type name
    std::unordered_map<std::string, unsigned int> aliasMap;

public:
    NON_COPYABLE(TypeMap);

    TypeMap() = default;
    ~TypeMap();

    template <typename T, typename ...A>
    T &newType(std::string &&name, A &&...arg) {
        unsigned int id = this->typeTable.size();
        return *static_cast<T *>(this->addType(std::move(name), new T(id, std::forward<A>(arg)...)));
    }

    DSType *get(unsigned int index) const {
        return this->typeTable[index];
    }

    /**
     * return null, if has no type.
     */
    DSType *getType(const std::string &typeName) const;

    /**
     * type must not be null.
     */
    const std::string &getTypeName(const DSType &type) const {
        return this->nameTable[type.getTypeID()];
    }

    /**
     * return false, if duplicated
     */
    bool setAlias(std::string &&alias, unsigned int typeID) {
        auto pair = this->aliasMap.emplace(std::move(alias), typeID);
        return pair.second;
    }

    void commit() {
        this->oldIDCount = this->typeTable.size();
    }

    void abort();

private:
    /**
     * return added type. type must not be null.
     */
    DSType *addType(std::string &&typeName, DSType *type);
};

enum class TYPE : unsigned int {
    _Root, // pseudo top type of all throwable type(except for option types)

    Any,
    Void,
    Nothing,

    _Value,    // super type of value type(int, float, bool, string). not directly used it.

    Byte,       // unsigned int 8
    Int16,
    Uint16,
    Int32,
    Uint32,
    Int64,
    Uint64,
    Float,
    Boolean,
    String,

    Regex,
    Signal,
    Signals,
    Error,
    Job,
    Func,
    StringIter,
    UnixFD,     // for Unix file descriptor
    StringArray,    // for command argument

    ArithmeticError,
    OutOfRangeError,
    KeyNotFoundError,
    TypeCastError,
    SystemError,    // for errno
    StackOverflowError,
    RegexSyntaxError,
    UnwrappingError,

    /**
     * for internal status reporting.
     * they are pseudo type, so must not use it from shell
     */
    _InternalStatus,   // base type
    _ShellExit,
    _AssertFail,
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

    FieldHandle *lookupFieldHandle(SymbolTable &symbolTable, const std::string &fieldName) override;

    static std::string toModName(unsigned short modID);
};

enum class ModLoadingError {
    CIRCULAR,
    NOT_OPEN,
    NOT_FOUND,
};

using ModResult = Union<const char *, ModType *, ModLoadingError>;

class ModuleLoader {
private:
    unsigned short oldIDCount{0};
    unsigned short modIDCount{0};
    std::unordered_map<std::string, ModType *> typeMap;

    friend class SymbolTable;

    NON_COPYABLE(ModuleLoader);

    ModuleLoader() = default;

    ~ModuleLoader() = default;

    void commit() {
        this->oldIDCount = this->modIDCount;
    }

    void abort();

    /**
     * resolve module path or module type
     * @param scriptDir
     * may be null
     * @param modPath
     * must be applied tilde expansion
     * @param filePtr
     * write resolved file pointer
     * @return
     */
    ModResult load(const char *scriptDir, const std::string &modPath, FilePtr &filePtr);
};


class SymbolTable {
private:
    ModuleLoader modLoader;
    unsigned int oldGvarCount{0};
    unsigned int gvarCount{0};
    ModuleScope rootModule;
    ModuleScope *curModule;

    TypeMap typeMap;

    /**
     * for type template
     */
    std::unordered_map<std::string, TypeTemplate *> templateMap;

    // type template definition
    TypeTemplate *arrayTemplate;
    TypeTemplate *mapTemplate;
    TypeTemplate *tupleTemplate;
    TypeTemplate *optionTemplate;

public:
    NON_COPYABLE(SymbolTable);

    SymbolTable();

    ~SymbolTable() {
        delete this->arrayTemplate;
        delete this->mapTemplate;
        delete this->tupleTemplate;
        delete this->optionTemplate;
    }

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

    /**
     * if scriptDir is null, not search module dir
     * @param scriptDir
     * may be null
     * @param modPath
     * @param filePtr
     * if module loading failed, will be null
     * @return
     */
    ModResult tryToLoadModule(const char *scriptDir, const char *modPath, FilePtr &filePtr);

    /**
     * create new module scope and assign it to curModule
     * @return
     */
    std::unique_ptr<ModuleScope> createModuleScope() {
        auto id = ++this->modLoader.modIDCount;
        auto *ptr = new ModuleScope(this->gvarCount, id);
        this->curModule = ptr;
        return std::unique_ptr<ModuleScope>(ptr);
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
    HandleOrError newHandle(const std::string &symbolName, DSType &type, FieldAttributes attribute);

    bool disallowShadowing(const std::string &symbolName) {
        return this->cur().disallowShadowing(symbolName);
    }

    void closeBuiltin() {
        this->root().setBuiltin(false);
    }

    /**
     * if already registered, return null.
     * type must be any type
     */
    HandleOrError registerUdc(const std::string &cmdName, DSType &type) {
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

    HandleOrError newModHandle(ModType &type) {
        return this->root().newHandle(type.toName(), type, FieldAttribute::READ_ONLY);
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
        this->typeMap.commit();
        this->modLoader.commit();
        this->oldGvarCount = this->gvarCount;
    }

    void abort(bool abortType = true) {
        this->modLoader.abort();
        this->gvarCount = this->oldGvarCount;
        if(abortType) {
            this->typeMap.abort();
        }
        this->cur().abort();
        this->resetCurModule();
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

    // for type lookup

    /**
     * unsafe api. normally unused
     * @param index
     * @return
     */
    DSType &get(unsigned int index) const {
        return *this->typeMap.get(index);
    }

    DSType &get(TYPE type) const {
        return this->get(static_cast<unsigned int>(type));
    }

    // for reified type.
    const TypeTemplate &getArrayTemplate() const {
        return *this->arrayTemplate;
    }

    const TypeTemplate &getMapTemplate() const {
        return *this->mapTemplate;
    }

    const TypeTemplate &getTupleTemplate() const {
        return *this->tupleTemplate;
    }

    const TypeTemplate &getOptionTemplate() const {
        return *this->optionTemplate;
    }

    // for type lookup

    /**
     * return null, if type is not defined.
     */
    DSType *getType(const std::string &typeName) const {
        return this->typeMap.getType(typeName);
    }

    /**
     * get type except template type.
     * if type is undefined, throw exception
     */
    DSType &getTypeOrThrow(const std::string &typeName) const;

    /**
     * get template type.
     * if template type is not found, throw exception
     */
    const TypeTemplate &getTypeTemplate(const std::string &typeName) const;

    /**
     * if type template is Tuple, call createTupleType()
     */
    DSType &createReifiedType(const TypeTemplate &typeTemplate, std::vector<DSType *> &&elementTypes);

    DSType &createTupleType(std::vector<DSType *> &&elementTypes);

    FunctionType &createFuncType(DSType *returnType, std::vector<DSType *> &&paramTypes);

    /**
     * set type name alias. if alias name has already defined, report error.
     */
    void setAlias(const std::string &alias, DSType &targetType) {
        this->setAlias(alias.c_str(), targetType);
    }

    void setAlias(const char *alias, DSType &targetType);

    const char *getTypeName(const DSType &type) const {
        return this->typeMap.getTypeName(type).c_str();
    }

    /**
     * create reified type name
     * equivalent to toReifiedTypeName(typeTemplate->getName(), elementTypes)
     */
    std::string toReifiedTypeName(const TypeTemplate &typeTemplate, const std::vector<DSType *> &elementTypes) const {
        return this->toReifiedTypeName(typeTemplate.getName(), elementTypes);
    }

    std::string toReifiedTypeName(const std::string &name, const std::vector<DSType *> &elementTypes) const;

    std::string toTupleTypeName(const std::vector<DSType *> &elementTypes) const {
        return this->toReifiedTypeName(TYPE_TUPLE, elementTypes);
    }

    /**
     * create function type name
     */
    std::string toFunctionTypeName(DSType *returnType, const std::vector<DSType *> &paramTypes) const;

    static constexpr int INT64_PRECISION = 50;
    static constexpr int INT32_PRECISION = 40;
    static constexpr int INT16_PRECISION = 30;
    static constexpr int BYTE_PRECISION = 20;
    static constexpr int INVALID_PRECISION = 1;

    /**
     * get integer precision. if type is not int type, return INVALID_PRECISION.
     */
    int getIntPrecision(const DSType &type) const;

    /**
     * if type is not number type, return -1.
     */
    int getNumTypeIndex(const DSType &type) const;

    /**
     * if not found, return null.
     */
    DSType *getByNumTypeIndex(unsigned int index) const;

private:
    void initBuiltinType(TYPE t, const char *typeName, bool extendible, native_type_info_t info);

    void initBuiltinType(TYPE t, const char *typeName, bool extendible,
                         DSType &superType, native_type_info_t info);

    TypeTemplate *initTypeTemplate(const char *typeName,
                                   std::vector<DSType*> &&elementTypes, native_type_info_t info);

    void initErrorType(TYPE t, const char *typeName);

    void checkElementTypes(const std::vector<DSType *> &elementTypes) const;
    void checkElementTypes(const TypeTemplate &t, const std::vector<DSType *> &elementTypes) const;
};

} // namespace ydsh

#endif //YDSH_SYMBOL_TABLE_H
