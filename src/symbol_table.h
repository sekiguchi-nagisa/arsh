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

class Scope {
private:
    unsigned int curVarIndex;
    unsigned int shadowCount;
    std::unordered_map<std::string, FieldHandle> handleMap;

public:
    NON_COPYABLE(Scope);

    /**
     * equivalent to Scope(0)
     */
    Scope() : Scope(0) { }

    Scope(Scope&&) = default;

    explicit Scope(unsigned int curVarIndex) :
            curVarIndex(curVarIndex), shadowCount(0) { }

    ~Scope() = default;

    /**
     * return null, if not exist.
     */
    const FieldHandle *lookup(const std::string &symbolName) const;

    /**
     * add FieldHandle. if adding success, increment curVarIndex.
     * return null if found duplicated handle.
     */
    const FieldHandle *add(const std::string &symbolName, FieldHandle handle);

    /**
     * remove handle from handleMap, and delete it.
     */
    void remove(const std::string &symbolName) {
        this->handleMap.erase(symbolName);
    }

    unsigned int getCurVarIndex() const {
        return this->curVarIndex;
    }

    unsigned int getVarSize() const {
        return this->handleMap.size() - this->shadowCount;
    }

    unsigned int getBaseIndex() const {
        return this->getCurVarIndex() - this->getVarSize();
    }

    using const_iterator = decltype(handleMap)::const_iterator;

    const_iterator begin() const {
        return this->handleMap.begin();
    }

    const_iterator end() const {
        return this->handleMap.end();
    }
};

enum class SymbolError {
    DUMMY,
    DEFINED,
    LIMIT,
};

using HandleOrError = std::pair<const FieldHandle *, SymbolError>;

class ModuleScope {
private:
    std::vector<std::string> handleCache;

    /**
     * first scope is always global scope.
     */
    std::vector<Scope> scopes;

    /**
     * contains max number of local variable index.
     */
    std::vector<unsigned int> maxVarIndexStack;

public:
    NON_COPYABLE(ModuleScope);

    ModuleScope(bool rootModule = false);

    ModuleScope(ModuleScope&&) = default;

    ~ModuleScope() = default;

    /**
     * return null, if not found.
     */
    const FieldHandle *lookupHandle(const std::string &symbolName) const;

    /**
     * return null, if found duplicated handle.
     */
    HandleOrError registerHandle(const std::string &symbolName, DSType &type, FieldAttributes attribute);

    bool disallowShadowing(const std::string &symbolName) {
        assert(!this->inGlobalScope());
        return this->scopes.back().add(symbolName, FieldHandle()) != nullptr;
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
     * clear entry cache.
     */
    void commit();

    /**
     * remove changed state(local scope, global FieldHandle)
     */
    void abort();

    /**
     * max number of local variable index.
     */
    unsigned int getMaxVarIndex() const {
        return this->maxVarIndexStack.back();
    }

    /**
     * max number of global variable index.
     */
    unsigned int getMaxGVarIndex() const {
        assert(this->inGlobalScope());
        return this->scopes.back().getCurVarIndex();
    }

    bool inGlobalScope() const {
        return this->scopes.size() == 1;
    }

    const Scope &curScope() const {
        return this->scopes.back();
    }

private:
    HandleOrError tryToRegister(const std::string &name, FieldHandle handle);
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


class SymbolTable {
private:
    std::vector<ModuleScope> moduleScopes;


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
        return this->moduleScopes.back();
    }

    const ModuleScope &cur() const {
        return this->moduleScopes.back();
    }

    ModuleScope &root() {
        return this->moduleScopes.front();
    }

    const ModuleScope &root() const {
        return this->moduleScopes.front();
    }

public:
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
    HandleOrError registerHandle(const std::string &symbolName, DSType &type, FieldAttributes attribute) {
        return this->cur().registerHandle(symbolName, type, attribute);
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
        return this->root().registerHandle(name, type, FieldAttribute::READ_ONLY);
    }

    /**
     * if not found, return null.
     */
    const FieldHandle *lookupUdc(const char *cmdName) const {
        std::string name = CMD_SYMBOL_PREFIX;
        name += cmdName;
        return this->root().lookupHandle(name);
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
        this->cur().commit();
    }

    void abort(bool abortType) {
        if(abortType) {
            this->typeMap.abort();
        }
        this->cur().abort();
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
        return this->cur().getMaxGVarIndex();
    }

    const Scope &curScope() const {
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
