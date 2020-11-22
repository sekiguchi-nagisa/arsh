/*
 * Copyright (C) 2015-2020 Nagisa Sekiguchi
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

#include "scope.h"
#include "symbol_table.h"
#include "core.h"
#include "logger.h"
#include "misc/files.h"

namespace ydsh {

// ###################
// ##     Scope     ##
// ###################

const FieldHandle * Scope::lookup(const std::string &symbolName) const {
    for(auto *scope = this; scope != nullptr; scope = scope->prev.get()) {
        auto *handle = scope->find(symbolName);
        if(handle) {
            return handle;
        }
    }
    return nullptr;
}

HandleOrError Scope::add(const std::string &symbolName, FieldHandle &&handle) {
    auto pair = this->handleMap.emplace(symbolName, handle);
    if(!pair.second) {
        return Err(SymbolError::DEFINED);
    }
    return Ok(&pair.first->second);
}

// ########################
// ##     BlockScope     ##
// ########################

HandleOrError BlockScope::addNew(const std::string &symbolName, const DSType &type,
                                 FieldAttribute attribute, unsigned short modID) {
    unsetFlag(attribute, FieldAttribute::GLOBAL);
    auto ret = this->add(symbolName, type, this->getCurVarIndex(), attribute, modID);
    if(!ret) {
        return ret;
    }
    this->curVarIndex++;
    this->varSize++;
    if(this->getCurVarIndex() > UINT8_MAX) {
        return Err(SymbolError::LIMIT);
    }
    return ret;
}

// #########################
// ##     GlobalScope     ##
// #########################

GlobalScope::GlobalScope(unsigned int &gvarCount) : Scope(GLOBAL, nullptr), gvarCount(gvarCount) {
    RefCountOp<Scope>::increase(this);
}

HandleOrError GlobalScope::addNew(const std::string &symbolName, const DSType &type,
                                  FieldAttribute attribute, unsigned short modID) {
    setFlag(attribute, FieldAttribute::GLOBAL);
    auto ret = this->add(symbolName, type, this->gvarCount.get(), attribute, modID);
    if(ret) {
        this->gvarCount.get()++;
    }
    return ret;
}

// #########################
// ##     ModuleScope     ##
// #########################

HandleOrError ModuleScope::newHandle(const std::string &symbolName,
                                     const DSType &type, FieldAttribute attribute) {
    if(this->inGlobalScope()) {
        if(this->builtin) {
            setFlag(attribute, FieldAttribute::BUILTIN);
        }
        return this->globalScope.addNew(symbolName, type, attribute, this->modID);
    }

    auto ret = this->curScope().addNew(symbolName, type, attribute, this->modID);
    if(ret) {
        unsigned int varIndex = this->curScope().getCurVarIndex();
        if(varIndex > this->maxVarIndexStack.back()) {
            this->maxVarIndexStack.back() = varIndex;
        }
    }
    return ret;
}

HandleOrError ModuleScope::addAlias(const std::string &symbolName, const FieldHandle &handle) {
    auto attr = handle.attr();
    if(this->inGlobalScope()) {
        if(this->builtin) {
            setFlag(attr, FieldAttribute::BUILTIN);
        }
    }
    return this->scope->add(symbolName, handle, attr, this->modID);
}

void ModuleScope::enterScope() {
    unsigned int index = 0;
    if(!this->inGlobalScope()) {
        index = this->curScope().getCurVarIndex();
    }
    this->scope = IntrusivePtr<Scope>(new BlockScope(this->scope, index));
}

void ModuleScope::exitScope() {
    assert(!this->inGlobalScope());
    this->scope = this->scope->getPrev();
}

void ModuleScope::enterFunc() {
    this->scope = IntrusivePtr<Scope>(new BlockScope(this->scope));
    this->maxVarIndexStack.push_back(0);
}

void ModuleScope::exitFunc() {
    assert(!this->inGlobalScope());
    this->scope = this->scope->getPrev();
    this->maxVarIndexStack.pop_back();
}

bool ModuleScope::needGlobalImport(ChildModEntry entry) {
    auto iter = std::lower_bound(this->childs.begin(), this->childs.end(), entry,
                                 [](ChildModEntry x, ChildModEntry y) {
                                    return toTypeId(x) < toTypeId(y);
                                });
    if(iter != this->childs.end() && toTypeId(*iter) == toTypeId(entry)) {
        if(isGlobal(*iter)) {
            return false;
        }
        if(isGlobal(entry)) {
            *iter = entry;
        }
    } else {
        this->childs.insert(iter, entry);
    }
    return isGlobal(entry);
}

std::string ModuleScope::import(const ModType &type, bool global) {
    if(!this->needGlobalImport(toChildModEntry(type, global))) {
        return "";
    }

    for(auto &e : type.getHandleMap()) {
        assert(!hasFlag(e.second.attr(), FieldAttribute::BUILTIN));
        assert(this->getModID() != e.second.getModID());
        StringRef ref = e.first;
        if(ref.startsWith("_")) {
            continue;
        }
        const auto &symbolName = e.first;
        const auto &handle = e.second;
        auto ret = this->globalScope.add(symbolName, handle, handle.getModID());
        if(!ret) {
            StringRef name = symbolName;
            if(isCmdFullName(name)) {
                name.removeSuffix(strlen(CMD_SYMBOL_SUFFIX));
            } else if(isTypeAliasFullName(name)) {
                name.removeSuffix(strlen(TYPE_ALIAS_SYMBOL_SUFFIX));
            }
            return name.toString();
        }
    }
    return "";
}

void ModuleScope::clear() {
    this->maxVarIndexStack.clear();
    this->maxVarIndexStack.push_back(0);
    this->resetScope();
}

// ##########################
// ##     ModuleLoader     ##
// ##########################

void ModuleLoader::discard(const ModDiscardPoint discardPoint) {
    if(discardPoint.idCount >= this->modSize()) {
        return; // do nothing
    }

    for(auto iter = this->indexMap.begin(); iter != this->indexMap.end(); ) {
        if(iter->second.getIndex() >= discardPoint.idCount) {
            const char *ptr = iter->first.data();
            iter = this->indexMap.erase(iter);
            free(const_cast<char*>(ptr));
        } else {
            ++iter;
        }
    }
    this->gvarCount = discardPoint.gvarCount;
}

static CStrPtr expandToRealpath(const char *baseDir, const char *path) {
    if(!*path) {
        errno = ENOENT;
        return nullptr;
    }
    std::string value;
    if(!baseDir || *path == '/') {
        value = path;
    } else {
        assert(*baseDir);
        value = baseDir;
        value += '/';
        value += path;
    }
    return getRealpath(value.c_str());
}

static int checkFileType(const struct stat &st, ModLoadOption option) {
    if(S_ISDIR(st.st_mode)) {
        return EISDIR;
    } else if(S_ISREG(st.st_mode)) {
        if(st.st_size > static_cast<off_t>(static_cast<uint32_t>(-1) >> 2)) {
            return EFBIG;
        }
    } else if(hasFlag(option, ModLoadOption::IGNORE_NON_REG_FILE)) {
        return EINVAL;
    }
    return 0;
}

ModResult ModuleLoader::loadImpl(const char *scriptDir, const char *modPath,
                                 FilePtr &filePtr, ModLoadOption option) {
    assert(modPath);

    auto str = expandToRealpath(scriptDir, modPath);
    LOG(TRACE_MODULE, "\n    scriptDir: `%s'\n    modPath: `%s'\n    fullPath: `%s'",
                       (scriptDir == nullptr ? "(null)" : scriptDir),
                       modPath, str ? str.get() : "(null)");
    // check file type
    if(!str) {
        return ModLoadingError(ENOENT);
    }

    /**
     * check file type before open due to prevent named pipe blocking
     */
    struct stat st1;
    if(stat(str.get(), &st1) != 0) {
        return ModLoadingError(errno);
    }
    int s = checkFileType(st1, option);
    if(s) {
        return ModLoadingError(s);
    }

    int fd = open(str.get(), O_RDONLY);
    if(fd < 0) {
        return ModLoadingError(errno);
    }

    struct stat st2;
    errno = 0;
    if(fstat(fd, &st2) != 0 || !isSameFile(st1, st2)) {
        int old = errno == 0 ? ENOENT : errno;
        close(fd);
        return ModLoadingError(old);
    }

    auto ret = this->addNewModEntry(std::move(str));
    if(is<ModLoadingError>(ret)) {
        close(fd);
    } else {
        filePtr = createFilePtr(fdopen, fd, "rb");
        assert(filePtr);
    }
    return ret;
}

