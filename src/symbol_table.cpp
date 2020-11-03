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
    if(this->getCurVarIndex() > UINT8_MAX) {
        return Err(SymbolError::LIMIT);
    }
    return ret;
}

// #########################
// ##     GlobalScope     ##
// #########################

GlobalScope::GlobalScope(unsigned int &gvarCount) : Scope(GLOBAL, nullptr), gvarCount(gvarCount) {
    for(auto &e : DENIED_REDEFINED_CMD_LIST) {
        std::string name = toCmdFullName(e);
        this->add(name);
    }
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

const char* ModuleScope::import(const ModType &type, bool global) {
    if(!this->needGlobalImport(toChildModEntry(type, global))) {
        return nullptr;
    }

    for(auto &e : type.handleMap) {
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
            }
            return name.data();
        }
    }
    return nullptr;
}

void ModuleScope::clear() {
    this->maxVarIndexStack.clear();
    this->maxVarIndexStack.push_back(0);
    this->resetScope();
}

// #####################
// ##     ModType     ##
// #####################

ModType::~ModType() {
    free(this->childs);
}

ModType::ModType(unsigned int id, DSType &superType, unsigned short modID,
                 const std::unordered_map<std::string, FieldHandle> &handleMap,
                 const FlexBuffer<ChildModEntry> &childs, unsigned int index) :
        DSType(id, toModName(modID), &superType, TypeAttr::MODULE_TYPE), modID(modID), index(index) {
    assert(modID > 0);
    for(auto &e : handleMap) {
        if(e.second.getModID() == modID) {
            this->handleMap.emplace(e.first, e.second);
        }
    }
    this->childSize = childs.size();
    this->childs = FlexBuffer<ChildModEntry>(childs.begin(), childs.end()).take();
}

const FieldHandle* ModType::lookupFieldHandle(const SymbolTable &symbolTable, const std::string &fieldName) const {
    auto *handle = this->find(fieldName);
    if(!handle) {
        return nullptr;
    }

    if(symbolTable.currentModID() != handle->getModID()) {
        StringRef ref = fieldName;
        if(ref[0] == '_') {
            return nullptr;
        }
    }
    return handle;
}

std::string ModType::toModName(unsigned short id) {
    std::string str = MOD_SYMBOL_PREFIX;
    str += std::to_string(id);
    return str;
}

// ##########################
// ##     ModuleLoader     ##
// ##########################

void ModuleLoader::discard(unsigned int discardPoint) {
    if(discardPoint >= this->modSize()) {
        return; // do nothing
    }

    for(auto iter = this->indexMap.begin(); iter != this->indexMap.end(); ) {
        if(iter->second.getIndex() >= discardPoint) {
            const char *ptr = iter->first.data();
            iter = this->indexMap.erase(iter);
            free(const_cast<char*>(ptr));
        } else {
            ++iter;
        }
    }
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

ModResult ModuleLoader::load(const char *scriptDir, const char *modPath,
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


// #########################
// ##     SymbolTable     ##
// #########################

static bool isFileNotFound(const ModResult &ret) {
    return is<ModLoadingError>(ret) && get<ModLoadingError>(ret).isFileNotFound();
}

ModResult SymbolTable::tryToLoadModule(const char *scriptDir, const char *path,
        FilePtr &filePtr, ModLoadOption option) {
    auto ret = this->modLoader.load(scriptDir, path, filePtr, option);
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
            ret = this->modLoader.load(dir.c_str(), path, filePtr, option);
        }
        if(isFileNotFound(ret)) {
            ret = this->modLoader.load(SYSTEM_MOD_DIR, path, filePtr, option);
        }
    }
    return ret;
}

ModType& SymbolTable::createModType(TypePool &typePool, const std::string &fullpath) {
    auto &modType = typePool.newType<ModType>(
            typePool.get(TYPE::Any), this->cur().getModID(),
            this->cur().global().getHandleMap(), this->cur().getChilds(), this->gvarCount);
    this->gvarCount++;
    this->curModule = nullptr;
    this->modLoader.addModType(fullpath, modType);
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

HandleOrError SymbolTable::addGlobalAlias(const std::string &symbolName, const FieldHandle &handle) {
    assert(this->cur().inGlobalScope());
    if(&this->cur() != &this->root()) {
        auto ret = this->root().lookupHandle(symbolName);
        if(ret && hasFlag(ret->attr(), FieldAttribute::BUILTIN)) {
            return Err(SymbolError::DEFINED);
        }
    }
    return this->cur().addGlobalAlias(symbolName, handle);
}

unsigned int SymbolTable::getTermHookIndex() {
    if(this->termHookIndex == 0) {
        auto *handle = this->lookupHandle(VAR_TERM_HOOK);
        assert(handle != nullptr);
        this->termHookIndex = handle->getIndex();
    }
    return this->termHookIndex;
}

const FieldHandle *SymbolTable::lookupField(DSType &recvType, const std::string &fieldName) const {
    return recvType.lookupFieldHandle(*this, fieldName);    //FIXME:
}

/**
 *
 * @param symbolTable
 * @param modType
 * @param fullname
 * must be end with CMD_SYMBOL_SUFFIX
 * @return
 */
static const FieldHandle *lookupUdcFromModule(const TypePool &typePool,
                                              const ModType &modType, const std::string &fullname) {
    // search own udc
    auto *handle = modType.find(fullname);
    if(handle) {
        return handle;
    }

    // search public udc from globally loaded module
    if(fullname[0] == '_') {
        return nullptr;
    }
    unsigned int size = modType.getChildSize();
    for(unsigned int i = 0; i < size; i++) {
        auto e = modType.getChildAt(i);
        if(isGlobal(e)) {
            auto &type = typePool.get(toTypeId(e));
            assert(type.isModType());
            handle = static_cast<const ModType&>(type).find(fullname);
            if(handle) {
                return handle;
            }
        }
    }
    return nullptr;
}

const FieldHandle * SymbolTable::lookupUdc(const TypePool &pool, const ModType *belongModType, const char *cmdName) const {
    std::string name = toCmdFullName(cmdName);
    if(belongModType == nullptr) {
        return this->lookupHandle(name);
    } else {
        return lookupUdcFromModule(pool, *belongModType, name);
    }
}

} // namespace ydsh