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

namespace ydsh {

class SymbolTable;

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
    DUMMY,
    DEFINED,
    LIMIT,
};

using HandleOrError = std::pair<const FieldHandle *, SymbolError>;

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

class ModuleScope {
private:
    unsigned short modID;

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

    ModuleScope(unsigned int &gvarCount, unsigned short modID = 0) :
            modID(modID), globalScope(gvarCount) {
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
        return this->scopes.back().add(symbolName, FieldHandle()).first != nullptr;
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
        assert(this->maxVarIndexStack.size());
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
    std::unordered_map<std::string, DSType *> typeMapImpl;
    std::unordered_map<unsigned long, const std::string *> typeNameMap;

    /**
     * cache generated type(interface).
     */
    std::vector<const std::string *> typeCache;

public:
    NON_COPYABLE(TypeMap);

    TypeMap() = default;
    ~TypeMap();

    /**
     * return added type. type must not be null.
     */
    DSType *addType(std::string &&typeName, DSType *type);

    /**
     * return null, if has no type.
     */
    DSType *getType(const std::string &typeName) const;

    /**
     * type must not be null.
     */
    const std::string &getTypeName(const DSType &type) const;

    /**
     * return false, if duplicated
     */
    bool setAlias(std::string &&alias, DSType &targetType);

    /**
     * clear typeCache.
     */
    void commit();

    /**
     * remove cached type
     */
    void abort();


private:
    /**
     * remove type and call destructor.
     */
    void removeType(const std::string &typeName);
};

enum class TYPE : unsigned int {
    _Root, // pseudo top type of all throwable type(except for option types)
    Any,
    Void,
    Nothing,
    Variant,    // for base type of all of D-Bus related type.
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
    StringArray,    // for command argument
    Regex,
    Signal,
    Signals,
    Error,
    Job,
    Func,
    StringIter,
    ObjectPath, // for D-Bus object path
    UnixFD,     // for Unix file descriptor
    Proxy,
    DBus,       // for owner type of each bus object
    Bus,        // for message bus.
    Service,    // for service
    DBusObject, // for D-Bus proxy instance
    ArithmeticError,
    OutOfRangeError,
    KeyNotFoundError,
    TypeCastError,
    DBusError,
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

    __SIZE_OF_DS_TYPE__,    // for enum size counting
};

class ModType : public DSType {
private:
    unsigned short modID;
    std::unordered_map<std::string, FieldHandle> handleMap;

public:
    ModType(DSType &superType, unsigned short modID,
            const std::unordered_map<std::string, FieldHandle> &handleMap);

    ~ModType() override = default;

    unsigned short getModID() const {
        return this->modID;
    }

    FieldHandle *lookupFieldHandle(SymbolTable &symbolTable, const std::string &fieldName) override;
    void accept(TypeVisitor *visitor) override;
};

class ModResult {
public:
    enum Kind {
        // module loading error
        UNRESOLVED,
        CIRCULAR,

        PATH,
        TYPE,
    };

private:
    Kind kind;

    union {
        /**
         * resolved module path
         */
        const char *path;

        /**
         * resolved module type
         */
        ModType *type;
    };

private:
    ModResult(Kind kind) : kind(kind), path(nullptr) {}

public:
    explicit ModResult(const char *path) : kind(PATH), path(path) {}

    explicit ModResult(ModType *type) : kind(TYPE), type(type) {}

    static ModResult unresolved() {
        return ModResult(UNRESOLVED);
    }

    static ModResult circular() {
        return ModResult(CIRCULAR);
    }

    /**
     * if false, module loading failed
     * @return
     */
    explicit operator bool() const {
        return this->path != nullptr;
    }

    Kind getKind() const {
        return this->kind;
    }

    const char *asPath() const {
        return this->path;
    }

    ModType &asType() const {
        return *this->type;
    }
};

class ModuleLoader {
private:
    unsigned short oldIDCount{0};
    unsigned short modIDCount{0};
    std::unordered_map<std::string, ModType *> typeMap;

    friend class SymbolTable;

    NON_COPYABLE(ModuleLoader);

    ModuleLoader() = default;

    ~ModuleLoader() {
        for(auto &e : this->typeMap) {
            delete e.second;
        }
    }

    void commit() {
        this->oldIDCount = this->modIDCount;
    }

    void abort() {
        this->modIDCount = this->oldIDCount;//FIXME: remove old mod type
    }

    /**
     *
     * @param modPath
     * @return
     */
    ModResult load(const std::string &modPath);
};


class SymbolTable {
private:
    ModuleLoader modLoader;
    unsigned int oldGvarCount{0};
    unsigned int gvarCount{0};
    ModuleScope rootModule;
    ModuleScope *curModule;


    // for type
    TypeMap typeMap;
    DSType **typeTable; //for builtin type lookup

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

    ~SymbolTable();

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

    ModResult tryToLoadModule(const std::string &modPath) {
        return this->modLoader.load(modPath);
    }

    /**
     * create new moulue scope and assign it to curModule
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

    // for FieldHandle lookup

    /**
     * return null, if not found.
     */
    const FieldHandle *lookupHandle(const std::string &symbolName) const {
        return this->cur().lookupHandle(symbolName);
    }

    /**
     * return null, if found duplicated handle.
     */
    HandleOrError newHandle(const std::string &symbolName, DSType &type, FieldAttributes attribute) {
        return this->cur().newHandle(symbolName, type, attribute);
    }

    bool disallowShadowing(const std::string &symbolName) {
        return this->cur().disallowShadowing(symbolName);
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

    HandleOrError newModHandle(const std::string &name, ModType &type) {
        return this->root().newHandle(name, type, FieldAttribute::READ_ONLY);
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

    void abort(bool abortType) {
        this->gvarCount = this->oldGvarCount;
        if(abortType) {
            this->typeMap.abort();
        }
        this->cur().abort();
        this->modLoader.abort();
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

    DSType &get(TYPE type) const {
        return *this->typeTable[static_cast<unsigned int>(type)];
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

    InterfaceType &createInterfaceType(const std::string &interfaceName);

    DSType &createErrorType(const std::string &errorName, DSType &superType);

    /**
     * if not found type, search directory /etc/ydsh/dbus/
     */
    DSType &getDBusInterfaceType(const std::string &typeName);

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
    void setToTypeTable(TYPE t, DSType *type);

    void initBuiltinType(TYPE t, const char *typeName, bool extendible, native_type_info_t info);

    void initBuiltinType(TYPE t, const char *typeName, bool extendible,
                         DSType &superType, native_type_info_t info);

    TypeTemplate *initTypeTemplate(const char *typeName,
                                   std::vector<DSType*> &&elementTypes, native_type_info_t info);

    void initErrorType(TYPE t, const char *typeName, DSType &superType);

    void checkElementTypes(const std::vector<DSType *> &elementTypes) const;
    void checkElementTypes(const TypeTemplate &t, const std::vector<DSType *> &elementTypes) const;
    bool asVariantType(const std::vector<DSType *> &elementTypes) const;

    /**
     * add standard dbus error type and alias. must call only once.
     */
    void registerDBusErrorTypes();
};

} // namespace ydsh

#endif //YDSH_SYMBOL_TABLE_H
