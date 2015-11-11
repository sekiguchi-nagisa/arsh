/*
 * Copyright (C) 2015 Nagisa Sekiguchi
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

#ifndef YDSH_TYPEPOOL_H
#define YDSH_TYPEPOOL_H

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
    bool setAlias(std::string &&alias, DSType *targetType);

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
private:
    TypeMap typeMap;

    // type definition
    DSType *anyType;
    DSType *voidType;

    /**
     * for base type of all of D-Bus related type.
     */
    DSType *variantType;

    /**
     * super type of value type(int, float, bool, string)
     * not directly used it.
     */
    DSType *valueType;

    /**
     * unsigned int 8
     */
    DSType *byteType;

    DSType *int16Type;
    DSType *uint16Type;
    DSType *int32Type;
    DSType *uint32Type;
    DSType *int64Type;
    DSType *uint64Type;
    DSType *floatType;
    DSType *boolType;
    DSType *stringType;
    DSType *errorType;
    DSType *taskType;
    DSType *baseFuncType;
    DSType *procType;
    DSType *stringIterType;

    /**
     * for D-Bus object path
     */
    DSType *objectPathType;

    /**
     * for Unix file descriptor
     */
    DSType *unixFDType;

    DSType *proxyType;

    /**
     * for owner type of each bus object
     */
    DSType *dbusType;

    /**
     * for message bus.
     */
    DSType *busType;

    /**
     * for service
     */
    DSType *serviceType;

    /**
     * for D-Bus proxy instance
     */
    DSType *dbusObjectType;

    /**
     * for exception
     */
    DSType *arithmeticErrorType;
    DSType *outOfRangeErrorType;
    DSType *keyNotFoundErrorType;
    DSType *typeCastErrorType;
    DSType *dbusErrorType;

    // for errno
    DSType *systemErrorType;

    /**
     * for internal status reporting.
     * they are pseudo type, so must not use it from shell
     */
    DSType *internalStatus; // base type
    DSType *shellExit;
    DSType *assertFail;

    /**
     * for type template
     */
    std::unordered_map<std::string, TypeTemplate *> templateMap;

    // type template definition
    TypeTemplate *arrayTemplate;
    TypeTemplate *mapTemplate;
    TypeTemplate *tupleTemplate;

    /*
     * for command argument
     */
    DSType *stringArrayType;

    /**
     * for integer widening
     */
    std::unordered_map<unsigned long, int> precisionMap;

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
    DSType *getAnyType() const {
        return this->anyType;
    }

    /**
     * get void type (pseudo class representing for void)
     */
    DSType *getVoidType() const {
        return this->voidType;
    }

    DSType *getVariantType() const {
        return this->variantType;
    }

    DSType *getValueType() const {
        return this->valueType;
    }

    /**
     * int is 32bit.
     */
    DSType *getIntType() const {
        return this->getInt32Type();
    }

    DSType *getByteType() const {
        return this->byteType;
    }

    DSType *getInt16Type() const {
        return this->int16Type;
    }

    DSType *getUint16Type() const {
        return this->uint16Type;
    }

    DSType *getInt32Type() const {
        return this->int32Type;
    }

    DSType *getUint32Type() const {
        return this->uint32Type;
    }

    DSType *getInt64Type() const {
        return this->int64Type;
    }

    DSType *getUint64Type() const {
        return this->uint64Type;
    }

    /**
     * float is 64bit.
     */
    DSType *getFloatType() const {
        return this->floatType;
    }

    DSType *getBooleanType() const {
        return this->boolType;
    }

    DSType *getStringType() const {
        return this->stringType;
    }

    DSType *getErrorType() const {
        return this->errorType;
    }

    DSType *getTaskType() const {
        return this->taskType;
    }

    DSType *getBaseFuncType() const {
        return this->baseFuncType;
    }

    DSType *getProcType() const {
        return this->procType;
    }

    DSType *getStringIterType() const {
        return this->stringIterType;
    }

    DSType *getObjectPathType() const {
        return this->objectPathType;
    }

    DSType *getUnixFDType() const {
        return this->unixFDType;
    }

    DSType *getProxyType() const {
        return this->proxyType;
    }

    DSType *getDBusType() const {
        return this->dbusType;
    }

    DSType *getBusType() const {
        return this->busType;
    }

    DSType *getServiceType() const {
        return this->serviceType;
    }

    DSType *getDBusObjectType() const {
        return this->dbusObjectType;
    }

    DSType *getStringArrayType() const {
        return this->stringArrayType;
    }

    // for error
    DSType *getArithmeticErrorType() const {
        return this->arithmeticErrorType;
    }

    DSType *getOutOfRangeErrorType() const {
        return this->outOfRangeErrorType;
    }

    DSType *getKeyNotFoundErrorType() const {
        return this->keyNotFoundErrorType;
    }

    DSType *getTypeCastErrorType() const {
        return this->typeCastErrorType;
    }

    DSType *getDBusErrorType() const {
        return this->dbusErrorType;
    }

    DSType *getSystemErrorType() const {
        return this->systemErrorType;
    }

    // for internal status reporting
    DSType *getInternalStatus() const {
        return this->internalStatus;
    }

    DSType *getShellExit() const {
        return this->shellExit;
    }

    DSType *getAssertFail() const {
        return this->assertFail;
    }

    // for reified type.
    TypeTemplate *getArrayTemplate() const {
        return this->arrayTemplate;
    }

    TypeTemplate *getMapTemplate() const {
        return this->mapTemplate;
    }

    TypeTemplate *getTupleTemplate() const {
        return this->tupleTemplate;
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
    DSType *getTypeAndThrowIfUndefined(const std::string &typeName);

    /**
     * get template type.
     * if template type is not found, throw exception
     */
    TypeTemplate *getTypeTemplate(const std::string &typeName);

    /**
     * if type template is Tuple, call createAndGetTupleTypeIfUndefined()
     */
    DSType *createAndGetReifiedTypeIfUndefined(TypeTemplate *typeTemplate, std::vector<DSType *> &&elementTypes);

    DSType *createAndGetTupleTypeIfUndefined(std::vector<DSType *> &&elementTypes);

    FunctionType *createAndGetFuncTypeIfUndefined(DSType *returnType, std::vector<DSType *> &&paramTypes);

    InterfaceType *createAndGetInterfaceTypeIfUndefined(const std::string &interfaceName);

    DSType *createAndGetErrorTypeIfUndefined(const std::string &errorName, DSType *superType);

    /**
     * if not found type, search directory /etc/ydsh/dbus/
     */
    DSType *getDBusInterfaceType(const std::string &typeName);

    /**
     * set type name alias. if alias name has already defined, report error.
     */
    void setAlias(const std::string &alias, DSType *targetType);

    void setAlias(const char *alias, DSType *targetType);

    const std::string &getTypeName(const DSType &type) const;

    /**
     * create reified type name
     * equivalent to toReifiedTypeName(typeTemplate->getName(), elementTypes)
     */
    std::string toReifiedTypeName(TypeTemplate *typeTemplate, const std::vector<DSType *> &elementTypes);

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
    int getIntPrecision(DSType *type);

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
    DSType *initBuiltinType(const char *typeName, bool extendable,
                            DSType *superType, native_type_info_t info, bool isVoid = false);

    TypeTemplate *initTypeTemplate(const char *typeName,
                                   std::vector<DSType*> &&elementTypes, native_type_info_t info);

    DSType *initErrorType(const char *typeName, DSType *superType);

    void checkElementTypes(const std::vector<DSType *> &elementTypes);
    void checkElementTypes(TypeTemplate *t, const std::vector<DSType *> &elementTypes);
    bool asVariantType(const std::vector<DSType *> &elementTypes);

    /**
     * add standard dbus error type and alias. must call only once.
     */
    void registerDBusErrorTypes();
};

} // namespace core
} // namespace ydsh

#endif //YDSH_TYPEPOOL_H
