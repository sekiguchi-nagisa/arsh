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

#include "type.h"
#include "handle.h"
#include "constant.h"

namespace ydsh {

class Scope {
private:
    unsigned int curVarIndex;
    unsigned int shadowCount;
    std::unordered_map<std::string, FieldHandle *> handleMap;

public:
    NON_COPYABLE(Scope);

    /**
     * equivalent to Scope(0)
     */
    Scope() : Scope(0) { }

    explicit Scope(unsigned int curVarIndex) :
            curVarIndex(curVarIndex), shadowCount(0) { }

    ~Scope() {
        for(auto &pair : this->handleMap) {
            delete pair.second;
        }
    }

    /**
     * return null, if not exist.
     */
    FieldHandle *lookupHandle(const std::string &symbolName) const;

    /**
     * add FieldHandle. if adding success, increment curVarIndex.
     * return false if found duplicated handle.
     */
    bool addFieldHandle(const std::string &symbolName, FieldHandle *handle);

    unsigned int getCurVarIndex() const {
        return this->curVarIndex;
    }

    unsigned int getVarSize() const {
        return this->handleMap.size() - this->shadowCount;
    }

    unsigned int getBaseIndex() const {
        return this->getCurVarIndex() - this->getVarSize();
    }

    /**
     * remove handle from handleMap, and delete it.
     */
    void deleteHandle(const std::string &symbolName);

    using const_iterator = std::unordered_map<std::string, FieldHandle *>::const_iterator;

    const_iterator begin() const {
        return this->handleMap.begin();
    }