static bool isFileNotFound(const ModResult &ret) {
    return is<ModLoadingError>(ret) && get<ModLoadingError>(ret).isFileNotFound();
}

ModResult ModuleLoader::load(const char *scriptDir, const char *path, FilePtr &filePtr, ModLoadOption option) {
    auto ret = this->loadImpl(scriptDir, path, filePtr, option);
    if(path[0] == '/' || scriptDir == nullptr || scriptDir[0] != '/') {   // if full path, not search next path
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
            ret = this->loadImpl(dir.c_str(), path, filePtr, option);
        }
        if(isFileNotFound(ret)) {
            ret = this->loadImpl(SYSTEM_MOD_DIR, path, filePtr, option);
        }
    }
    return ret;
}

ModType & ModuleLoader::createModType(TypePool &pool, const ModuleScope &scope, const std::string &fullpath) {
    std::unordered_map<std::string, FieldHandle> handles;
    assert(scope.getModID() > 0);
    for(auto &e : scope.global().getHandleMap()) {
        if(e.second.getModID() == scope.getModID()) {
            handles.emplace(e.first, e.second);
        }
    }
    auto children = FlexBuffer<ChildModEntry>(scope.getChilds().begin(), scope.getChilds().end());

    auto &modType = pool.createModType(scope.getModID(), std::move(handles), std::move(children), this->gvarCount);
    this->gvarCount++;  // reserve module object entry
    auto iter = this->indexMap.find(fullpath);
    assert(iter != this->indexMap.end());
    assert(!iter->second);
    iter->second.setModType(modType);
    return modType;
}

