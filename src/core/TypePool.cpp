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

#include "TypePool.h"
#include "TypeLookupError.h"
#include "bind.h"
#include "DSType.h"
#include "TypeTemplate.h"
#include "../parser/Parser.h"
#include "../ast/Node.h"
#include "../parser/TypeChecker.h"
#include "RuntimeContext.h"

namespace ydsh {
namespace core {

using namespace ydsh::ast;

// ######################
// ##     TypePool     ##
// ######################

TypePool::TypePool(char **envp) :
        typeMap(16), typeNameMap(), typeCache(),
        anyType(), voidType(), variantType(), valueType(),
        byteType(), int16Type(), uint16Type(),
        int32Type(), uint32Type(), int64Type(), uint64Type(),
        floatType(), boolType(), stringType(),
        errorType(), taskType(), baseFuncType(),
        objectPathType(), unixFDType(), proxyType(),
        dbusType(), busType(), serviceType(), dbusObjectType(),
        arithmeticErrorType(), outOfIndexErrorType(),
        keyNotFoundErrorType(), typeCastErrorType(),
        templateMap(8),
        arrayTemplate(), mapTemplate(), tupleTemplate(),
        stringArrayType(), envp(envp), envSet(), precisionMap() {

    // initialize type
    this->anyType = this->initBuiltinType("Any", true, 0, info_AnyType());
    this->voidType = this->initBuiltinType("Void", false, 0, info_VoidType(), true);
    this->variantType = this->initBuiltinType("Variant", false, this->anyType, info_Dummy());

    /**
     * hidden from script.
     */
    this->valueType = this->initBuiltinType("%Value%", true, this->variantType, info_Dummy());

    this->byteType = this->initBuiltinType("Byte", false, this->valueType, info_ByteType());
    this->int16Type = this->initBuiltinType("Int16", false, this->valueType, info_Int16Type());
    this->uint16Type = this->initBuiltinType("Uint16", false, this->valueType, info_Uint16Type());
    this->int32Type = this->initBuiltinType("Int32", false, this->valueType, info_Int32Type());
    this->uint32Type = this->initBuiltinType("Uint32", false, this->valueType, info_Uint32Type());
    this->int64Type = this->initBuiltinType("Int64", false, this->valueType, info_Int64Type());
    this->uint64Type = this->initBuiltinType("Uint64", false, this->valueType, info_Uint64Type());

    this->floatType = this->initBuiltinType("Float", false, this->valueType, info_FloatType());
    this->boolType = this->initBuiltinType("Boolean", false, this->valueType, info_BooleanType());
    this->stringType = this->initBuiltinType("String", false, this->valueType, info_StringType());

    this->objectPathType = this->initBuiltinType("ObjectPath", false, this->valueType, info_ObjectPathType());
    this->unixFDType = this->initBuiltinType("UnixFD", false, this->uint32Type, info_UnixFDType());
    this->proxyType = this->initBuiltinType("Proxy", false, this->anyType, info_ProxyType());
    this->dbusType = this->initBuiltinType("DBus", false, this->anyType, info_DBusType());
    this->busType = this->initBuiltinType("Bus", false, this->anyType, info_BusType());
    this->serviceType = this->initBuiltinType("Service", false, this->anyType, info_ServiceType());
    this->dbusObjectType = this->initBuiltinType("DBusObject", false, this->proxyType, info_Dummy());

    this->errorType = this->initBuiltinType("Error", true, this->anyType, info_ErrorType());
    this->taskType = this->initBuiltinType("Task", false, this->anyType, info_Dummy());

    // register NativeFuncInfo to TupleType
    TupleType::registerFuncInfo(info_TupleType()->initInfo);

    // register NativeFuncInfo to ErrorType
    ErrorType::registerFuncInfo(info_ErrorType()->initInfo);

    /**
     * hidden from script
     */
    this->baseFuncType = this->initBuiltinType("%BaseFunc%", false, this->anyType, info_Dummy());

    // initialize type template
    std::vector<DSType*> elements;
    elements.push_back(this->anyType);
    this->arrayTemplate = this->initTypeTemplate("Array", std::move(elements), info_ArrayType());

    elements = std::vector<DSType*>();
    elements.push_back(this->valueType);
    elements.push_back(this->anyType);
    this->mapTemplate = this->initTypeTemplate("Map", std::move(elements), info_MapType());

    elements = std::vector<DSType*>();
    this->tupleTemplate = this->initTypeTemplate("Tuple", std::move(elements), 0);   // pseudo template.

    // init string array type(for command argument)
    std::vector<DSType *> types(1);
    types[0] = this->stringType;
    this->stringArrayType = this->createAndGetReifiedTypeIfUndefined(this->arrayTemplate, std::move(types));

    // init some error type
    this->arithmeticErrorType = this->initErrorType("ArithmeticError", this->errorType);
    this->outOfIndexErrorType = this->initErrorType("OutOfIndexError", this->errorType);
    this->keyNotFoundErrorType = this->initErrorType("KeyNotFoundError", this->errorType);
    this->typeCastErrorType = this->initErrorType("TypeCastError", this->errorType);
}

TypePool::~TypePool() {
    for(const std::pair<std::string, DSType *> &pair : this->typeMap) {
        if((long) pair.second > -1) {
            delete pair.second;
        }
    }
    this->typeMap.clear();

    for(const std::pair<std::string, TypeTemplate *> &pair : this->templateMap) {
        delete pair.second;
    }
    this->templateMap.clear();
}

DSType *TypePool::getAnyType() {
    return this->anyType;
}

DSType *TypePool::getVoidType() {
    return this->voidType;
}

DSType *TypePool::getValueType() {
    return this->valueType;
}

DSType *TypePool::getIntType() {
    return this->getInt32Type();
}

DSType *TypePool::getByteType() {
    return this->byteType;
}

DSType *TypePool::getInt16Type() {
    return this->int16Type;
}

DSType *TypePool::getUint16Type() {
    return this->uint16Type;
}

DSType *TypePool::getInt32Type() {
    return this->int32Type;
}

DSType *TypePool::getUint32Type() {
    return this->uint32Type;
}

DSType *TypePool::getInt64Type() {
    return this->int64Type;
}

DSType *TypePool::getUint64Type() {
    return this->uint64Type;
}

DSType *TypePool::getFloatType() {
    return this->floatType;
}

DSType *TypePool::getBooleanType() {
    return this->boolType;
}

DSType *TypePool::getStringType() {
    return this->stringType;
}

DSType *TypePool::getErrorType() {
    return this->errorType;
}

DSType *TypePool::getTaskType() {
    return this->taskType;
}

DSType *TypePool::getBaseFuncType() {
    return this->baseFuncType;
}

DSType *TypePool::getObjectPathType() {
    return this->objectPathType;
}

DSType *TypePool::getUnixFDType() {
    return this->unixFDType;
}

DSType *TypePool::getProxyType() {
    return this->proxyType;
}

DSType *TypePool::getDBusType() {
    return this->dbusType;
}

DSType *TypePool::getBusType() {
    return this->busType;
}

DSType *TypePool::getServiceType() {
    return this->serviceType;
}

DSType *TypePool::getDBusObjectType() {
    return this->dbusObjectType;
}

DSType *TypePool::getVariantType() {
    return this->variantType;
}

DSType *TypePool::getStringArrayType() {
    return this->stringArrayType;
}

DSType *TypePool::getArithmeticErrorType() {
    return this->arithmeticErrorType;
}

DSType *TypePool::getOutOfIndexErrorType() {
    return this->outOfIndexErrorType;
}

DSType *TypePool::getKeyNotFoundErrorType() {
    return this->keyNotFoundErrorType;
}

DSType *TypePool::getTypeCastErrorType() {
    return this->typeCastErrorType;
}

TypeTemplate *TypePool::getArrayTemplate() {
    return this->arrayTemplate;
}

TypeTemplate *TypePool::getMapTemplate() {
    return this->mapTemplate;
}

TypeTemplate *TypePool::getTupleTemplate() {
    return this->tupleTemplate;
}

DSType *TypePool::getType(const std::string &typeName) {
    static const unsigned long mask = ~(1L << 63);
    auto iter = this->typeMap.find(typeName);
    if(iter != this->typeMap.end()) {
        DSType *type = iter->second;
        if((long) type < 0) {   // if tagged pointer, mask tag
            return (DSType *) (mask & (unsigned long) type);
        }
        return type;
    }
    return 0;
}

DSType *TypePool::getTypeAndThrowIfUndefined(const std::string &typeName) {
    DSType *type = this->getType(typeName);
    if(type == 0) {
        E_UndefinedType(typeName);
    }
    return type;
}

TypeTemplate *TypePool::getTypeTemplate(const std::string &typeName) {
    auto iter = this->templateMap.find(typeName);
    if(iter == this->templateMap.end()) {
        E_NotGenericBase(typeName);
    }
    return iter->second;
}

DSType *TypePool::createAndGetReifiedTypeIfUndefined(TypeTemplate *typeTemplate,
                                                     std::vector<DSType *> &&elementTypes) {
    if(this->tupleTemplate->getName() == typeTemplate->getName()) {
        return this->createAndGetTupleTypeIfUndefined(std::move(elementTypes));
    }
    this->checkElementTypes(typeTemplate, elementTypes);

    if(typeTemplate->getElementTypeSize() != elementTypes.size()) {
        E_UnmatchElement(typeTemplate->getName(),
                         std::to_string(typeTemplate->getElementTypeSize()), std::to_string(elementTypes.size()));
    }

    std::string typeName(this->toReifiedTypeName(typeTemplate, elementTypes));
    auto iter = this->typeMap.find(typeName);
    if(iter == this->typeMap.end()) {
        DSType *superType = this->asVariantType(elementTypes) ? this->variantType : this->anyType;
        return this->addType(std::move(typeName),
                             new ReifiedType(typeTemplate->getInfo(), superType, std::move(elementTypes)));
    }
    return iter->second;
}

DSType *TypePool::createAndGetTupleTypeIfUndefined(std::vector<DSType *> &&elementTypes) {
    this->checkElementTypes(elementTypes);

    if(elementTypes.size() == 0) {
        E_TupleElement();
    }

    std::string typeName(this->toTupleTypeName(elementTypes));
    auto iter = this->typeMap.find(typeName);
    if(iter == this->typeMap.end()) {
        DSType *superType = this->asVariantType(elementTypes) ? this->variantType : this->anyType;
        return this->addType(std::move(typeName), new TupleType(superType, std::move(elementTypes)));
    }
    return iter->second;
}

FunctionType *TypePool::createAndGetFuncTypeIfUndefined(DSType *returnType, std::vector<DSType *> &&paramTypes) {
    this->checkElementTypes(paramTypes);

    std::string typeName(toFunctionTypeName(returnType, paramTypes));
    auto iter = this->typeMap.find(typeName);
    if(iter == this->typeMap.end()) {
        FunctionType *funcType =
                new FunctionType(this->getBaseFuncType(), returnType, std::move(paramTypes));
        this->addType(std::move(typeName), funcType);
        return funcType;
    }

    return dynamic_cast<FunctionType *>(iter->second);
}

InterfaceType *TypePool::createAndGetInterfaceTypeIfUndefined(const std::string &interfaceName) {
    auto iter = this->typeMap.find(interfaceName);
    if(iter == this->typeMap.end()) {
        InterfaceType *type = new InterfaceType(this->dbusObjectType);
        this->addType(std::string(interfaceName), type, true);
        return type;
    }

    return dynamic_cast<InterfaceType *>(iter->second);
}

DSType *TypePool::createAndGetErrorTypeIfUndefined(const std::string &errorName, DSType *superType) {
    auto iter = this->typeMap.find(errorName);
    if(iter == this->typeMap.end()) {
        DSType *type = new ErrorType(superType);
        this->addType(std::string(errorName), type, false);
        return type;
    }
    return iter->second;
}

DSType *TypePool::getDBusInterfaceType(const std::string &typeName) {
    auto iter = this->typeMap.find(typeName);
    if(iter == this->typeMap.end()) {
        // load dbus interface
        std::string ifacePath(RuntimeContext::typeDefDir);
        ifacePath += typeName;

        RootNode rootNode(ifacePath.c_str());
        if(!parse(ifacePath.c_str(), rootNode)) {
            E_NoDBusInterface(typeName);
        }

        InterfaceNode *ifaceNode = dynamic_cast<InterfaceNode *>(rootNode.getNodeList().front());
        if(ifaceNode == 0) {
            E_NoDBusInterface(typeName);
        }
        return TypeChecker::resolveInterface(this, ifaceNode);
    }
    return iter->second;
}

void TypePool::setAlias(const std::string &alias, DSType *targetType) {
    static const unsigned long tag = 1L << 63;

    /**
     * use tagged pointer to prevent double free.
     */
    DSType *taggedPtr = (DSType *) (tag | (unsigned long) targetType);
    auto pair = this->typeMap.insert(std::make_pair(alias, taggedPtr));
    if(!pair.second) {
        E_DefinedType(alias);
    }
}

const std::string &TypePool::getTypeName(const DSType &type) {
    return *this->typeNameMap[(unsigned long) &type];
}

std::string TypePool::toReifiedTypeName(TypeTemplate *typeTemplate, const std::vector<DSType *> &elementTypes) {
    return this->toReifiedTypeName(typeTemplate->getName(), elementTypes);
}

std::string TypePool::toReifiedTypeName(const std::string &name, const std::vector<DSType *> &elementTypes) {
    int elementSize = elementTypes.size();
    std::string reifiedTypeName(name);
    reifiedTypeName += "<";
    for(int i = 0; i < elementSize; i++) {
        if(i > 0) {
            reifiedTypeName += ",";
        }
        reifiedTypeName += this->getTypeName(*elementTypes[i]);
    }
    reifiedTypeName += ">";
    return reifiedTypeName;
}


std::string TypePool::toTupleTypeName(const std::vector<DSType *> &elementTypes) {
    return toReifiedTypeName("Tuple", elementTypes);
}

std::string TypePool::toFunctionTypeName(DSType *returnType, const std::vector<DSType *> &paramTypes) {
    int paramSize = paramTypes.size();
    std::string funcTypeName("Func<");
    funcTypeName += this->getTypeName(*returnType);
    for(int i = 0; i < paramSize; i++) {
        if(i == 0) {
            funcTypeName += ",[";
        }
        if(i > 0) {
            funcTypeName += ",";
        }
        funcTypeName += this->getTypeName(*paramTypes[i]);
        if(i == paramSize - 1) {
            funcTypeName += "]";
        }
    }
    funcTypeName += ">";
    return funcTypeName;
}

bool TypePool::hasEnv(const std::string &envName) {
    if(this->envSet.empty()) {
        this->initEnvSet();
    }
    return this->envSet.find(envName) != this->envSet.end();
}

void TypePool::addEnv(const std::string &envName) {
    if(this->envSet.empty()) {
        this->initEnvSet();
    }
    this->envSet.insert(envName);
}

constexpr int TypePool::INT64_PRECISION;
constexpr int TypePool::INT32_PRECISION;
constexpr int TypePool::INT16_PRECISION;
constexpr int TypePool::BYTE_PRECISION;
constexpr int TypePool::INVALID_PRECISION;

int TypePool::getIntPrecision(DSType *type) {
    if(this->precisionMap.empty()) {    // init precision map
        // int64, uint64
        this->precisionMap.insert(std::make_pair((unsigned long) this->getInt64Type(), INT64_PRECISION));
        this->precisionMap.insert(std::make_pair((unsigned long) this->getUint64Type(), INT64_PRECISION));

        // int32, uint32
        this->precisionMap.insert(std::make_pair((unsigned long) this->getInt32Type(), INT32_PRECISION));
        this->precisionMap.insert(std::make_pair((unsigned long) this->getUint32Type(), INT32_PRECISION));

        // int16, uint16
        this->precisionMap.insert(std::make_pair((unsigned long) this->getInt16Type(), INT16_PRECISION));
        this->precisionMap.insert(std::make_pair((unsigned long) this->getUint16Type(), INT16_PRECISION));

        // byte
        this->precisionMap.insert(std::make_pair((unsigned long) this->getByteType(), BYTE_PRECISION));
    }

    auto iter = this->precisionMap.find((unsigned long) type);
    if(iter == this->precisionMap.end()) {
        return INVALID_PRECISION;
    }
    return iter->second;
}

void TypePool::removeCachedType() {
    for(const std::string *typeName : this->typeCache) {
        auto iter = this->typeMap.find(*typeName);
        if(iter != this->typeMap.end()) {
            if((long) iter->second > -1) {
                this->typeNameMap.erase((unsigned long) iter->second);
                delete iter->second;
            }
            this->typeMap.erase(iter);
        }
    }
}

void TypePool::clearTypeCache() {
    this->typeCache.clear();
}

void TypePool::initEnvSet() {
    for(unsigned int i = 0; this->envp[i] != NULL; i++) {
        char *env = this->envp[i];
        std::string str;
        for(unsigned int j = 0; env[j] != '\0'; j++) {
            char ch = env[j];
            if(ch == '=') {
                break;
            }
            str += ch;
        }
        this->envSet.insert(std::move(str));
    }
}

DSType *TypePool::addType(std::string &&typeName, DSType *type, bool cache) {
    auto pair = this->typeMap.insert(std::make_pair(std::move(typeName), type));
    //this->typeNameMap.push_back(&pair.first->first);
    this->typeNameMap.insert(std::make_pair((unsigned long) type, &pair.first->first));
    if(cache) {
        this->typeCache.push_back(&pair.first->first);
    }
    return type;
}

DSType *TypePool::initBuiltinType(const char *typeName, bool extendable,
                                  DSType *superType, native_type_info_t *info, bool isVoid) {
    // create and register type
    return this->addType(
            std::string(typeName), newBuiltinType(extendable, superType, info, isVoid));
}

TypeTemplate *TypePool::initTypeTemplate(const char *typeName,
                                         std::vector<DSType*> &&elementTypes, native_type_info_t *info) {
    return this->templateMap.insert(
            std::make_pair(typeName, new TypeTemplate(std::string(typeName),
                                                      std::move(elementTypes), info))).first->second;
}

DSType *TypePool::initErrorType(const char *typeName, DSType *superType) {
    return this->addType(std::string(typeName), new ErrorType(superType));
}

void TypePool::checkElementTypes(const std::vector<DSType *> &elementTypes) {
    for(DSType *type : elementTypes) {
        if(*type == *this->voidType) {
            E_InvalidElement(this->getTypeName(*type));
        }
    }
}

void TypePool::checkElementTypes(TypeTemplate *t, const std::vector<DSType *> &elementTypes) {
    unsigned int size = elementTypes.size();
    for(unsigned int i = 0; i < size; i++) {
        if(!t->getAcceptableTypes()[i]->isAssignableFrom(elementTypes[i])) {
            E_InvalidElement(this->getTypeName(*elementTypes[i]));
        }
    }
}

bool TypePool::asVariantType(const std::vector<DSType *> &elementTypes) {
    for(DSType *type : elementTypes) {
        if(!this->variantType->isAssignableFrom(type)) {
            return false;
        }
    }
    return true;
}

} // namespace core
} // namespace ydsh
