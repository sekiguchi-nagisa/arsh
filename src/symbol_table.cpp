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

// ########################
// ##     BlockScope     ##
// ########################

HandleOrError BlockScope::add(const std::string &symbolName, FieldHandle handle) {
    auto pair = this->handleMap.insert({symbolName, handle});
    if(!pair.second) {
        return {nullptr, SymbolError::DEFINED};
    }
    if(pair.first->second) {
        this->curVarIndex++;
    } else {
        this->shadowCount++;
    }
    if(this->getCurVarIndex() > UINT8_MAX) {
        return {nullptr, SymbolError::LIMIT};
    }
    return {&pair.first->second, SymbolError::DUMMY};
}

// #########################
// ##     GlobalScope     ##
// #########################

GlobalScope::GlobalScope(unsigned int &gvarCount) : gvarCount(gvarCount) {
    if(gvarCount == 0) {
        const char *blacklist[] = {
                "eval",
                "exit",
                "exec",
                "command",
        };
        for(auto &e : blacklist) {
            std::string name = CMD_SYMBOL_PREFIX;
            name += e;
            this->handleMap.emplace(std::move(name), FieldHandle());
        }
    }
}

HandleOrError GlobalScope::addNew(const std::string &symbolName, DSType &type,
                                  FieldAttributes attribute, unsigned short modID) {
    attribute.set(FieldAttribute::GLOBAL);
    FieldHandle handle(&type, this->gvarCount.get(), attribute, modID);
    auto pair = this->handleMap.emplace(symbolName, handle);
    if(!pair.second) {
        return {nullptr, SymbolError::DEFINED};
    }
    if(pair.first->second) {
        this->gvarCount.get()++;
    }
    return {&pair.first->second, SymbolError::DUMMY};
}


// #########################
// ##     ModuleScope     ##
// #########################

const FieldHandle *ModuleScope::lookupHandle(const std::string &symbolName) const {
    for(auto iter = this->scopes.crbegin(); iter != this->scopes.crend(); ++iter) {
        auto *handle = (*iter).lookup(symbolName);
        if(handle != nullptr) {
            return handle;
        }
    }
    return this->globalScope.lookup(symbolName);
}

HandleOrError ModuleScope::newHandle(const std::string &symbolName,
                                     DSType &type, FieldAttributes attribute) {
    if(this->inGlobalScope()) {
        if(this->builtin) {
            attribute.set(FieldAttribute::BUILTIN);
        }
        return this->globalScope.addNew(symbolName, type, attribute, this->modID);
    }

    FieldHandle handle(&type, this->scopes.back().getCurVarIndex(), attribute, this->modID);
    auto ret = this->scopes.back().add(symbolName, handle);
    if(ret.second == SymbolError::DUMMY) {
        unsigned int varIndex = this->scopes.back().getCurVarIndex();
        if(varIndex > this->maxVarIndexStack.back()) {
            this->maxVarIndexStack.back() = varIndex;
        }
    }
    return ret;
}

void ModuleScope::enterScope() {
    unsigned int index = 0;
    if(!this->inGlobalScope()) {
        index = this->scopes.back().getCurVarIndex();
    }
    this->scopes.emplace_back(index);
}

void ModuleScope::exitScope() {
    assert(!this->inGlobalScope());
    this->scopes.pop_back();
}

void ModuleScope::enterFunc() {
    this->scopes.emplace_back();
    this->maxVarIndexStack.push_back(0);
}

void ModuleScope::exitFunc() {
    assert(!this->inGlobalScope());
    this->scopes.pop_back();
    this->maxVarIndexStack.pop_back();
}

const char* ModuleScope::import(const ydsh::ModType &type) {
    for(auto &e : type.handleMap) {
        assert(!e.second.attr().has(FieldAttribute::BUILTIN));
        auto ret = this->globalScope.handleMap.insert(e);
        if(!ret.second && ret.first->second.getModID() != type.getModID()) {
            return ret.first->first.c_str();
        }
    }
    return nullptr;
}

void ModuleScope::clear() {
    this->maxVarIndexStack.clear();
    this->maxVarIndexStack.push_back(0);
    this->scopes.shrink_to_fit();
}

// #####################
// ##     TypeMap     ##
// #####################

TypeMap::~TypeMap() {
    for(auto &e : this->typeTable) {
        delete e;
    }
}

DSType *TypeMap::addType(std::string &&typeName, DSType *type) {
    assert(type != nullptr);
    this->nameTable.push_back(typeName);
    this->typeTable.push_back(type);
    bool s = this->setAlias(std::move(typeName), type->getTypeID());
    (void) s;
    assert(s);
    return type;
}

DSType *TypeMap::getType(const std::string &typeName) const {
    auto iter = this->aliasMap.find(typeName);
    if(iter == this->aliasMap.end()) {
        return nullptr;
    }
    return this->get(iter->second);
}

