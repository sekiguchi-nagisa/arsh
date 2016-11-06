/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#include "symbol_table.h"

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
    this->curVarIndex++;
    return true;
}

void Scope::deleteHandle(const std::string &symbolName) {
    auto iter = this->handleMap.find(symbolName);
    delete iter->second;
    this->handleMap.erase(symbolName);
}


// #########################
// ##     SymbolTable     ##
// #########################

FieldHandle *SymbolTable::lookupHandle(const std::string &symbolName) const {
    for(auto iter = this->scopes.crbegin(); iter != this->scopes.crend(); ++iter) {
        FieldHandle *handle = (*iter)->lookupHandle(symbolName);
        if(handle != nullptr) {
            return handle;
        }
    }
    return nullptr;
}

FieldHandle *SymbolTable::registerHandle(const std::string &symbolName, DSType &type, FieldAttributes attribute) {
    if(this->inGlobalScope()) {
        attribute.set(FieldAttribute::GLOBAL);
    }

    FieldHandle *handle = new FieldHandle(&type, this->scopes.back()->getCurVarIndex(), attribute);
    if(!this->scopes.back()->addFieldHandle(symbolName, handle)) {
        delete handle;
        return nullptr;
    }
    if(this->inGlobalScope()) {
        this->handleCache.push_back(symbolName);
    }
    return handle;
}

FunctionHandle *SymbolTable::registerFuncHandle(const std::string &funcName, DSType &returnType,
                                                const std::vector<DSType *> &paramTypes) {
    assert(this->inGlobalScope());
    FunctionHandle *handle = new FunctionHandle(&returnType, paramTypes, this->scopes.back()->getCurVarIndex());
    if(!this->scopes.back()->addFieldHandle(funcName, handle)) {
        delete handle;
        return nullptr;
    }
    this->handleCache.push_back(funcName);
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
        this->maxVarIndexStack.back() = varIndex;
    }

    this->scopes.pop_back();
    delete scope;
}

void SymbolTable::enterFunc() {
    this->scopes.push_back(new Scope());
    this->maxVarIndexStack.push_back(0);
}

void SymbolTable::exitFunc() {
    assert(!this->inGlobalScope());
    delete this->scopes.back();
    this->scopes.pop_back();
    this->maxVarIndexStack.pop_back();
}

void SymbolTable::commit() {
    assert(this->inGlobalScope());
    this->handleCache.clear();
}

void SymbolTable::abort() {
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

} // namespace ydsh