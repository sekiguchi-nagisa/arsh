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

#ifndef YDSH_SYMBOL_TABLE_H
#define YDSH_SYMBOL_TABLE_H

#include "type.h"
#include "handle.h"

namespace ydsh {

class Scope {
private:
    unsigned int curVarIndex;
    std::unordered_map<std::string, FieldHandle *> handleMap;

public:
    NON_COPYABLE(Scope);

    /**
     * equivalent to Scope(0)
     */
    Scope() : Scope(0) { }

    explicit Scope(unsigned int curVarIndex) :
            curVarIndex(curVarIndex), handleMap() { }

    ~Scope();

    /**
     * return null, if not exist.
     */
    FieldHandle *lookupHandle(const std::string &symbolName) const;

    /**
     * add FieldHandle. if adding success, increment curVarIndex.
     * return false if found duplicated handle.
     */
    bool addFieldHandle(const std::string &symbolName, FieldHandle *handle);

    unsigned int getCurVarIndex() const {
        return this->curVarIndex;
    }

    /**
     * remove handle from handleMap, and delete it.
     */
    void deleteHandle(const std::string &symbolName);

    using const_iterator = std::unordered_map<std::string, FieldHandle *>::const_iterator;

    const_iterator cbegin() const {
        return this->handleMap.begin();
    }

    const_iterator cend() const {
        return this->handleMap.end();
    }
};

class SymbolTable {
private:
    std::vector<std::string> handleCache;

    /**
     * first scope is always global scope.
     */
    std::vector<Scope *> scopes;

    /**
     * contains max number of variable index.
     */
    std::vector<unsigned int> maxVarIndexStack;

public:
    NON_COPYABLE(SymbolTable);

    SymbolTable();

    ~SymbolTable();

    /**
     * return null, if not found.
     */
    FieldHandle *lookupHandle(const std::string &symbolName) const;

    /**
     * return null, if found duplicated handle.
     */
    FieldHandle *registerHandle(const std::string &symbolName, DSType &type, flag8_set_t attribute);

    /**
     * return null, if found duplicated handle.
     */
    FunctionHandle *registerFuncHandle(const std::string &funcName, DSType &returnType,
                                       const std::vector<DSType *> &paramTypes);

    /**
     * if already registered, return null.
     * type must be any type
     */
    FieldHandle *registerUdc(const std::string &cmdName, DSType &type);

    /**
     * if not found, return null.
     */
    FieldHandle *lookupUdc(const char *cmdName) const;

    /**
     * create new local scope.
     */
    void enterScope();

    /**
     * delete current local scope.
     */
    void exitScope();

    /**
     * create new function scope.
     */
    void enterFunc();

    /**
     * delete current function scope.
     */
    void exitFunc();

    /**
     * clear entry cache.
     */
    void commit();

    /**
     * remove changed state(local scope, global FieldHandle)
     */
    void abort();

    /**
     * max number of local variable index.
     */
    unsigned int getMaxVarIndex() const;

    /**
     * max number of global variable index.
     */
    unsigned int getMaxGVarIndex() const;

    bool inGlobalScope() const;

    /**
     * get const_iterator of global scope.
     */
    Scope::const_iterator cbeginGlobal() const;

    Scope::const_iterator cendGlobal() const;

    static constexpr const char *cmdSymbolPrefix = "%c";
};

} // namespace ydsh

#endif //YDSH_SYMBOL_TABLE_H
