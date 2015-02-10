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

// ###################
// ##     Scope     ##
// ###################

Scope::Scope() : Scope(0) {
}

Scope::Scope(unsigned int curVarIndex) :
        curVarIndex(curVarIndex), handleMap() {
}

Scope::~Scope() {
    for(const std::pair<std::string, FieldHandle*> &pair : this->handleMap) {
        delete pair.second;
    }
    this->handleMap.clear();
}

FieldHandle *Scope::lookupHandle(const std::string &symbolName) {
    auto iter = this->handleMap.find(symbolName);
    return iter != this->handleMap.end() ? iter->second : 0;
}

bool Scope::addFieldHandle(const std::string &symbolName, FieldHandle *handle) {
    if(!this->handleMap.insert(std::make_pair(symbolName, handle)).second) {
        return false;
    }
    this->curVarIndex++;
    return true;
}

unsigned int Scope::getCurVarIndex() {
    return this->curVarIndex;
}

void Scope::deleteHandle(const std::string &symbolName) {
    auto iter = this->handleMap.find(symbolName);
    delete iter->second;
    this->handleMap.erase(symbolName);
}


// #########################
// ##     SymbolTable     ##
// #########################

SymbolTable::SymbolTable() :
        handleCache(), scopes(), maxVarIndexStack() {
    this->scopes.push_back(new Scope());
    this->maxVarIndexStack.push_back(0);
}

SymbolTable::~SymbolTable() {
    for(Scope *scope : this->scopes) {
        delete scope;
    }
    this->scopes.clear();
}

FieldHandle *SymbolTable::lookupHandle(const std::string &symbolName) {
    for(int index = this->scopes.size() - 1; index > -1; index--) {
        FieldHandle *handle = this->scopes[index]->lookupHandle(symbolName);
        if(handle != 0) {
            return handle;
        }
    }
    return 0;
}

FieldHandle *SymbolTable::registerHandle(const std::string &symbolName, DSType *type, bool readOnly) {
    FieldHandle *handle = new FieldHandle(type, this->scopes.back()->getCurVarIndex(), readOnly);
    if(!this->scopes.back()->addFieldHandle(symbolName, handle)) {
        delete handle;
        return 0;
    }
    if(this->inGlobalScope()) {
        handle->setAttribute(FieldHandle::GLOBAL);
        this->handleCache.push_back(symbolName);
    }
    return handle;
}

void SymbolTable::enterScope() {
    unsigned int index = this->scopes.back()->getCurVarIndex();
    if(this->inGlobalScope()) {
        index = 0;
    }
    this->scopes.push_back(new Scope(index));
}

void SymbolTable::exitScope() {
    assert(!this->inGlobalScope());
    Scope *scope = this->scopes.back();
    unsigned int varIndex = scope->getCurVarIndex();
    if(varIndex > this->maxVarIndexStack.back()) {
        this->maxVarIndexStack[this->maxVarIndexStack.size() - 1] = varIndex;
    }

    this->scopes.pop_back();
    delete scope;
}

void SymbolTable::enterFuncScope() {
    this->scopes.push_back(new Scope());
    this->maxVarIndexStack.push_back(0);
}

void SymbolTable::exitFuncScope() {
    assert(!this->inGlobalScope());
    delete this->scopes.back();
    this->scopes.pop_back();
    this->maxVarIndexStack.pop_back();
}

void SymbolTable::popAllLocal() {
    while(!this->inGlobalScope()) {
        delete this->scopes.back();
        this->scopes.pop_back();
    }
    while(this->maxVarIndexStack.size() > 1) {
        this->maxVarIndexStack.pop_back();
    }
}

void SymbolTable::clearEntryCache() {
    assert(this->inGlobalScope());
    this->handleCache.clear();
}

void SymbolTable::removeCachedEntry() {
    assert(this->inGlobalScope());
    for(const std::string &name : this->handleCache) {
        this->scopes.back()->deleteHandle(name);
    }
}

unsigned int SymbolTable::getMaxVarIndex() {
    return this->maxVarIndexStack.back();
}

bool SymbolTable::inGlobalScope() {
    return this->scopes.size() == 1;
}

