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

#include <assert.h>

#include <core/SymbolTable.h>

// #####################
// ##     ScopeOp     ##
// #####################

ScopeOp::~ScopeOp() {
}


// ###################
// ##     Scope     ##
// ###################

Scope::Scope(int curVarIndex) :
        curVarIndex(curVarIndex), handleMap() {
}

Scope::~Scope() {
    for(const std::pair<std::string, FieldHandle*> &pair : this->handleMap) {
        delete pair.second;
    }
    this->handleMap.clear();
}

FieldHandle *Scope::getHandle(const std::string &entryName) {
    return this->handleMap[entryName];
}

int Scope::getCurVarIndex() {
    return this->curVarIndex;
}


// #########################
// ##     GlobalScope     ##
// #########################

class GlobalScope : public Scope {
private:
    std::vector<std::string> entryCache;

public:
    GlobalScope() :
            Scope(0), entryCache() {
    }

    ~GlobalScope() {
        this->entryCache.clear();
    }

    bool addHandle(const std::string &entryName, DSType *type, bool readOnly) {   // override
        if(this->handleMap[entryName] != 0) {
            return false;
        }
        FieldHandle *handle = new FieldHandle(type, this->curVarIndex++, readOnly);
        handle->setAttribute(FieldHandle::GLOBAL);
        this->handleMap[entryName] = handle;
        this->entryCache.push_back(entryName);
        return true;
    }

    void clearEntryCache() {
        this->entryCache.clear();
    }

    void removeCachedEntry() {
        for(std::string entryName : this->entryCache) {
            this->handleMap.erase(entryName);
        }
    }
};


// ########################
// ##     LocalScope     ##
// ########################

class LocalScope : public Scope {   //FIXME: var index
private:
    int localVarBaseIndex;

public:
    LocalScope(int localVarBaseIndex) :
            Scope(localVarBaseIndex), localVarBaseIndex(localVarBaseIndex) {
    }

    ~LocalScope() {
    }

    bool addHandle(const std::string &entryName, DSType *type, bool readOnly) {   // override
        if(this->handleMap[entryName] != 0) {
            return false;
        }
        FieldHandle *handle = new FieldHandle(type, this->curVarIndex++, readOnly);
        this->handleMap[entryName] = handle;
        return true;
    }
};


// #########################
// ##     SymbolTable     ##
// #########################

SymbolTable::SymbolTable() :
        scopes() {
    scopes.push_back(new GlobalScope());
}

SymbolTable::~SymbolTable() {
    for(Scope *scope : this->scopes) {
        delete scope;
    }
    this->scopes.clear();
}

FieldHandle *SymbolTable::getHandle(const std::string &entryName) {
    for(int index = this->scopes.size() - 1; index > -1; index--) {
        FieldHandle *handle = this->scopes[index]->getHandle(entryName);
        if(handle != 0) {
            return handle;
        }
    }
    return 0;
}

bool SymbolTable::addHandle(const std::string &entryName, DSType *type, bool readOnly) {
    return this->scopes.back()->addHandle(entryName, type, readOnly);
}

void SymbolTable::enterScope() {    //FIXME:
    int index = this->scopes.back()->getCurVarIndex();
    if(this->scopes.size() == 1) {
        index = 0;
    }
    this->scopes.push_back(new LocalScope(index));
}

void SymbolTable::exitScope() { //FIXME:
    assert(this->scopes.size() > 1);
    delete this->scopes.back();
    this->scopes.pop_back();
}

/**
 * pop all local scope and func scope
 */
void SymbolTable::popAllLocal() {
    while(this->scopes.size() > 1) {
        delete this->scopes.back();
        this->scopes.pop_back();
    }
}

void SymbolTable::clearEntryCache() {
    assert(this->scopes.size() == 1);
    dynamic_cast<GlobalScope*>(this->scopes.back())->clearEntryCache();
}

void SymbolTable::removeCachedEntry() {
    assert(this->scopes.size() == 1);
    dynamic_cast<GlobalScope*>(this->scopes.back())->removeCachedEntry();
}

int SymbolTable::getMaxVarIndex() {
    return 0;   // FIXME:
}

bool SymbolTable::inGlobalScope() {
    return this->scopes.size() == 1;
}