void TypeMap::abort() {
    for(unsigned int i = this->oldIDCount; i < this->typeTable.size(); i++) {
        delete this->typeTable[i];
    }
    this->typeTable.erase(this->typeTable.begin() + this->oldIDCount, this->typeTable.end());
    this->nameTable.erase(this->nameTable.begin() + this->oldIDCount, this->nameTable.end());

    for(auto iter = this->aliasMap.begin(); iter != this->aliasMap.end();) {
        if(iter->second >= this->oldIDCount) {
            iter = this->aliasMap.erase(iter);
        } else {
            ++iter;
        }
    }

    assert(this->oldIDCount == this->typeTable.size());
    assert(this->oldIDCount == this->nameTable.size());
}

// #####################
// ##     ModType     ##
// #####################

ModType::ModType(unsigned int id, ydsh::DSType &superType, unsigned short modID,
                 const std::unordered_map<std::string, ydsh::FieldHandle> &handleMap) :
        DSType(id, &superType, DSType::MODULE_TYPE), modID(modID) {
    assert(modID > 0);
    for(auto &e : handleMap) {
        if(e.second.getModID() == modID) {
            this->handleMap.emplace(e.first, e.second);
        }
    }
}

FieldHandle* ModType::lookupFieldHandle(ydsh::SymbolTable &, const std::string &fieldName) {
    auto iter = this->handleMap.find(fieldName);
    if(iter != this->handleMap.end()) {
        return &iter->second;
    }
    return nullptr;
}

void ModType::accept(ydsh::TypeVisitor *) {
    fatal("unsupported\n");
}

std::string ModType::toModName(unsigned short id) {
    std::string str = MOD_SYMBOL_PREFIX;
    str += std::to_string(id);
    return str;
}

// ##########################
// ##     ModuleLoader     ##
// ##########################

void ModuleLoader::abort() {
    for(auto iter = this->typeMap.begin(); iter != this->typeMap.end();) {
        if(!iter->second || iter->second->getModID() > this->oldIDCount) {
            iter = this->typeMap.erase(iter);
        } else {
            ++iter;
        }
    }
    this->modIDCount = this->oldIDCount;
}

ModResult ModuleLoader::load(const std::string &modPath) {
    std::string str = modPath;
    expandTilde(str);
    char *buf = realpath(str.c_str(), nullptr);
    if(buf == nullptr) {
        return ModResult::unresolved();
    }

    str = buf;
    free(buf);

    auto pair = this->typeMap.emplace(std::move(str), nullptr);
    if(!pair.second) {
        if(pair.first->second) {
            return ModResult(pair.first->second);
        }
        return ModResult::circular(pair.first->first.c_str());
    }
    return ModResult(pair.first->first.c_str());
}


// #########################
// ##     SymbolTable     ##
// #########################

SymbolTable::SymbolTable() :
        rootModule(this->gvarCount), curModule(&this->rootModule), templateMap(8) {

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

    this->initBuiltinType(TYPE::Regex, "Regex", false, this->get(TYPE::Any), info_RegexType());
    this->initBuiltinType(TYPE::Signal, "Signal", false, this->get(TYPE::Any), info_SignalType());
    this->initBuiltinType(TYPE::Signals, "Signals", false, this->get(TYPE::Any), info_SignalsType());
    this->initBuiltinType(TYPE::Error, "Error", true, this->get(TYPE::Any), info_ErrorType());
    this->initBuiltinType(TYPE::Job, "Job", false, this->get(TYPE::Any), info_JobType());
    this->initBuiltinType(TYPE::Func, "Func", false, this->get(TYPE::Any), info_Dummy());
    this->initBuiltinType(TYPE::StringIter, "StringIter%%", false, this->get(TYPE::Any), info_StringIterType());

    this->initBuiltinType(TYPE::ObjectPath, "ObjectPath", false, this->get(TYPE::_Value), info_ObjectPathType());
    this->initBuiltinType(TYPE::UnixFD, "UnixFD", false, this->get(TYPE::Any), info_UnixFDType());
    this->initBuiltinType(TYPE::Proxy, "Proxy", false, this->get(TYPE::Any), info_ProxyType());
    this->initBuiltinType(TYPE::DBus, "DBus", false, this->get(TYPE::Any), info_DBusType());
    this->initBuiltinType(TYPE::Bus, "Bus", false, this->get(TYPE::Any), info_BusType());
    this->initBuiltinType(TYPE::Service, "Service", false, this->get(TYPE::Any), info_ServiceType());
    this->initBuiltinType(TYPE::DBusObject, "DBusObject", false, this->get(TYPE::Proxy), info_DBusObjectType());

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
    this->createReifiedType(this->getArrayTemplate(), std::move(types));    // TYPE::StringArray

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

    // init internal status type
    this->initBuiltinType(TYPE::_InternalStatus, "internal status%%", false, this->get(TYPE::_Root), info_Dummy());
    this->initBuiltinType(TYPE::_ShellExit, "Shell Exit", false, this->get(TYPE::_InternalStatus), info_Dummy());
    this->initBuiltinType(TYPE::_AssertFail, "Assertion Error", false, this->get(TYPE::_InternalStatus), info_Dummy());

    this->registerDBusErrorTypes();

    // commit generated type
    this->typeMap.commit();
}