    const_iterator end() const {
        return this->handleMap.end();
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

enum class SymbolError {
    DUMMY,
    DEFINED,
    LIMIT,
};

class SymbolTable {
public:
    enum DS_TYPE : unsigned int {
        Root__, // pseudo top type of all throwable type(except for option types)
        Any,
        Void,
        Nothing,
        Variant,    // for base type of all of D-Bus related type.
        Value__,    // super type of value type(int, float, bool, string). not directly used it.
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
        StringIter__,
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
        UnwrapingError,

        /**
         * for internal status reporting.
         * they are pseudo type, so must not use it from shell
         */
                InternalStatus__,   // base type
        ShellExit__,
        AssertFail__,

        __SIZE_OF_DS_TYPE__,    // for enum size counting
    };

private:
    // for FieldHandle
    std::vector<std::string> handleCache;

    /**
     * first scope is always global scope.
     */
    std::vector<Scope *> scopes;

    /**
     * contains max number of variable index.
     */
    std::vector<unsigned int> maxVarIndexStack;


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
    SymbolError tryToRegister(const std::string &name, FieldHandle *handle);

    void forbitCmdRedefinition(const char *cmdName) {
        assert(this->inGlobalScope());
        std::string name = cmdSymbolPrefix;
        name += cmdName;
        this->scopes.back()->addFieldHandle(name, nullptr);
    }

public:
    // for FieldHandle lookup

    /**
     * return null, if not found.
     */
    FieldHandle *lookupHandle(const std::string &symbolName) const;

    /**
     * return null, if found duplicated handle.
     */
    std::pair<FieldHandle *, SymbolError> registerHandle(const std::string &symbolName, DSType &type, FieldAttributes attribute);

    bool disallowShadowing(const std::string &symbolName) {
        assert(!this->inGlobalScope());
        return this->scopes.back()->addFieldHandle(symbolName, nullptr);
    }

    /**
     * return null, if found duplicated handle.
     */
    std::pair<FieldHandle *, SymbolError> registerFuncHandle(const std::string &funcName, DSType &returnType,
                                       const std::vector<DSType *> &paramTypes);

    /**
     * if already registered, return null.
     * type must be any type
     */
    std::pair<FieldHandle *, SymbolError> registerUdc(const std::string &cmdName, DSType &type) {
        assert(this->inGlobalScope());
        std::string name = cmdSymbolPrefix;
        name += cmdName;
        return this->registerHandle(name, type, FieldAttribute::READ_ONLY);
    }

    /**
     * if not found, return null.
     */
    FieldHandle *lookupUdc(const char *cmdName) const {
        std::string name = cmdSymbolPrefix;
        name += cmdName;
        return this->lookupHandle(name);
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
    void abort(bool sbortType);

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
        return this->scopes.back()->getCurVarIndex();
    }

    bool inGlobalScope() const {
        return this->scopes.size() == 1;
    }

    const Scope &curScope() const {
        return *this->scopes.back();
    }

    static constexpr const char *cmdSymbolPrefix = "%c";


    // for type lookup

    /**
     * get any type (root class of ydsh class)
     */
    DSType &getAnyType() const {
        return *this->typeTable[Any];
    }

    /**
     * get void type (pseudo class representing for void)
     */
    DSType &getVoidType() const {
        return *this->typeTable[Void];
    }

    DSType &getNothingType() const {
        return *this->typeTable[Nothing];
    }

    DSType &getVariantType() const {
        return *this->typeTable[Variant];
    }

    DSType &getValueType() const {
        return *this->typeTable[Value__];
    }

    /**
     * int is 32bit.
     */
    DSType &getIntType() const {
        return this->getInt32Type();
    }

    DSType &getByteType() const {
        return *this->typeTable[Byte];
    }

    DSType &getInt16Type() const {
        return *this->typeTable[Int16];
    }

    DSType &getUint16Type() const {
        return *this->typeTable[Uint16];
    }

    DSType &getInt32Type() const {
        return *this->typeTable[Int32];
    }

    DSType &getUint32Type() const {
        return *this->typeTable[Uint32];
    }

    DSType &getInt64Type() const {
        return *this->typeTable[Int64];
    }

    DSType &getUint64Type() const {
        return *this->typeTable[Uint64];
    }

    /**
     * float is 64bit.
     */
    DSType &getFloatType() const {
        return *this->typeTable[Float];
    }

    DSType &getBooleanType() const {
        return *this->typeTable[Boolean];
    }

    DSType &getStringType() const {
        return *this->typeTable[String];
    }

    DSType &getErrorType() const {
        return *this->typeTable[Error];
    }

    DSType &getJobType() const {
        return *this->typeTable[Job];
    }

    DSType &getBaseFuncType() const {
        return *this->typeTable[Func];
    }

    DSType &getStringIterType() const {
        return *this->typeTable[StringIter__];
    }

    DSType &getRegexType() const {
        return *this->typeTable[Regex];
    }

    DSType &getSignalType() const {
        return *this->typeTable[Signal];
    }

    DSType &getSignalsType() const {
        return *this->typeTable[Signals];
    }

    DSType &getObjectPathType() const {
        return *this->typeTable[ObjectPath];
    }

    DSType &getUnixFDType() const {
        return *this->typeTable[UnixFD];
    }

    DSType &getProxyType() const {
        return *this->typeTable[Proxy];
    }

    DSType &getDBusType() const {
        return *this->typeTable[DBus];
    }

    DSType &getBusType() const {
        return *this->typeTable[Bus];
    }

    DSType &getServiceType() const {
        return *this->typeTable[Service];
    }

    DSType &getDBusObjectType() const {
        return *this->typeTable[DBusObject];
    }

    DSType &getStringArrayType() const {
        return *this->typeTable[StringArray];
    }

    // for error
    DSType &getArithmeticErrorType() const {
        return *this->typeTable[ArithmeticError];
    }

    DSType &getOutOfRangeErrorType() const {
        return *this->typeTable[OutOfRangeError];
    }

    DSType &getKeyNotFoundErrorType() const {
        return *this->typeTable[KeyNotFoundError];
    }

    DSType &getTypeCastErrorType() const {
        return *this->typeTable[TypeCastError];
    }

    DSType &getDBusErrorType() const {
        return *this->typeTable[DBusError];
    }

    DSType &getSystemErrorType() const {
        return *this->typeTable[SystemError];
    }

    DSType &getStackOverflowErrorType() const {
        return *this->typeTable[StackOverflowError];
    }

    DSType &getRegexSyntaxErrorType() const {
        return *this->typeTable[RegexSyntaxError];
    }

    DSType &getUnwrappingErrorType() const {
        return *this->typeTable[UnwrapingError];
    }

    // for internal status reporting
    DSType &getRoot() const {
        return *this->typeTable[Root__];
    }

    DSType &getInternalStatus() const {
        return *this->typeTable[InternalStatus__];
    }

    DSType &getShellExit() const {
        return *this->typeTable[ShellExit__];
    }

    DSType &getAssertFail() const {
        return *this->typeTable[AssertFail__];
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
    DSType &getTypeAndThrowIfUndefined(const std::string &typeName) const;

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
    void setToTypeTable(DS_TYPE TYPE, DSType *type);

    void initBuiltinType(DS_TYPE TYPE, const char *typeName, bool extendible, native_type_info_t info);

    void initBuiltinType(DS_TYPE TYPE, const char *typeName, bool extendible,
                         DSType &superType, native_type_info_t info);

    TypeTemplate *initTypeTemplate(const char *typeName,
                                   std::vector<DSType*> &&elementTypes, native_type_info_t info);

    void initErrorType(DS_TYPE TYPE, const char *typeName, DSType &superType);

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
