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

#include <cassert>
#include <array>

#include "symbol_table.h"
#include "parser.h"
#include "type_checker.h"
#include "core.h"
#include "object.h"
#include "constant.h"

namespace ydsh {

template <std::size_t N>
std::array<NativeCode, N> initNative(const NativeFuncInfo (&e)[N]) {
    std::array<NativeCode, N> array;
    for(unsigned int i = 0; i < N; i++) {
        const char *funcName = e[i].funcName;
        if(funcName != nullptr && strcmp(funcName, "waitSignal") == 0) {
            array[i] = createWaitSignalCode();
        } else {
            array[i] = NativeCode(e[i].func_ptr, static_cast<HandleInfo>(e[i].handleInfo[0]) != HandleInfo::Void);
        }
    }
    return array;
}

} // namespace ydsh

#include "bind.h"

namespace ydsh {

// ###################
// ##     Scope     ##
// ###################

FieldHandle *Scope::lookupHandle(const std::string &symbolName) const {
    auto iter = this->handleMap.find(symbolName);
    return iter != this->handleMap.end() ? iter->second : nullptr;
}

bool Scope::addFieldHandle(const std::string &symbolName, FieldHandle *handle) {
    if(!this->handleMap.insert(std::make_pair(symbolName, handle)).second) {
        return false;
    }
    if(handle == nullptr) {
        this->shadowCount++;
    } else {
        this->curVarIndex++;
    }
    return true;
}

void Scope::deleteHandle(const std::string &symbolName) {
    auto iter = this->handleMap.find(symbolName);
    delete iter->second;
    this->handleMap.erase(symbolName);
}

// #############################
// ##     SymbolTableBase     ##
// #############################

SymbolTableBase::SymbolTableBase() : scopes(1), maxVarIndexStack(1) {
    this->scopes[0] = new Scope();
    this->maxVarIndexStack[0] = 0;
}

SymbolTableBase::~SymbolTableBase() {
    for(Scope *scope : this->scopes) {
        delete scope;
    }
}

SymbolError SymbolTableBase::tryToRegister(const std::string &name, FieldHandle *handle) {
    if(!this->scopes.back()->addFieldHandle(name, handle)) {
        delete handle;
        return SymbolError::DEFINED;
    }
    if(!this->inGlobalScope()) {
        unsigned int varIndex = this->scopes.back()->getCurVarIndex();
        if(varIndex > UINT8_MAX) {
            return SymbolError::LIMIT;
        }
        if(varIndex > this->maxVarIndexStack.back()) {
            this->maxVarIndexStack.back() = varIndex;
        }
    }
    return SymbolError::DUMMY;
}

FieldHandle *SymbolTableBase::lookupHandle(const std::string &symbolName) const {
    for(auto iter = this->scopes.crbegin(); iter != this->scopes.crend(); ++iter) {
        FieldHandle *handle = (*iter)->lookupHandle(symbolName);
        if(handle != nullptr) {
            return handle;
        }
    }
    return nullptr;
}

std::pair<FieldHandle *, SymbolError>
SymbolTableBase::registerHandle(const std::string &symbolName, DSType &type, FieldAttributes attribute) {
    if(this->inGlobalScope()) {
        attribute.set(FieldAttribute::GLOBAL);
    }

    auto *handle = new FieldHandle(&type, this->scopes.back()->getCurVarIndex(), attribute);
    auto e = this->tryToRegister(symbolName, handle);
    if(e != SymbolError::DUMMY) {
        return {nullptr, e};
    }
    if(this->inGlobalScope()) {
        this->handleCache.push_back(symbolName);
    }
    return std::make_pair(handle, SymbolError::DUMMY);
}

std::pair<FieldHandle *, SymbolError>
SymbolTableBase::registerFuncHandle(const std::string &funcName, DSType &returnType,
                                    const std::vector<DSType *> &paramTypes) {
    assert(this->inGlobalScope());
    FieldHandle *handle = new FunctionHandle(&returnType, paramTypes, this->scopes.back()->getCurVarIndex());
    auto e = this->tryToRegister(funcName, handle);
    if(e != SymbolError::DUMMY) {
        return {nullptr, e};
    }
    this->handleCache.push_back(funcName);
    return std::make_pair(handle, SymbolError::DUMMY);
}

void SymbolTableBase::enterScope() {
    unsigned int index = this->scopes.back()->getCurVarIndex();
    if(this->inGlobalScope()) {
        index = 0;
    }
    this->scopes.push_back(new Scope(index));
}

void SymbolTableBase::exitScope() {
    assert(!this->inGlobalScope());
    delete this->scopes.back();
    this->scopes.pop_back();
}

void SymbolTableBase::enterFunc() {
    this->scopes.push_back(new Scope());
    this->maxVarIndexStack.push_back(0);
}

void SymbolTableBase::exitFunc() {
    assert(!this->inGlobalScope());
    delete this->scopes.back();
    this->scopes.pop_back();
    this->maxVarIndexStack.pop_back();
}

void SymbolTableBase::commit() {
    assert(this->inGlobalScope());
    this->handleCache.clear();
    this->maxVarIndexStack.clear();
    this->maxVarIndexStack.push_back(0);
}

void SymbolTableBase::abort() {
    // pop local scope and function scope
    while(!this->inGlobalScope()) {
        delete this->scopes.back();
        this->scopes.pop_back();
    }
    while(this->maxVarIndexStack.size() > 1) {
        this->maxVarIndexStack.pop_back();
    }

    // remove cached entry
    assert(this->inGlobalScope());
    for(auto &p : this->handleCache) {
        this->scopes.back()->deleteHandle(p);
    }
}


// #####################
// ##     TypeMap     ##
// #####################

static bool isAlias(const DSType *type) {
    assert(type != nullptr);
    return (reinterpret_cast<long>(type)) < 0;
}

static unsigned long asKey(const DSType *type) {
    assert(type != nullptr);
    return reinterpret_cast<unsigned long>(type);
}

TypeMap::~TypeMap() {
    for(auto pair : this->typeMapImpl) {
        if(!isAlias(pair.second)) {
            delete pair.second;
        }
    }
}

DSType *TypeMap::addType(std::string &&typeName, DSType *type) {
    assert(type != nullptr);
    auto pair = this->typeMapImpl.insert(std::make_pair(std::move(typeName), type));
    this->typeNameMap.insert(std::make_pair(asKey(type), &pair.first->first));
//    this->typeCache.push_back(&pair.first->first);
    return type;
}

DSType *TypeMap::getType(const std::string &typeName) const {
    constexpr unsigned long mask = ~(1L << 63);
    auto iter = this->typeMapImpl.find(typeName);
    if(iter != this->typeMapImpl.end()) {
        DSType *type = iter->second;
        if(isAlias(type)) {   // if tagged pointer, mask tag
            return reinterpret_cast<DSType *>(mask & (unsigned long) type);
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

bool TypeMap::setAlias(std::string &&alias, DSType &targetType) {
    constexpr unsigned long tag = 1L << 63;

    /**
     * use tagged pointer to prevent double free.
     */
    auto *taggedPtr = reinterpret_cast<DSType *>(tag | (unsigned long) &targetType);
    auto pair = this->typeMapImpl.insert(std::make_pair(std::move(alias), taggedPtr));
//    this->typeCache.push_back(&pair.first->first);
    return pair.second;
}

void TypeMap::commit() {
    this->typeCache.clear();
}

void TypeMap::abort() {
//    for(const std::string *typeName : this->typeCache) {
//        this->removeType(*typeName);
//    }
    this->typeCache.clear();
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


// #########################
// ##     SymbolTable     ##
// #########################

SymbolTable::SymbolTable() : typeTable(new DSType*[static_cast<unsigned int>(TYPE::__SIZE_OF_DS_TYPE__)]()), templateMap(8) {
    const char *blacklist[] = {
            "eval",
            "exit",
            "exec",
            "command",
    };
    for(auto &e : blacklist) {
        this->forbitCmdRedefinition(e);
    }

    // initialize type
    this->initBuiltinType(TYPE::_Root, "pseudo top%%", false, info_Dummy()); // pseudo base type

    this->initBuiltinType(TYPE::Any, "Any", true, this->get(TYPE::_Root), info_AnyType());
    this->initBuiltinType(TYPE::Void, "Void", false, info_Dummy());
    this->initBuiltinType(TYPE::Nothing, "Nothing", false, info_Dummy());
    this->initBuiltinType(TYPE::Variant, "Variant", false, this->get(TYPE::Any), info_Dummy());

    /**
     * hidden from script.
     */
    this->initBuiltinType(TYPE::_Value, "Value%%", true, this->get(TYPE::Variant), info_Dummy());

    this->initBuiltinType(TYPE::Byte, "Byte", false, this->get(TYPE::_Value), info_ByteType());
    this->initBuiltinType(TYPE::Int16, "Int16", false, this->get(TYPE::_Value), info_Int16Type());
    this->initBuiltinType(TYPE::Uint16, "Uint16", false, this->get(TYPE::_Value), info_Uint16Type());
    this->initBuiltinType(TYPE::Int32, "Int32", false, this->get(TYPE::_Value), info_Int32Type());
    this->initBuiltinType(TYPE::Uint32, "Uint32", false, this->get(TYPE::_Value), info_Uint32Type());
    this->initBuiltinType(TYPE::Int64, "Int64", false, this->get(TYPE::_Value), info_Int64Type());
    this->initBuiltinType(TYPE::Uint64, "Uint64", false, this->get(TYPE::_Value), info_Uint64Type());

    this->initBuiltinType(TYPE::Float, "Float", false, this->get(TYPE::_Value), info_FloatType());
    this->initBuiltinType(TYPE::Boolean, "Boolean", false, this->get(TYPE::_Value), info_BooleanType());
    this->initBuiltinType(TYPE::String, "String", false, this->get(TYPE::_Value), info_StringType());

    this->initBuiltinType(TYPE::ObjectPath, "ObjectPath", false, this->get(TYPE::_Value), info_ObjectPathType());
    this->initBuiltinType(TYPE::UnixFD, "UnixFD", false, this->get(TYPE::Any), info_UnixFDType());
    this->initBuiltinType(TYPE::Proxy, "Proxy", false, this->get(TYPE::Any), info_ProxyType());
    this->initBuiltinType(TYPE::DBus, "DBus", false, this->get(TYPE::Any), info_DBusType());
    this->initBuiltinType(TYPE::Bus, "Bus", false, this->get(TYPE::Any), info_BusType());
    this->initBuiltinType(TYPE::Service, "Service", false, this->get(TYPE::Any), info_ServiceType());
    this->initBuiltinType(TYPE::DBusObject, "DBusObject", false, this->get(TYPE::Proxy), info_DBusObjectType());

    this->initBuiltinType(TYPE::Error, "Error", true, this->get(TYPE::Any), info_ErrorType());
    this->initBuiltinType(TYPE::Job, "Job", false, this->get(TYPE::Any), info_JobType());
    this->initBuiltinType(TYPE::Func, "Func", false, this->get(TYPE::Any), info_Dummy());
    this->initBuiltinType(TYPE::StringIter, "StringIter%%", false, this->get(TYPE::Any), info_StringIterType());
    this->initBuiltinType(TYPE::Regex, "Regex", false, this->get(TYPE::Any), info_RegexType());
    this->initBuiltinType(TYPE::Signal, "Signal", false, this->get(TYPE::Any), info_SignalType());
    this->initBuiltinType(TYPE::Signals, "Signals", false, this->get(TYPE::Any), info_SignalsType());

    // register NativeFuncInfo to ErrorType
    ErrorType::registerFuncInfo(info_ErrorType());

    // initialize type template
    std::vector<DSType *> elements = {&this->get(TYPE::Any)};
    this->arrayTemplate = this->initTypeTemplate(TYPE_ARRAY, std::move(elements), info_ArrayType());

    elements = {&this->get(TYPE::_Value), &this->get(TYPE::Any)};
    this->mapTemplate = this->initTypeTemplate(TYPE_MAP, std::move(elements), info_MapType());

    elements = std::vector<DSType *>();
    this->tupleTemplate = this->initTypeTemplate(TYPE_TUPLE, std::move(elements), info_TupleType());   // pseudo template.

    elements = std::vector<DSType *>();
    this->optionTemplate = this->initTypeTemplate(TYPE_OPTION, std::move(elements), info_OptionType()); // pseudo template

    // init string array type(for command argument)
    std::vector<DSType *> types = {&this->get(TYPE::String)};
    this->setToTypeTable(TYPE::StringArray, &this->createReifiedType(this->getArrayTemplate(), std::move(types)));

    // init some error type
    this->initErrorType(TYPE::ArithmeticError, "ArithmeticError", this->get(TYPE::Error));
    this->initErrorType(TYPE::OutOfRangeError, "OutOfRangeError", this->get(TYPE::Error));
    this->initErrorType(TYPE::KeyNotFoundError, "KeyNotFoundError", this->get(TYPE::Error));
    this->initErrorType(TYPE::TypeCastError, "TypeCastError", this->get(TYPE::Error));
    this->initErrorType(TYPE::DBusError, "DBusError", this->get(TYPE::Error));
    this->initErrorType(TYPE::SystemError, "SystemError", this->get(TYPE::Error));
    this->initErrorType(TYPE::StackOverflowError, "StackOverflowError", this->get(TYPE::Error));
    this->initErrorType(TYPE::RegexSyntaxError, "RegexSyntaxError", this->get(TYPE::Error));
    this->initErrorType(TYPE::UnwrappingError, "UnwrappingError", this->get(TYPE::Error));

    this->registerDBusErrorTypes();

    // init internal status type
    this->initBuiltinType(TYPE::_InternalStatus, "internal status%%", false, this->get(TYPE::_Root), info_Dummy());
    this->initBuiltinType(TYPE::_ShellExit, "Shell Exit", false, this->get(TYPE::_InternalStatus), info_Dummy());
    this->initBuiltinType(TYPE::_AssertFail, "Assertion Error", false, this->get(TYPE::_InternalStatus), info_Dummy());

    // commit generated type
    this->typeMap.commit();
}

SymbolTable::~SymbolTable() {
    delete[] this->typeTable;
    for(auto &pair : this->templateMap) {
        delete pair.second;
    }
}

void SymbolTable::commit() {
    SymbolTableBase::commit();
    this->typeMap.commit();
}

void SymbolTable::abort(bool abortType) {
    SymbolTableBase::abort();
    if(abortType) {
        this->typeMap.abort();
    }
}

DSType &SymbolTable::getTypeAndThrowIfUndefined(const std::string &typeName) const {
    DSType *type = this->getType(typeName);
    if(type == nullptr) {
        RAISE_TL_ERROR(UndefinedType, typeName.c_str());
    }
    return *type;
}

const TypeTemplate &SymbolTable::getTypeTemplate(const std::string &typeName) const {
    auto iter = this->templateMap.find(typeName);
    if(iter == this->templateMap.end()) {
        RAISE_TL_ERROR(NotTemplate, typeName.c_str());
    }
    return *iter->second;
}

DSType &SymbolTable::createReifiedType(const TypeTemplate &typeTemplate,
                                    std::vector<DSType *> &&elementTypes) {
    if(this->tupleTemplate->getName() == typeTemplate.getName()) {
        return this->createTupleType(std::move(elementTypes));
    }

    flag8_set_t attr = this->optionTemplate->getName() == typeTemplate.getName() ? DSType::OPTION_TYPE : 0;

    // check each element type
    if(attr != 0u) {
        auto *type = elementTypes[0];
        if(type->isOptionType() || type->isVoidType() || type->isNothingType()) {
            RAISE_TL_ERROR(InvalidElement, this->getTypeName(*type));
        }
    } else {
        this->checkElementTypes(typeTemplate, elementTypes);
    }

    std::string typeName(this->toReifiedTypeName(typeTemplate, elementTypes));
    DSType *type = this->typeMap.getType(typeName);
    if(type == nullptr) {
        DSType *superType = attr != 0u ? nullptr :
                            this->asVariantType(elementTypes) ? &this->get(TYPE::Variant) : &this->get(TYPE::Any);
        return *this->typeMap.addType(std::move(typeName),
                                      new ReifiedType(typeTemplate.getInfo(), superType, std::move(elementTypes), attr));
    }
    return *type;
}

DSType &SymbolTable::createTupleType(std::vector<DSType *> &&elementTypes) {
    this->checkElementTypes(elementTypes);

    assert(!elementTypes.empty());

    std::string typeName(this->toTupleTypeName(elementTypes));
    DSType *type = this->typeMap.getType(typeName);
    if(type == nullptr) {
        DSType *superType = this->asVariantType(elementTypes) ? &this->get(TYPE::Variant) : &this->get(TYPE::Any);
        return *this->typeMap.addType(std::move(typeName),
                                      new TupleType(this->tupleTemplate->getInfo(), superType, std::move(elementTypes)));
    }
    return *type;
}

FunctionType &SymbolTable::createFuncType(DSType *returnType, std::vector<DSType *> &&paramTypes) {
    this->checkElementTypes(paramTypes);

    std::string typeName(toFunctionTypeName(returnType, paramTypes));
    DSType *type = this->typeMap.getType(typeName);
    if(type == nullptr) {
        auto *funcType = new FunctionType(&this->get(TYPE::Func), returnType, std::move(paramTypes));
        this->typeMap.addType(std::move(typeName), funcType);
        return *funcType;
    }
    assert(type->isFuncType());

    return *static_cast<FunctionType *>(type);
}

InterfaceType &SymbolTable::createInterfaceType(const std::string &interfaceName) {
    DSType *type = this->typeMap.getType(interfaceName);
    if(type == nullptr) {
        auto *ifaceType = new InterfaceType(&this->get(TYPE::DBusObject));
        this->typeMap.addType(std::string(interfaceName), ifaceType);
        return *ifaceType;
    }
    assert(type->isInterface());

    return *static_cast<InterfaceType *>(type);
}

DSType &SymbolTable::createErrorType(const std::string &errorName, DSType &superType) {
    DSType *type = this->typeMap.getType(errorName);
    if(type == nullptr) {
        DSType *errorType = new ErrorType(&superType);
        this->typeMap.addType(std::string(errorName), errorType);
        return *errorType;
    }
    return *type;
}

DSType &SymbolTable::getDBusInterfaceType(const std::string &typeName) {
    DSType *type = this->typeMap.getType(typeName);
    if(type == nullptr) {
        // load dbus interface
        std::string ifacePath(getIfaceDir());
        ifacePath += "/";
        ifacePath += typeName;

        auto node = parse(ifacePath.c_str());
        if(!node) {
            RAISE_TL_ERROR(NoDBusInterface, typeName.c_str());
        }
        if(!node->is(NodeKind::Interface)) {
            RAISE_TL_ERROR(NoDBusInterface, typeName.c_str());
        }

        auto *ifaceNode = static_cast<InterfaceNode *>(node.get());
        return TypeGenerator(*this).resolveInterface(ifaceNode);
    }
    return *type;
}

void SymbolTable::setAlias(const char *alias, DSType &targetType) {
    if(!this->typeMap.setAlias(std::string(alias), targetType)) {
        RAISE_TL_ERROR(DefinedType, alias);
    }
}

std::string SymbolTable::toReifiedTypeName(const std::string &name, const std::vector<DSType *> &elementTypes) const {
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

std::string SymbolTable::toFunctionTypeName(DSType *returnType, const std::vector<DSType *> &paramTypes) const {
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

constexpr int SymbolTable::INT64_PRECISION;
constexpr int SymbolTable::INT32_PRECISION;
constexpr int SymbolTable::INT16_PRECISION;
constexpr int SymbolTable::BYTE_PRECISION;
constexpr int SymbolTable::INVALID_PRECISION;

int SymbolTable::getIntPrecision(const DSType &type) const {
    const struct {
        TYPE t;
        int precision;
    } table[] = {
            // Int64, Uint64
            {TYPE::Int64, INT64_PRECISION},
            {TYPE::Uint64, INT64_PRECISION},
            // Int32, Uint32
            {TYPE::Int32, INT32_PRECISION},
            {TYPE::Uint32, INT32_PRECISION},
            // Int16, Uint16
            {TYPE::Int16, INT16_PRECISION},
            {TYPE::Uint16, INT16_PRECISION},
            // Byte
            {TYPE::Byte, BYTE_PRECISION},
    };

    for(auto &e : table) {
        if(this->get(e.t) == type) {
            return e.precision;
        }
    }
    return INVALID_PRECISION;
}

static const TYPE numTypeTable[] = {
        TYPE::Byte,   // 0
        TYPE::Int16,  // 1
        TYPE::Uint16, // 2
        TYPE::Int32,  // 3
        TYPE::Uint32, // 4
        TYPE::Int64,  // 5
        TYPE::Uint64, // 6
        TYPE::Float,  // 7
};

int SymbolTable::getNumTypeIndex(const DSType &type) const {
    for(unsigned int i = 0; i < arraySize(numTypeTable); i++) {
        if(this->get(numTypeTable[i]) == type) {
            return i;
        }
    }
    return -1;
}

DSType *SymbolTable::getByNumTypeIndex(unsigned int index) const {
    return index < arraySize(numTypeTable) ?
           this->typeTable[static_cast<unsigned int>(numTypeTable[index])] : nullptr;
}

void SymbolTable::setToTypeTable(TYPE t, DSType *type) {
    assert(this->typeTable[static_cast<unsigned int>(t)] == nullptr && type != nullptr);
    this->typeTable[static_cast<unsigned int>(t)] = type;
}

void SymbolTable::initBuiltinType(TYPE t, const char *typeName, bool extendable,
                               native_type_info_t info) {
    // create and register type
    flag8_set_t attribute = extendable ? DSType::EXTENDIBLE : 0;
    if(t == TYPE::Void) {
        attribute |= DSType::VOID_TYPE;
    }
    if(t == TYPE::Nothing) {
        attribute |= DSType::NOTHING_TYPE;
    }

    DSType *type = this->typeMap.addType(
            std::string(typeName), new BuiltinType(nullptr, info, attribute));

    // set to typeTable
    this->setToTypeTable(t, type);
}

void SymbolTable::initBuiltinType(TYPE t, const char *typeName, bool extendable,
                                  DSType &superType, native_type_info_t info) {
    // create and register type
    DSType *type = this->typeMap.addType(
            std::string(typeName), new BuiltinType(&superType, info, extendable ? DSType::EXTENDIBLE : 0));

    // set to typeTable
    this->setToTypeTable(t, type);
}

TypeTemplate *SymbolTable::initTypeTemplate(const char *typeName,
                                            std::vector<DSType *> &&elementTypes, native_type_info_t info) {
    return this->templateMap.insert(
            std::make_pair(typeName, new TypeTemplate(std::string(typeName),
                                                      std::move(elementTypes), info))).first->second;
}

void SymbolTable::initErrorType(TYPE t, const char *typeName, DSType &superType) {
    DSType *type = this->typeMap.addType(std::string(typeName), new ErrorType(&superType));
    this->setToTypeTable(t, type);
}

void SymbolTable::checkElementTypes(const std::vector<DSType *> &elementTypes) const {
    for(DSType *type : elementTypes) {
        if(type->isVoidType() || type->isNothingType()) {
            RAISE_TL_ERROR(InvalidElement, this->getTypeName(*type));
        }
    }
}

void SymbolTable::checkElementTypes(const TypeTemplate &t, const std::vector<DSType *> &elementTypes) const {
    const unsigned int size = elementTypes.size();

    // check element type size
    if(t.getElementTypeSize() != size) {
        RAISE_TL_ERROR(UnmatchElement, t.getName().c_str(), t.getElementTypeSize(), size);
    }

    for(unsigned int i = 0; i < size; i++) {
        auto *acceptType = t.getAcceptableTypes()[i];
        auto *elementType = elementTypes[i];
        if(acceptType->isSameOrBaseTypeOf(*elementType) && !elementType->isNothingType()) {
            continue;
        }
        if(*acceptType == this->get(TYPE::Any) && elementType->isOptionType()) {
            continue;
        }
        RAISE_TL_ERROR(InvalidElement, this->getTypeName(*elementType));
    }
}

bool SymbolTable::asVariantType(const std::vector<DSType *> &elementTypes) const {
    for(DSType *type : elementTypes) {
        if(!this->get(TYPE::Variant).isSameOrBaseTypeOf(*type)) {
            return false;
        }
    }
    return true;
}

void SymbolTable::registerDBusErrorTypes() {
    const char *table[] = {
            "Failed",
            "NoMemory",
            "ServiceUnknown",
            "NameHasNoOwner",
            "NoReply",
            "IOError",
            "BadAddress",
            "NotSupported",
            "LimitsExceeded",
            "AccessDenied",
            "AuthFailed",
            "NoServer",
            "Timeout",
            "NoNetwork",
            "AddressInUse",
            "Disconnected",
            "InvalidArgs",
            "FileNotFound",
            "FileExists",
            "UnknownMethod",
            "UnknownObject",
            "UnknownInterface",
            "UnknownProperty",
            "PropertyReadOnly",
            "TimedOut",
            "MatchRuleNotFound",
            "MatchRuleInvalid",
            "Spawn.ExecFailed",
            "Spawn.ForkFailed",
            "Spawn.ChildExited",
            "Spawn.ChildSignaled",
            "Spawn.Failed",
            "Spawn.FailedToSetup",
            "Spawn.ConfigInvalid",
            "Spawn.ServiceNotValid",
            "Spawn.ServiceNotFound",
            "Spawn.PermissionsInvalid",
            "Spawn.FileInvalid",
            "Spawn.NoMemory",
            "UnixProcessIdUnknown",
            "InvalidSignature",
            "InvalidFileContent",
            "SELinuxSecurityContextUnknown",
            "AdtAuditDataUnknown",
            "ObjectPathInUse",
            "InconsistentMessage",
            "InteractiveAuthorizationRequired",
    };

    for(const auto &e : table) {
        std::string s = "org.freedesktop.DBus.Error.";
        s += e;
        this->setAlias(e, this->createErrorType(s, this->get(TYPE::DBusError)));
    }
}


} // namespace ydsh