ModType& SymbolTable::createModType(const std::string &fullpath) {
    std::string name = ModType::toModName(this->cur().getModID());
    auto &modType = this->typeMap.newType<ModType>(std::move(name),
            this->get(TYPE::Any), this->cur().getModID(), this->cur().global().getHandleMap());
    this->curModule = nullptr;
    auto iter = this->modLoader.typeMap.find(fullpath);
    assert(iter != this->modLoader.typeMap.end());
    assert(iter->second == nullptr);
    iter->second = &modType;
    return modType;
}

const FieldHandle* SymbolTable::lookupHandle(const std::string &symbolName) const {
    auto handle = this->cur().lookupHandle(symbolName);
    if(handle == nullptr) {
        if(&this->cur() != &this->root()) {
            assert(this->root().inGlobalScope());
            auto ret = this->root().lookupHandle(symbolName);
            if(ret && ret->attr().has(FieldAttribute::BUILTIN)) {
                handle = ret;
            }
        }
    }
    return handle;
}

HandleOrError SymbolTable::newHandle(const std::string &symbolName, DSType &type,
                                     FieldAttributes attribute) {
    if(this->cur().inGlobalScope() && &this->cur() != &this->root()) {
        assert(this->root().inGlobalScope());
        auto handle = this->root().lookupHandle(symbolName);
        if(handle && handle->attr().has(FieldAttribute::BUILTIN)) {
            return {nullptr, SymbolError::DEFINED};
        }
    }
    return this->cur().newHandle(symbolName, type, attribute);
}

DSType &SymbolTable::getTypeOrThrow(const std::string &typeName) const {
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
        return this->typeMap.newType<ReifiedType>(std::move(typeName),
                                                  typeTemplate.getInfo(), superType, std::move(elementTypes), attr);
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
        return this->typeMap.newType<TupleType>(std::move(typeName),
                                                this->tupleTemplate->getInfo(), superType, std::move(elementTypes));
    }
    return *type;
}

FunctionType &SymbolTable::createFuncType(DSType *returnType, std::vector<DSType *> &&paramTypes) {
    this->checkElementTypes(paramTypes);

    std::string typeName(toFunctionTypeName(returnType, paramTypes));
    DSType *type = this->typeMap.getType(typeName);
    if(type == nullptr) {
        return this->typeMap.newType<FunctionType>(std::move(typeName),
                                                   &this->get(TYPE::Func), returnType, std::move(paramTypes));
    }
    assert(type->isFuncType());

    return *static_cast<FunctionType *>(type);
}

InterfaceType &SymbolTable::createInterfaceType(const std::string &interfaceName) {
    DSType *type = this->typeMap.getType(interfaceName);
    if(type == nullptr) {
        return this->typeMap.newType<InterfaceType>(std::string(interfaceName), &this->get(TYPE::DBusObject));
    }
    assert(type->isInterface());

    return *static_cast<InterfaceType *>(type);
}

DSType &SymbolTable::createErrorType(const std::string &errorName, DSType &superType) {
    DSType *type = this->typeMap.getType(errorName);
    if(type == nullptr) {
        return this->typeMap.newType<ErrorType>(std::string(errorName), &superType);
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
    if(!this->typeMap.setAlias(std::string(alias), targetType.getTypeID())) {
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
           this->typeMap.get(static_cast<unsigned int>(numTypeTable[index])) : nullptr;
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

    auto &type = this->typeMap.newType<BuiltinType>(std::string(typeName), nullptr, info, attribute);
    (void) type;
    (void) t;
    assert(type.getTypeID() == static_cast<unsigned int>(t));
}

void SymbolTable::initBuiltinType(TYPE t, const char *typeName, bool extendable,
                                  DSType &superType, native_type_info_t info) {
    // create and register type
    auto &type = this->typeMap.newType<BuiltinType>(
            std::string(typeName), &superType, info, extendable ? DSType::EXTENDIBLE : 0);
    (void) type;
    (void) t;
    assert(type.getTypeID() == static_cast<unsigned int>(t));
}

TypeTemplate *SymbolTable::initTypeTemplate(const char *typeName,
                                            std::vector<DSType *> &&elementTypes, native_type_info_t info) {
    return this->templateMap.insert(
            {typeName, new TypeTemplate(std::string(typeName), std::move(elementTypes), info)}).first->second;
}

void SymbolTable::initErrorType(TYPE t, const char *typeName, DSType &superType) {
    auto &type = this->typeMap.newType<ErrorType>(std::string(typeName), &superType);
    (void) type;
    (void) t;
    assert(type.getTypeID() == static_cast<unsigned int>(t));
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