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
#include "TypeLookupError.hpp"
#include "bind.h"
#include "../parser/Parser.h"
#include "../parser/TypeChecker.h"
#include "RuntimeContext.h"

extern char **environ;

namespace ydsh {
namespace core {

using namespace ydsh::ast;

// #####################
// ##     TypeMap     ##
// #####################

TypeMap::~TypeMap() {
    for(auto pair : this->typeMapImpl) {
        if(!isAlias(pair.second)) {
            delete pair.second;
        }
    }
    this->typeMapImpl.clear();
    this->typeNameMap.clear();
    this->typeCache.clear();
}


DSType *TypeMap::addType(std::string &&typeName, DSType *type) {
    assert(type != nullptr);
    auto pair = this->typeMapImpl.insert(std::make_pair(std::move(typeName), type));
    this->typeNameMap.insert(std::make_pair(asKey(type), &pair.first->first));
    this->typeCache.push_back(&pair.first->first);
    return type;
}

DSType *TypeMap::getType(const std::string &typeName) const {
    static const unsigned long mask = ~(1L << 63);
    auto iter = this->typeMapImpl.find(typeName);
    if(iter != this->typeMapImpl.end()) {
        DSType *type = iter->second;
        if(isAlias(type)) {   // if tagged pointer, mask tag
            return (DSType *) (mask & (unsigned long) type);
        }
        return type;
    }
    return nullptr;
}

const std::string &TypeMap::getTypeName(const DSType &type) const {
    auto iter = this->typeNameMap.find(asKey(&type));
    assert(iter != this->typeNameMap.end());
    return *iter->second;
}

bool TypeMap::setAlias(std::string &&alias, DSType *targetType) {
    static const unsigned long tag = 1L << 63;

    /**
     * use tagged pointer to prevent double free.
     */
    DSType *taggedPtr = (DSType *) (tag | (unsigned long) targetType);
    auto pair = this->typeMapImpl.insert(std::make_pair(std::move(alias), taggedPtr));
    this->typeCache.push_back(&pair.first->first);
    return pair.second;
}

void TypeMap::commit() {
    this->typeCache.clear();
}

void TypeMap::abort() {
    for(const std::string *typeName : this->typeCache) {
        this->removeType(*typeName);
    }
    this->typeCache.clear();
}

bool TypeMap::isAlias(const DSType *type) {
    assert(type != nullptr);
    return ((long) type) < 0;
}

unsigned long TypeMap::asKey(const DSType *type) {
    assert(type != nullptr);
    return (unsigned long) type;
}

void TypeMap::removeType(const std::string &typeName) {
    auto iter = this->typeMapImpl.find(typeName);
    if(iter != this->typeMapImpl.end()) {
        if(!isAlias(iter->second)) {
            this->typeNameMap.erase(asKey(iter->second));
            delete iter->second;
        }
        this->typeMapImpl.erase(iter);
    }
}


// ######################
// ##     TypePool     ##
// ######################

TypePool::TypePool() :
        typeMap(),
        anyType(), voidType(), variantType(), valueType(),
        byteType(), int16Type(), uint16Type(),
        int32Type(), uint32Type(), int64Type(), uint64Type(),
        floatType(), boolType(), stringType(),
        errorType(), taskType(), baseFuncType(), procType(),
        objectPathType(), unixFDType(), proxyType(),
        dbusType(), busType(), serviceType(), dbusObjectType(),
        arithmeticErrorType(), outOfRangeErrorType(),
        keyNotFoundErrorType(), typeCastErrorType(), dbusErrorType(),
        systemErrorType(), internalStatus(), shellExit(), assertFail(),
        templateMap(8),
        arrayTemplate(), mapTemplate(), tupleTemplate(),
        stringArrayType(), precisionMap(), udcSet() {

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
    this->baseFuncType = this->initBuiltinType("Func", false, this->anyType, info_Dummy());

    // pseudo type for command type checking
    this->procType = this->initBuiltinType("%Proc%", false, this->anyType, info_Dummy());

    // register NativeFuncInfo to ErrorType
    ErrorType::registerFuncInfo(info_ErrorType()->initInfo);

    // initialize type template
    std::vector<DSType *> elements;
    elements.push_back(this->anyType);
    this->arrayTemplate = this->initTypeTemplate("Array", std::move(elements), info_ArrayType());

    elements = std::vector<DSType *>();
    elements.push_back(this->valueType);
    elements.push_back(this->anyType);
    this->mapTemplate = this->initTypeTemplate("Map", std::move(elements), info_MapType());

    elements = std::vector<DSType *>();
    this->tupleTemplate = this->initTypeTemplate("Tuple", std::move(elements), info_TupleType());   // pseudo template.

    // init string array type(for command argument)
    std::vector<DSType *> types(1);
    types[0] = this->stringType;
    this->stringArrayType = this->createAndGetReifiedTypeIfUndefined(this->arrayTemplate, std::move(types));

    // init some error type
    this->arithmeticErrorType = this->initErrorType("ArithmeticError", this->errorType);
    this->outOfRangeErrorType = this->initErrorType("OutOfRangeError", this->errorType);
    this->keyNotFoundErrorType = this->initErrorType("KeyNotFoundError", this->errorType);
    this->typeCastErrorType = this->initErrorType("TypeCastError", this->errorType);
    this->dbusErrorType = this->initErrorType("DBusError", this->errorType);
    this->systemErrorType = this->initErrorType("SystemError", this->errorType);

    this->registerDBusErrorTypes();

    // init internal status type
    this->internalStatus = this->initBuiltinType("%internal status%", false, 0, info_Dummy());
    this->shellExit = this->initBuiltinType("Shell Exit", false, this->internalStatus, info_Dummy());
    this->assertFail = this->initBuiltinType("Assertion Error", false, this->internalStatus, info_Dummy());

    // commit generated type
    this->typeMap.commit();
}

TypePool::~TypePool() {
    for(const std::pair<std::string, TypeTemplate *> &pair : this->templateMap) {
        delete pair.second;
    }
    this->templateMap.clear();
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
    DSType *type = this->typeMap.getType(typeName);
    if(type == nullptr) {
        DSType *superType = this->asVariantType(elementTypes) ? this->variantType : this->anyType;
        return this->typeMap.addType(std::move(typeName),
                                     new ReifiedType(typeTemplate->getInfo(), superType, std::move(elementTypes)));
    }
    return type;
}

DSType *TypePool::createAndGetTupleTypeIfUndefined(std::vector<DSType *> &&elementTypes) {
    this->checkElementTypes(elementTypes);

    if(elementTypes.size() == 0) {
        E_TupleElement();
    }

    std::string typeName(this->toTupleTypeName(elementTypes));
    DSType *type = this->typeMap.getType(typeName);
    if(type == nullptr) {
        DSType *superType = this->asVariantType(elementTypes) ? this->variantType : this->anyType;
        return this->typeMap.addType(std::move(typeName),
                                     new TupleType(this->tupleTemplate->getInfo(), superType, std::move(elementTypes)));
    }
    return type;
}

FunctionType *TypePool::createAndGetFuncTypeIfUndefined(DSType *returnType, std::vector<DSType *> &&paramTypes) {
    this->checkElementTypes(paramTypes);

    std::string typeName(toFunctionTypeName(returnType, paramTypes));
    DSType *type = this->typeMap.getType(typeName);
    if(type == nullptr) {
        FunctionType *funcType =
                new FunctionType(this->getBaseFuncType(), returnType, std::move(paramTypes));
        this->typeMap.addType(std::move(typeName), funcType);
        return funcType;
    }
    assert(type->isFuncType());

    return static_cast<FunctionType *>(type);
}

InterfaceType *TypePool::createAndGetInterfaceTypeIfUndefined(const std::string &interfaceName) {
    DSType *type = this->typeMap.getType(interfaceName);
    if(type == nullptr) {
        InterfaceType *ifaceType = new InterfaceType(this->dbusObjectType);
        this->typeMap.addType(std::string(interfaceName), ifaceType);
        return ifaceType;
    }
    assert(type->isInterface());

    return static_cast<InterfaceType *>(type);
}

DSType *TypePool::createAndGetErrorTypeIfUndefined(const std::string &errorName, DSType *superType) {
    DSType *type = this->typeMap.getType(errorName);
    if(type == nullptr) {
        DSType *errorType = new ErrorType(superType);
        this->typeMap.addType(std::string(errorName), errorType);
        return errorType;
    }
    return type;
}

DSType *TypePool::getDBusInterfaceType(const std::string &typeName) {
    DSType *type = this->typeMap.getType(typeName);
    if(type == nullptr) {
        // load dbus interface
        std::string ifacePath(RuntimeContext::getIfaceDir());
        ifacePath += "/";
        ifacePath += typeName;

        RootNode rootNode(ifacePath.c_str());
        if(!parse(ifacePath.c_str(), rootNode)) {
            E_NoDBusInterface(typeName);
        }

        InterfaceNode *ifaceNode = dynamic_cast<InterfaceNode *>(rootNode.getNodeList().front());
        if(ifaceNode == nullptr) {
            E_NoDBusInterface(typeName);
        }
        return TypeChecker::resolveInterface(this, ifaceNode);
    }
    return type;
}

void TypePool::setAlias(const std::string &alias, DSType *targetType) {
    this->setAlias(alias.c_str(), targetType);
}

void TypePool::setAlias(const char *alias, DSType *targetType) {
    if(!this->typeMap.setAlias(std::string(alias), targetType)) {
        E_DefinedType(alias);
    }
}

const std::string &TypePool::getTypeName(const DSType &type) const {
    return this->typeMap.getTypeName(type);
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

bool TypePool::addUserDefnedCommandName(const std::string &cmdName) {
    return this->udcSet.insert(cmdName).second;
}

void TypePool::commit() {
    this->typeMap.commit();
}

void TypePool::abort() {
    this->typeMap.abort();
}

DSType *TypePool::initBuiltinType(const char *typeName, bool extendable,
                                  DSType *superType, native_type_info_t *info, bool isVoid) {
    // create and register type
    return this->typeMap.addType(
            std::string(typeName), new BuiltinType(extendable, superType, info, isVoid));
}

TypeTemplate *TypePool::initTypeTemplate(const char *typeName,
                                         std::vector<DSType *> &&elementTypes, native_type_info_t *info) {
    return this->templateMap.insert(
            std::make_pair(typeName, new TypeTemplate(std::string(typeName),
                                                      std::move(elementTypes), info))).first->second;
}

DSType *TypePool::initErrorType(const char *typeName, DSType *superType) {
    return this->typeMap.addType(std::string(typeName), new ErrorType(superType));
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
        if(!t->getAcceptableTypes()[i]->isSameOrBaseTypeOf(elementTypes[i])) {
            E_InvalidElement(this->getTypeName(*elementTypes[i]));
        }
    }
}

bool TypePool::asVariantType(const std::vector<DSType *> &elementTypes) {
    for(DSType *type : elementTypes) {
        if(!this->variantType->isSameOrBaseTypeOf(type)) {
            return false;
        }
    }
    return true;
}

void TypePool::registerDBusErrorTypes() {
#define EACH_DBUS_ERROR(OP) \
    OP("Failed") \
    OP("NoMemory") \
    OP("ServiceUnknown") \
    OP("NameHasNoOwner") \
    OP("NoReply") \
    OP("IOError") \
    OP("BadAddress") \
    OP("NotSupported") \
    OP("LimitsExceeded") \
    OP("AccessDenied") \
    OP("AuthFailed") \
    OP("NoServer") \
    OP("Timeout") \
    OP("NoNetwork") \
    OP("AddressInUse") \
    OP("Disconnected") \
    OP("InvalidArgs") \
    OP("FileNotFound") \
    OP("FileExists") \
    OP("UnknownMethod") \
    OP("UnknownObject") \
    OP("UnknownInterface") \
    OP("UnknownProperty") \
    OP("PropertyReadOnly") \
    OP("TimedOut") \
    OP("MatchRuleNotFound") \
    OP("MatchRuleInvalid") \
    OP("Spawn.ExecFailed") \
    OP("Spawn.ForkFailed") \
    OP("Spawn.ChildExited") \
    OP("Spawn.ChildSignaled") \
    OP("Spawn.Failed") \
    OP("Spawn.FailedToSetup") \
    OP("Spawn.ConfigInvalid") \
    OP("Spawn.ServiceNotValid") \
    OP("Spawn.ServiceNotFound") \
    OP("Spawn.PermissionsInvalid") \
    OP("Spawn.FileInvalid") \
    OP("Spawn.NoMemory") \
    OP("UnixProcessIdUnknown") \
    OP("InvalidSignature") \
    OP("InvalidFileContent") \
    OP("SELinuxSecurityContextUnknown") \
    OP("AdtAuditDataUnknown") \
    OP("ObjectPathInUse") \
    OP("InconsistentMessage") \
    OP("InteractiveAuthorizationRequired")

#define ADD_ERROR(E) this->setAlias(E, this->initErrorType("org.freedesktop.DBus.Error." E, this->dbusErrorType));

    EACH_DBUS_ERROR(ADD_ERROR)

#undef ADD_ERROR
#undef EACH_DBUS_ERROR
}

} // namespace core
} // namespace ydsh
