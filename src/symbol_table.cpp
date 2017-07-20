/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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


// #########################
// ##     SymbolTable     ##
// #########################

SymbolError SymbolTable::tryToRegister(const std::string &name, FieldHandle *handle) {
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

FieldHandle *SymbolTable::lookupHandle(const std::string &symbolName) const {
    for(auto iter = this->scopes.crbegin(); iter != this->scopes.crend(); ++iter) {
        FieldHandle *handle = (*iter)->lookupHandle(symbolName);
        if(handle != nullptr) {
            return handle;
        }
    }
    return nullptr;
}

std::pair<FieldHandle *, SymbolError> SymbolTable::registerHandle(const std::string &symbolName,
                                                                DSType &type, FieldAttributes attribute) {
    if(this->inGlobalScope()) {
        attribute.set(FieldAttribute::GLOBAL);
    }

    auto *handle = new FieldHandle(&type, this->scopes.back()->getCurVarIndex(), attribute);
    auto e = this->tryToRegister(symbolName, handle);
    if(e != SymbolError::DUMMY) {
        return std::make_pair(nullptr, e);
    }
    if(this->inGlobalScope()) {
        this->handleCache.push_back(symbolName);
    }
    return std::make_pair(handle, SymbolError::DUMMY);
}

std::pair<FieldHandle *, SymbolError> SymbolTable::registerFuncHandle(const std::string &funcName, DSType &returnType,
                                                                    const std::vector<DSType *> &paramTypes) {
    assert(this->inGlobalScope());
    FieldHandle *handle = new FunctionHandle(&returnType, paramTypes, this->scopes.back()->getCurVarIndex());
    auto e = this->tryToRegister(funcName, handle);
    if(e != SymbolError::DUMMY) {
        return std::make_pair(nullptr, e);
    }
    this->handleCache.push_back(funcName);
    return std::make_pair(handle, SymbolError::DUMMY);
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
    delete this->scopes.back();
    this->scopes.pop_back();
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
    this->maxVarIndexStack.clear();
    this->maxVarIndexStack.push_back(0);
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