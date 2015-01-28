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

#ifndef CORE_SYMBOLTABLE_H_
#define CORE_SYMBOLTABLE_H_

#include <core/TypePool.h>
#include <core/DSType.h>
#include <core/FieldHandle.h>

class ScopeOp {
public:
    virtual ~ScopeOp();

    /**
     * get handle from scope.
     * return null, if not exist
     */
    virtual FieldHandle *getHandle(const std::string &entryName) = 0;

    /**
     * add new handle.
     * type must not be void type, parametric type.
     * return false, if found duplicated entry
     */
    virtual bool addHandle(const std::string &entryName, DSType *type, bool readOnly) = 0;
};

class Scope : public ScopeOp {
protected:
    int curVarIndex;
    std::unordered_map<std::string, FieldHandle*> handleMap;

public:
    Scope(int curVarIndex);
    virtual ~Scope();

    FieldHandle *getHandle(const std::string &entryName);    // override
    int getCurVarIndex();
};

class SymbolTable : public ScopeOp {
private:
    /**
     * first element is always global scope
     */
    std::vector<Scope*> scopes;

public:
    SymbolTable();
    ~SymbolTable();

    FieldHandle *getHandle(const std::string &entryName);    // override
    bool addHandle(const std::string &entryName, DSType *type, bool readOnly);   // override

    /**
     * create new local scope
     */
    void enterScope();

    /**
     * delete current local scope
     */
    void exitScope();

    /**
     * pop all local scope and func scope
     */
    void popAllLocal();

    void clearEntryCache();
    void removeCachedEntry();

    /**
     * max number of local variable index.
     */
    int getMaxVarIndex();

    bool inGlobalScope();
};

#endif /* CORE_SYMBOLTABLE_H_ */
