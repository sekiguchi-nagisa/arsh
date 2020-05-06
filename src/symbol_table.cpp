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
#include "logger.h"
#include "misc/files.h"

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

HandleOrError GlobalScope::addNew(const std::string &symbolName, const DSType &type,
                                  FieldAttribute attribute, unsigned short modID) {
    setFlag(attribute, FieldAttribute::GLOBAL);
    FieldHandle handle(type, this->gvarCount.get(), attribute, modID);
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
                                     const DSType &type, FieldAttribute attribute) {
    if(this->inGlobalScope()) {
        if(this->builtin) {
            setFlag(attribute, FieldAttribute::BUILTIN);
        }
        return this->globalScope.addNew(symbolName, type, attribute, this->modID);
    }

    FieldHandle handle(type, this->scopes.back().getCurVarIndex(), attribute, this->modID);
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

const FieldHandle* ModType::lookupFieldHandle(SymbolTable &symbolTable, const std::string &fieldName) const {
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

ModResult ModuleLoader::load(const char *scriptDir, const char *modPath, FilePtr &filePtr) {
    assert(modPath);

    std::string str = expandDots(scriptDir, modPath);
    LOG(TRACE_MODULE, "\n    scriptDir: `%s'\n    modPath: `%s'\n    fullPath: `%s'",
                       (scriptDir == nullptr ? "" : scriptDir), modPath, str.c_str());

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

static bool isFileNotFound(const ModResult &ret) {
    return is<ModLoadingError>(ret) && get<ModLoadingError>(ret) == ModLoadingError::NOT_FOUND;
}

ModResult SymbolTable::tryToLoadModule(const char *scriptDir, const char *path, FilePtr &filePtr) {
    auto ret = this->modLoader.load(scriptDir, path, filePtr);
    if(path[0] == '/' || scriptDir == nullptr || scriptDir[0] == '\0') {   // if full path, not search next path
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
            ret = this->modLoader.load(dir.c_str(), path, filePtr);
        }
        if(isFileNotFound(ret)) {
            ret = this->modLoader.load(SYSTEM_MOD_DIR, path, filePtr);
        }
    }
    return ret;
}

ModType& SymbolTable::createModType(const std::string &fullpath) {
    std::string name = ModType::toModName(this->cur().getModID());
    auto &modType = this->typePool.newType<ModType>(std::move(name),
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

HandleOrError SymbolTable::newHandle(const std::string &symbolName, const DSType &type,
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

unsigned int SymbolTable::getTermHookIndex() {
    if(this->termHookIndex == 0) {
        auto *handle = this->lookupHandle(VAR_TERM_HOOK);
        assert(handle != nullptr);
        this->termHookIndex = handle->getIndex();
    }
    return this->termHookIndex;
}

const FieldHandle *SymbolTable::lookupField(DSType &recvType, const std::string &fieldName) {
    return recvType.lookupFieldHandle(*this, fieldName);    //FIXME:
}

} // namespace ydsh