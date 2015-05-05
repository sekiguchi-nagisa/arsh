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

#ifndef CORE_TYPEPOOL_H_
#define CORE_TYPEPOOL_H_

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace ydsh {
namespace core {

class DSType;
class FunctionType;
class InterfaceType;
class TypeTemplate;
struct native_type_info_t;

class TypePool {
private:
    /**
     * contains tagged pointer if set alias.
     */
    std::unordered_map<std::string, DSType *> typeMap;
    std::unordered_map<unsigned long, const std::string *> typeNameMap;

    /**
     * cache generated type(interface).
     */
    std::vector<const std::string *> typeCache;

    // type definition
    DSType *anyType;
    DSType *voidType;

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
     * for dbus bus.
     */
    DSType *busType;

    /**
     * for D-Bus proxy instance
     */
    DSType *dbusObjectType;

    /**
     * for exception
     */
    DSType *arithmeticErrorType;
    DSType *outOfIndexErrorType;
    DSType *keyNotFoundErrorType;
    DSType *typeCastErrorType;

    /**
     * for type template
     */
    std::unordered_map<std::string, TypeTemplate *> templateMap;

    // type template definition
    TypeTemplate *arrayTemplate;
    TypeTemplate *mapTemplate;

    /**
     * pseudo type template for Tuple type
     */
    TypeTemplate *tupleTemplate;

    /*
     * for command argument
     */
    DSType *stringArrayType;

    /**
     * not delete.
     */
    char **envp;

    std::unordered_set<std::string> envSet;

public:
    TypePool(char **envp);

    ~TypePool();

    /**
     * get any type (root class of ydsh class)
     */
    DSType *getAnyType();

    /**
     * get void type (pseudo class representing for void)
     */
    DSType *getVoidType();

    DSType *getValueType();

    /**
     * int is 32bit.
     */
    DSType *getIntType();

    DSType *getByteType();
    DSType *getInt16Type();
    DSType *getUint16Type();
    DSType *getInt32Type();
    DSType *getUint32Type();
    DSType *getInt64Type();
    DSType *getUint64Type();

    /**
     * float is 64bit.
     */
    DSType *getFloatType();

    DSType *getBooleanType();

    DSType *getStringType();

    DSType *getErrorType();

    DSType *getTaskType();

    DSType *getBaseFuncType();

    DSType *getObjectPathType();
    DSType *getUnixFDType();

    DSType *getProxyType();

    DSType *getDBusType();
    DSType *getBusType();
    DSType *getDBusObjectType();

    DSType *getStringArrayType();

    // for error
    DSType *getArithmeticErrorType();
    DSType *getOutOfIndexErrorType();
    DSType *getKeyNotFoundErrorType();
    DSType *getTypeCastErrorType();


    // for reified type.
    TypeTemplate *getArrayTemplate();

    TypeTemplate *getMapTemplate();

    TypeTemplate *getTupleTemplate();

    // for type lookup

    /**
     * return null, if type is not defined.
     */
    DSType *getType(const std::string &typeName);

    /**
     * get type except template type.
     * if type is undefined, throw exception
     */
    DSType *getTypeAndThrowIfUndefined(const std::string &typeName);

    /**
     * get template type.
     * if template type is not found, throw exception
     */
    TypeTemplate *getTypeTemplate(const std::string &typeName, int elementSize);

    /**
     * if type template is Tuple, call createAndGetTupleTypeIfUndefined()
     */
    DSType *createAndGetReifiedTypeIfUndefined(TypeTemplate *typeTemplate, const std::vector<DSType *> &elementTypes);

    DSType *createAndGetTupleTypeIfUndefined(const std::vector<DSType *> &elementTypes);

    FunctionType *createAndGetFuncTypeIfUndefined(DSType *returnType, const std::vector<DSType *> &paramTypes);

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

    const std::string &getTypeName(const DSType &type);

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

    bool hasEnv(const std::string &envName);

    void addEnv(const std::string &envName);

    /**
     * remove cached type from typeMap and call destructor.
     */
    void removeCachedType();

    void clearTypeCache();

private:
    void initEnvSet();

    DSType *addType(std::string &&typeName, DSType *type, bool cache = false);

    DSType *initBuiltinType(const char *typeName, bool extendable,
                            DSType *superType, native_type_info_t *info, bool isVoid = false);

    TypeTemplate *initTypeTemplate(const char *typeName,
                                   std::vector<DSType*> &&elementTypes, native_type_info_t *info);

    DSType *initErrorType(const char *typeName, DSType *superType);

    void checkElementTypes(const std::vector<DSType *> &elementTypes);
    void checkElementTypes(TypeTemplate *t, const std::vector<DSType *> &elementTypes);
};

} // namespace core
} // namespace ydsh

#endif /* CORE_TYPEPOOL_H_ */
