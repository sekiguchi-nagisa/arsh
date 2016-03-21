/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#ifndef YDSH_CORE_TYPE_POOL_H
#define YDSH_CORE_TYPE_POOL_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "../misc/noncopyable.h"

namespace ydsh {
namespace core {

class DSType;
class FunctionType;
class InterfaceType;
class TypeTemplate;
struct native_type_info_t;

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
    static bool isAlias(const DSType *type);
    static unsigned long asKey(const DSType *type);

    /**
     * remove type and call destructor.
     */
    void removeType(const std::string &typeName);
};

class TypePool {
public:
    enum DS_TYPE : unsigned int {
        Any,
        Void,
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
        Error,
        Task,
        Func,
        Proc__,
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

    /**
     * for integer widening
     */
    std::unordered_map<unsigned long, int> precisionMap;

    /**
     * for number cast op
     */
    std::unordered_map<unsigned long, unsigned char> numTypeIndexMap;

    /**
     * contain user defined command name.
     */
    std::unordered_set<std::string> udcSet;

public:
    NON_COPYABLE(TypePool);

    TypePool();

    ~TypePool();

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

    DSType &getTaskType() const {
        return *this->typeTable[Task];
    }

    DSType &getBaseFuncType() const {
        return *this->typeTable[Func];
    }

    DSType &getProcType() const {
        return *this->typeTable[Proc__];
    }

    DSType &getStringIterType() const {
        return *this->typeTable[StringIter__];
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

    // for internal status reporting
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
    DSType &getTypeAndThrowIfUndefined(const std::string &typeName);

    /**
     * get template type.
     * if template type is not found, throw exception
     */
    const TypeTemplate &getTypeTemplate(const std::string &typeName);

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
    void setAlias(const std::string &alias, DSType &targetType);

    void setAlias(const char *alias, DSType &targetType);

    const std::string &getTypeName(const DSType &type) const;

    /**
     * create reified type name
     * equivalent to toReifiedTypeName(typeTemplate->getName(), elementTypes)
     */
    std::string toReifiedTypeName(const TypeTemplate &typeTemplate, const std::vector<DSType *> &elementTypes);

    std::string toReifiedTypeName(const std::string &name, const std::vector<DSType *> &elementTypes);

    std::string toTupleTypeName(const std::vector<DSType *> &elementTypes);

    /**
     * create function type name
     */
    std::string toFunctionTypeName(DSType *returnType, const std::vector<DSType *> &paramTypes);

    static constexpr int INT64_PRECISION = 50;
    static constexpr int INT32_PRECISION = 40;
    static constexpr int INT16_PRECISION = 30;
    static constexpr int BYTE_PRECISION = 20;
    static constexpr int INVALID_PRECISION = 1;

    /**
     * get integer precision. if type is not int type, return INVALID_PRECISION.
     */
    int getIntPrecision(const DSType &type);

    /**
     * if type is not number type, return -1.
     */
    int getNumTypeIndex(const DSType &type);

    bool addUserDefnedCommandName(const std::string &cmdName);

    /**
     * commit changed state(type)
     */
    void commit();

    /**
     * abort changed state(type)
     */
    void abort();

private:
    void setToTypeTable(DS_TYPE TYPE, DSType *type);

    void initBuiltinType(DS_TYPE TYPE, const char *typeName, bool extendable,
                         native_type_info_t info);

    void initBuiltinType(DS_TYPE TYPE, const char *typeName, bool extendable,
                         DSType &superType, native_type_info_t info);

    TypeTemplate *initTypeTemplate(const char *typeName,
                                   std::vector<DSType*> &&elementTypes, native_type_info_t info);

    void initErrorType(DS_TYPE TYPE, const char *typeName, DSType &superType);

    void checkElementTypes(const std::vector<DSType *> &elementTypes);
    void checkElementTypes(const TypeTemplate &t, const std::vector<DSType *> &elementTypes);
    bool asVariantType(const std::vector<DSType *> &elementTypes);

    /**
     * add standard dbus error type and alias. must call only once.
     */
    void registerDBusErrorTypes();
};

} // namespace core
} // namespace ydsh

#endif //YDSH_CORE_TYPE_POOL_H
