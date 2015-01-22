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

#ifndef PARSER_SYMBOLTABLE_H_
#define PARSER_SYMBOLTABLE_H_

#include "../core/DSType.h"
#include "../core/CalleeHandle.h"
#include "../core/TypePool.h"

class SymbolEntry {
private:
    int varIndex;

public:
    SymbolEntry(int varIndex);
    virtual ~SymbolEntry();

    /**
     * return the type of symbol(variable, function)
     */
    virtual DSType *getType(TypePool *typePool) = 0;

    /**
     * return true, if read only entry
     */
    virtual bool isReadOnly() = 0;

    /**
     * return true, if global entry
     */
    virtual bool isGlobal() = 0;

    int getVarIndex();
};

class CommonSymbolEntry: public SymbolEntry {
private:
    const static int READ_ONLY = 1 << 0;
    const static int GLOBAL = 1 << 1;

    /**
     * 1 << 0: read only
     * 1 << 1: global
     */
    unsigned char flag;

    DSType *type;

public:
    CommonSymbolEntry(int varIndex, DSType *type, bool readOnly, bool global);
    ~CommonSymbolEntry();

    DSType *getType(TypePool *typePool);	// override
    bool isReadOnly();	// override
    bool isGlobal();	// override
};

class FuncSymbolEntry: public SymbolEntry {
private:
    FunctionHandle *handle;

public:
    FuncSymbolEntry(int varIndex, FunctionHandle *handle);
    ~FuncSymbolEntry();

    DSType *getType(TypePool *typePool);	// override

    /**
     * return always true
     */
    bool isReadOnly();	// override

    /**
     * currently, return always true
     */
    bool isGlobal();	// override

    FunctionHandle *getHandle();
};

class ScopeOp {
public:
    virtual ~ScopeOp();

    /**
     * get entry from scope.
     * return null, if not exist
     */
    virtual SymbolEntry *getEntry(const std::string &entryName) = 0;

    /**
     * add new entry.
     * type must not be void type, parametric type.
     * return false, if found duplicated entry
     */
    virtual bool addEntry(const std::string &entryName, DSType *type, bool readOnly) = 0;
};

class Scope : public ScopeOp {
protected:
    int curVarIndex;
    std::unordered_map<std::string, SymbolEntry*> entryMap;

public:
    Scope(int curVarIndex);
    virtual ~Scope();

    SymbolEntry *getEntry(const std::string &entryName);    // override
    int getCurVarIndex();
};

class GlobalScope : public Scope {
private:
    std::vector<std::string> entryCache;

public:
    GlobalScope();
    ~GlobalScope();

    bool addEntry(const std::string &entryName, DSType *type, bool readOnly);   // override
    void clearEntryCache();
    void removeCachedEntry();
};

class LocalScope : public Scope {
private:
    int localVarBaseIndex;

public:
    LocalScope(int localVarBaseIndex);
    ~LocalScope();

    bool addEntry(const std::string &entryName, DSType *type, bool readOnly);   // override
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

    SymbolEntry *getEntry(const std::string &entryName);    // override
    bool addEntry(const std::string &entryName, DSType *type, bool readOnly);   // override

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

#endif /* PARSER_SYMBOLTABLE_H_ */
