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
#include "core.h"
#include "object.h"
#include "constant.h"
#include "logger.h"
#include "misc/files.h"

namespace ydsh {

template <unsigned int N>
std::array<NativeCode, N> initNative(const NativeFuncInfo (&e)[N]) {
    std::array<NativeCode, N> array;
    for(unsigned int i = 0; i < N; i++) {
        array[i] = NativeCode(e[i].func_ptr, e[i].hasRet);
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
        return Err(SymbolError::DEFINED);
    }
    if(pair.first->second) {
        this->curVarIndex++;
    } else {
        this->shadowCount++;
    }
    if(this->getCurVarIndex() > UINT8_MAX) {
        return Err(SymbolError::LIMIT);
    }
    return Ok(&pair.first->second);
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
                                  FieldAttribute attribute, unsigned short modID) {
    setFlag(attribute, FieldAttribute::GLOBAL);
    FieldHandle handle(&type, this->gvarCount.get(), attribute, modID);
    auto pair = this->handleMap.emplace(symbolName, handle);
    if(!pair.second) {
        return Err(SymbolError::DEFINED);
    }
    if(pair.first->second) {
        this->gvarCount.get()++;
    }
    return Ok(&pair.first->second);
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
                                     DSType &type, FieldAttribute attribute) {
    if(this->inGlobalScope()) {
        if(this->builtin) {
            setFlag(attribute, FieldAttribute::BUILTIN);
        }
        return this->globalScope.addNew(symbolName, type, attribute, this->modID);
    }

    FieldHandle handle(&type, this->scopes.back().getCurVarIndex(), attribute, this->modID);
    auto ret = this->scopes.back().add(symbolName, handle);
    if(ret) {
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

const char* ModuleScope::import(const ModType &type) {
    for(auto &e : type.handleMap) {
        assert(!hasFlag(e.second.attr(), FieldAttribute::BUILTIN));
        if(e.first[0] == '_' && this->getModID() != e.second.getModID()) {
            continue;
        }
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
        DSType(id, &superType, TypeAttr::MODULE_TYPE), modID(modID) {
    assert(modID > 0);
    for(auto &e : handleMap) {
        if(e.second.getModID() == modID) {
            this->handleMap.emplace(e.first, e.second);
        }
    }
}

FieldHandle* ModType::lookupFieldHandle(SymbolTable &symbolTable, const std::string &fieldName) {
    auto iter = this->handleMap.find(fieldName);
    if(iter != this->handleMap.end()) {
        if(fieldName[0] == '_' && symbolTable.currentModID() != iter->second.getModID()) {
            return nullptr;
        }
        return &iter->second;
    }
    return nullptr;
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

ModResult ModuleLoader::load(const char *scriptDir, const std::string &modPath, FilePtr &filePtr) {
    std::string str = expandDots(scriptDir, modPath.c_str());
    LOG(TRACE_MODULE, "\n    scriptDir: `%s'\n    modPath: `%s'\n    fullPath: `%s'",
                       (scriptDir == nullptr ? "" : scriptDir), modPath.c_str(), str.c_str());

    auto pair = this->typeMap.emplace(std::move(str), nullptr);
    if(!pair.second) {
        if(pair.first->second) {
            return pair.first->second;
        }
        return ModLoadingError::CIRCULAR;
    }

    const char *resolvedPath = pair.first->first.c_str();
    filePtr = createFilePtr(fopen, resolvedPath, "rb");
    if(!filePtr) {
        int old = errno;
        this->typeMap.erase(pair.first);
        errno = old;
        if(errno == ENOENT) {
            return ModLoadingError::NOT_FOUND;
        }
        return ModLoadingError::NOT_OPEN;
    } else if(S_ISDIR(getStMode(fileno(filePtr.get())))) {
        this->typeMap.erase(pair.first);
        filePtr.reset();
        errno = EISDIR;
        return ModLoadingError::NOT_OPEN;
    }
    return resolvedPath;
}


// #########################
// ##     SymbolTable     ##
// #########################

SymbolTable::SymbolTable() :
        rootModule(this->gvarCount), curModule(&this->rootModule) {

    // initialize type
    this->initBuiltinType(TYPE::_Root, "pseudo top%%", false, info_Dummy()); // pseudo base type

    this->initBuiltinType(TYPE::Any, "Any", true, this->get(TYPE::_Root), info_AnyType());
    this->initBuiltinType(TYPE::Void, "Void", false, info_Dummy());
    this->initBuiltinType(TYPE::Nothing, "Nothing", false, info_Dummy());

    /**
     * hidden from script.
     */
    this->initBuiltinType(TYPE::_Value, "Value%%", true, this->get(TYPE::Any), info_Dummy());

    this->initBuiltinType(TYPE::Int32, "Int32", false, this->get(TYPE::_Value), info_Int32Type());
    this->initBuiltinType(TYPE::Int64, "Int64", false, this->get(TYPE::_Value), info_Int64Type());
    this->initBuiltinType(TYPE::Float, "Float", false, this->get(TYPE::_Value), info_FloatType());
    this->initBuiltinType(TYPE::Boolean, "Boolean", false, this->get(TYPE::_Value), info_BooleanType());
    this->initBuiltinType(TYPE::String, "String", false, this->get(TYPE::_Value), info_StringType());

    this->initBuiltinType(TYPE::Regex, "Regex", false, this->get(TYPE::Any), info_RegexType());
    this->initBuiltinType(TYPE::Signal, "Signal", false, this->get(TYPE::_Value), info_SignalType());
    this->initBuiltinType(TYPE::Signals, "Signals", false, this->get(TYPE::Any), info_SignalsType());
    this->initBuiltinType(TYPE::Error, "Error", true, this->get(TYPE::Any), info_ErrorType());
    this->initBuiltinType(TYPE::Job, "Job", false, this->get(TYPE::Any), info_JobType());
    this->initBuiltinType(TYPE::Func, "Func", false, this->get(TYPE::Any), info_Dummy());
    this->initBuiltinType(TYPE::StringIter, "StringIter%%", false, this->get(TYPE::Any), info_StringIterType());
    this->initBuiltinType(TYPE::UnixFD, "UnixFD", false, this->get(TYPE::Any), info_UnixFDType());

    // register NativeFuncInfo to ErrorType
    ErrorType::registerFuncInfo(info_ErrorType());

    // initialize type template
    std::vector<DSType *> elements = {&this->get(TYPE::Any)};
     this->initTypeTemplate(this->arrayTemplate, TYPE_ARRAY, std::move(elements), info_ArrayType());

    elements = {&this->get(TYPE::_Value), &this->get(TYPE::Any)};
    this->initTypeTemplate(this->mapTemplate, TYPE_MAP, std::move(elements), info_MapType());

    elements = std::vector<DSType *>();
    this->initTypeTemplate(this->tupleTemplate, TYPE_TUPLE, std::move(elements), info_TupleType());   // pseudo template.

    elements = std::vector<DSType *>();
    this->initTypeTemplate(this->optionTemplate, TYPE_OPTION, std::move(elements), info_OptionType()); // pseudo template

    // init string array type(for command argument)
    {
        std::vector<DSType *> types = {&this->get(TYPE::String)};
        auto checked = this->createReifiedType(this->getArrayTemplate(), std::move(types));    // TYPE::StringArray
        (void) checked;
        assert(checked);
    }

    // init some error type
    this->initErrorType(TYPE::ArithmeticError, "ArithmeticError");
    this->initErrorType(TYPE::OutOfRangeError, "OutOfRangeError");
    this->initErrorType(TYPE::KeyNotFoundError, "KeyNotFoundError");
    this->initErrorType(TYPE::TypeCastError, "TypeCastError");
    this->initErrorType(TYPE::SystemError, "SystemError");
    this->initErrorType(TYPE::StackOverflowError, "StackOverflowError");
    this->initErrorType(TYPE::RegexSyntaxError, "RegexSyntaxError");
    this->initErrorType(TYPE::UnwrappingError, "UnwrappingError");

    // init internal status type
    this->initBuiltinType(TYPE::_InternalStatus, "internal status%%", false, this->get(TYPE::_Root), info_Dummy());
    this->initBuiltinType(TYPE::_ShellExit, "Shell Exit", false, this->get(TYPE::_InternalStatus), info_Dummy());
    this->initBuiltinType(TYPE::_AssertFail, "Assertion Error", false, this->get(TYPE::_InternalStatus), info_Dummy());

    // commit generated type
    this->typeMap.commit();
}

static bool isFileNotFound(const ModResult &ret) {
    return is<ModLoadingError>(ret) && get<ModLoadingError>(ret) == ModLoadingError::NOT_FOUND;
}

ModResult SymbolTable::tryToLoadModule(const char *scriptDir, const char *path, FilePtr &filePtr) {
    std::string modPath = path;
    expandTilde(modPath);
    auto ret = this->modLoader.load(scriptDir, modPath, filePtr);
    if(modPath[0] == '/' || scriptDir == nullptr) {   // if full path, not search next path
        return ret;
    }
    if(strcmp(scriptDir, SYSTEM_MOD_DIR) == 0) {
        return ret;
    }

    if(isFileNotFound(ret)) {
        int old = errno;
        std::string dir = LOCAL_MOD_DIR;
        expandTilde(dir);
        errno = old;
        if(strcmp(scriptDir, dir.c_str()) != 0) {
            ret = this->modLoader.load(dir.c_str(), modPath, filePtr);
        }
        if(isFileNotFound(ret)) {
            ret = this->modLoader.load(SYSTEM_MOD_DIR, modPath, filePtr);
        }
    }
    return ret;
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
            if(ret && hasFlag(ret->attr(), FieldAttribute::BUILTIN)) {
                handle = ret;
            }
        }
    }
    return handle;
}

HandleOrError SymbolTable::newHandle(const std::string &symbolName, DSType &type,
                                     FieldAttribute attribute) {
    if(this->cur().inGlobalScope() && &this->cur() != &this->root()) {
        assert(this->root().inGlobalScope());
        auto handle = this->root().lookupHandle(symbolName);
        if(handle && hasFlag(handle->attr(), FieldAttribute::BUILTIN)) {
            return Err(SymbolError::DEFINED);
        }
    }
    return this->cur().newHandle(symbolName, type, attribute);
}

TypeOrError SymbolTable::getTypeOrError(const std::string &typeName) const {
    DSType *type = this->getType(typeName);
    if(type == nullptr) {
        RAISE_TL_ERROR(UndefinedType, typeName.c_str());
    }
    return Ok(type);
}

TypeTempOrError SymbolTable::getTypeTemplate(const std::string &typeName) const {
    auto iter = this->templateMap.find(typeName);
    if(iter == this->templateMap.end()) {
        RAISE_TL_ERROR(NotTemplate, typeName.c_str());
    }
    return Ok(iter->second);
}

TypeOrError SymbolTable::createReifiedType(const TypeTemplate &typeTemplate,
                                    std::vector<DSType *> &&elementTypes) {
    if(this->tupleTemplate.getName() == typeTemplate.getName()) {
        return this->createTupleType(std::move(elementTypes));
    }

    TypeAttr attr{};
    if(this->optionTemplate.getName() == typeTemplate.getName()) {
        setFlag(attr, TypeAttr::OPTION_TYPE);
    }

    // check each element type
    if(hasFlag(attr, TypeAttr::OPTION_TYPE)) {
        auto *type = elementTypes[0];
        if(type->isVoidType() || type->isNothingType()) {
            RAISE_TL_ERROR(InvalidElement, this->getTypeName(*type));
        } else if(type->isOptionType()) {
            return Ok(type);
        }
    } else {
        auto checked = this->checkElementTypes(typeTemplate, elementTypes);
        if(!checked) {
            return checked;
        }
    }

    std::string typeName(this->toReifiedTypeName(typeTemplate, elementTypes));
    DSType *type = this->typeMap.getType(typeName);
    if(type == nullptr) {
        DSType *superType = hasFlag(attr, TypeAttr::OPTION_TYPE) ? nullptr : &this->get(TYPE::Any);
        type = &this->typeMap.newType<ReifiedType>(
                std::move(typeName),
                typeTemplate.getInfo(), superType, std::move(elementTypes), attr);
    }
    return Ok(type);
}

TypeOrError SymbolTable::createTupleType(std::vector<DSType *> &&elementTypes) {
    auto checked = this->checkElementTypes(elementTypes);
    if(!checked) {
        return checked;
    }

    assert(!elementTypes.empty());

    std::string typeName(this->toTupleTypeName(elementTypes));
    DSType *type = this->typeMap.getType(typeName);
    if(type == nullptr) {
        DSType *superType = &this->get(TYPE::Any);
        type = &this->typeMap.newType<TupleType>(
                std::move(typeName),
                this->tupleTemplate.getInfo(), superType, std::move(elementTypes));
    }
    return Ok(type);
}

TypeOrError SymbolTable::createFuncType(DSType *returnType, std::vector<DSType *> &&paramTypes) {
    auto checked = this->checkElementTypes(paramTypes);
    if(!checked) {
        return checked;
    }

    std::string typeName(toFunctionTypeName(returnType, paramTypes));
    DSType *type = this->typeMap.getType(typeName);
    if(type == nullptr) {
        type = &this->typeMap.newType<FunctionType>(
                std::move(typeName),
                &this->get(TYPE::Func), returnType, std::move(paramTypes));
    }
    assert(type->isFuncType());
    return Ok(type);
}

bool SymbolTable::setAlias(const char *alias, DSType &targetType) {
    return this->typeMap.setAlias(std::string(alias), targetType.getTypeID());
}

std::string SymbolTable::toReifiedTypeName(const ydsh::TypeTemplate &typeTemplate,
                                           const std::vector<DSType *> &elementTypes) const {
    if(typeTemplate == this->getArrayTemplate()) {
        std::string str = "[";
        str += this->getTypeName(*elementTypes[0]);
        str += "]";
        return str;
    } else if(typeTemplate == this->getMapTemplate()) {
        std::string str = "[";
        str += this->getTypeName(*elementTypes[0]);
        str += " : ";
        str += this->getTypeName(*elementTypes[1]);
        str += "]";
        return str;
    } else if(typeTemplate == this->getOptionTemplate()) {
        auto *type = elementTypes[0];
        std::string str;
        if(type->isFuncType()) {
            str += "(";
        }
        str += this->getTypeName(*type);
        if(type->isFuncType()) {
            str += ")";
        }
        str += "!";
        return str;
    } else {
        unsigned int elementSize = elementTypes.size();
        std::string str = typeTemplate.getName();
        str += "<";
        for(unsigned int i = 0; i < elementSize; i++) {
            if(i > 0) {
                str += ",";
            }
            str += this->getTypeName(*elementTypes[i]);
        }
        str += ">";
        return str;
    }
}

std::string SymbolTable::toTupleTypeName(const std::vector<DSType *> &elementTypes) const {
    std::string str = "(";
    for(unsigned int i = 0; i < elementTypes.size(); i++) {
        if(i > 0) {
            str += ", ";
        }
        str += this->getTypeName(*elementTypes[i]);
    }
    if(elementTypes.size() == 1) {
        str += ",";
    }
    str += ")";
    return str;
}

std::string SymbolTable::toFunctionTypeName(DSType *returnType, const std::vector<DSType *> &paramTypes) const {
    std::string funcTypeName = "(";
    for(unsigned int i = 0; i < paramTypes.size(); i++) {
        if(i > 0) {
            funcTypeName += ", ";
        }
        funcTypeName += this->getTypeName(*paramTypes[i]);
    }
    funcTypeName += ") -> ";
    funcTypeName += this->getTypeName(*returnType);
    return funcTypeName;
}

void SymbolTable::initBuiltinType(TYPE t, const char *typeName, bool extendable,
                               native_type_info_t info) {
    // create and register type
    auto attribute = extendable ? TypeAttr::EXTENDIBLE : TypeAttr();
    auto &type = this->typeMap.newType<BuiltinType>(std::string(typeName), nullptr, info, attribute);
    (void) type;
    (void) t;
    assert(type.is(t));
}

void SymbolTable::initBuiltinType(TYPE t, const char *typeName, bool extendable,
                                  DSType &superType, native_type_info_t info) {
    // create and register type
    auto &type = this->typeMap.newType<BuiltinType>(
            std::string(typeName), &superType, info, extendable ? TypeAttr::EXTENDIBLE : TypeAttr());
    (void) type;
    (void) t;
    assert(type.is(t));
}

void SymbolTable::initTypeTemplate(TypeTemplate &temp, const char *typeName,
                                   std::vector<DSType *> &&elementTypes, native_type_info_t info) {
    temp = TypeTemplate(std::string(typeName), std::move(elementTypes), info);
    this->templateMap.insert({typeName, &temp});
}

void SymbolTable::initErrorType(TYPE t, const char *typeName) {
    auto &type = this->typeMap.newType<ErrorType>(std::string(typeName), &this->get(TYPE::Error));
    (void) type;
    (void) t;
    assert(type.is(t));
}

TypeOrError SymbolTable::checkElementTypes(const std::vector<DSType *> &elementTypes) const {
    for(DSType *type : elementTypes) {
        if(type->isVoidType() || type->isNothingType()) {
            RAISE_TL_ERROR(InvalidElement, this->getTypeName(*type));
        }
    }
    return Ok(static_cast<DSType *>(nullptr));
}

TypeOrError SymbolTable::checkElementTypes(const TypeTemplate &t, const std::vector<DSType *> &elementTypes) const {
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
        if(acceptType->is(TYPE::Any) && elementType->isOptionType()) {
            continue;
        }
        RAISE_TL_ERROR(InvalidElement, this->getTypeName(*elementType));
    }
    return Ok(static_cast<DSType *>(nullptr));
}

} // namespace ydsh