const ModType &ModuleLoader::createModType(TypePool &pool, const NameScope &scope, const std::string &fullpath) {
    auto &modType = scope.toModType(pool);
    this->gvarCount++;  // reserve module object entry
    auto iter = this->indexMap.find(fullpath);
    assert(iter != this->indexMap.end());
    assert(!iter->second);
    iter->second.setModType(modType);
    return modType;
}


// #########################
// ##     SymbolTable     ##
// #########################

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

HandleOrError SymbolTable::addAlias(const std::string &symbolName, const FieldHandle &handle) {
    if(this->cur().inGlobalScope() && &this->cur() != &this->root()) {
        auto ret = this->root().lookupHandle(symbolName);
        if(ret && hasFlag(ret->attr(), FieldAttribute::BUILTIN)) {
            return Err(SymbolError::DEFINED);
        }
    }
    return this->cur().addAlias(symbolName, handle);
}

HandleOrError SymbolTable::addTypeAlias(const TypePool &pool, const std::string &alias, const DSType &type) {
    if(this->cur().inGlobalScope()) {
        auto ret = pool.getType(alias);
        if(ret) {
            return Err(SymbolError::DEFINED);
        }
    }
    return this->addAlias(toTypeAliasFullName(alias), FieldHandle(0, type, 0, FieldAttribute{},0));
}

const FieldHandle *SymbolTable::lookupField(const DSType &recvType, const std::string &fieldName) const {
    auto *handle = recvType.lookupField(fieldName);
    if(handle) {
        if(handle->getModID() == 0) {
            return handle;
        }
        if(this->currentModID() != handle->getModID()) {
            StringRef ref = fieldName;
            if(ref[0] == '_') {
                return nullptr;
            }
        }
    }
    return handle;
}

} // namespace ydsh