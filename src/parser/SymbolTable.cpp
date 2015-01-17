/*
 * SymbolTable.cpp
 *
 *  Created on: 2015/01/10
 *      Author: skgchxngsxyz-osx
 */

#include <assert.h>

#include "SymbolTable.h"

// #########################
// ##     SymbolEntry     ##
// #########################

SymbolEntry::SymbolEntry(int varIndex) :
        varIndex(varIndex){
}

SymbolEntry::~SymbolEntry() {
}

int SymbolEntry::getVarIndex() {
    return this->varIndex;
}

// ###############################
// ##     CommonSymbolEntry     ##
// ###############################

CommonSymbolEntry::CommonSymbolEntry(int varIndex, DSType *type, bool readOnly, bool global) :
        SymbolEntry(varIndex), flag(0), type(type) {
    if(readOnly) {
        this->flag |= CommonSymbolEntry::READ_ONLY;
    }
    if(global) {
        this->flag |= CommonSymbolEntry::GLOBAL;
    }
}

CommonSymbolEntry::~CommonSymbolEntry() {
}

DSType *CommonSymbolEntry::getType() {
    return this->type;
}

bool CommonSymbolEntry::isReadOnly() {
    return (this->flag & CommonSymbolEntry::READ_ONLY) == CommonSymbolEntry::READ_ONLY;
}

bool CommonSymbolEntry::isGlobal() {
    return (this->flag & CommonSymbolEntry::GLOBAL) == CommonSymbolEntry::GLOBAL;
}

// #############################
// ##     FuncSymbolEntry     ##
// #############################

FuncSymbolEntry::FuncSymbolEntry(int varIndex, FunctionHandle *handle) :
        SymbolEntry(varIndex), handle(handle) {
}

DSType *FuncSymbolEntry::getType() {
    return this->handle->getFuncType();
}

bool FuncSymbolEntry::isReadOnly() {
    return true;
}

bool FuncSymbolEntry::isGlobal() {
    return true;
}

FunctionHandle *FuncSymbolEntry::getHandle() {
    return this->handle;
}


// #####################
// ##     ScopeOp     ##
// #####################

ScopeOp::~ScopeOp() {
}

// ###################
// ##     Scope     ##
// ###################

Scope::Scope(int curVarIndex) :
        curVarIndex(curVarIndex), entryMap() {
}

Scope::~Scope() {
    for(const std::pair<std::string, SymbolEntry*> &pair : this->entryMap) {
        if(pair.second != 0) {
            delete pair.second;
        }
    }
}

SymbolEntry *Scope::getEntry(const std::string &entryName) {
    return this->entryMap[entryName];
}

int Scope::getCurVarIndex() {
    return this->curVarIndex;
}


// #########################
// ##     GlobalScope     ##
// #########################

GlobalScope::GlobalScope() :
        Scope(0), entryCache() {
}

GlobalScope::~GlobalScope() {
}

bool GlobalScope::addEntry(const std::string &entryName, DSType *type, bool readOnly) {
    if(this->entryMap[entryName] != 0) {
        return false;
    }
    CommonSymbolEntry *entry = new CommonSymbolEntry(this->curVarIndex++, type, readOnly, true);
    this->entryMap[entryName] = entry;
    this->entryCache.push_back(entryName);
    return true;
}

void GlobalScope::clearEntryCache() {
    this->entryCache.clear();
}

void GlobalScope::removeCachedEntry() {
    for(std::string entryName : this->entryCache) {
        this->entryMap.erase(entryName);
    }
}


// ########################
// ##     LocalScope     ##
// ########################

LocalScope::LocalScope(int localVarBaseIndex) :
        Scope(localVarBaseIndex), localVarBaseIndex(localVarBaseIndex) {
}

LocalScope::~LocalScope() {
}

bool LocalScope::addEntry(const std::string &entryName, DSType *type, bool readOnly) {
    if(this->entryMap[entryName] != 0) {
        return false;
    }
    CommonSymbolEntry *entry = new CommonSymbolEntry(this->curVarIndex++, type, readOnly, false);
    this->entryMap[entryName] = entry;
    return true;
}


// #########################
// ##     SymbolTable     ##
// #########################

SymbolTable::SymbolTable() :
        scopes() {
    scopes.push_back(new GlobalScope());
}

SymbolTable::~SymbolTable() {
    for(Scope *scope : this->scopes) {
        if(scope != 0) {
            delete scope;
        }
    }
}

SymbolEntry *SymbolTable::getEntry(const std::string &entryName) {
    for(int index = this->scopes.size() - 1; index > -1; index--) {
        SymbolEntry *entry = this->scopes[index]->getEntry(entryName);
        if(entry != 0) {
            return entry;
        }
    }
    return 0;
}

bool SymbolTable::addEntry(const std::string &entryName, DSType *type, bool readOnly) {
    return this->scopes.back()->addEntry(entryName, type, readOnly);
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

