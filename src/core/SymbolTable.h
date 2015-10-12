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

#ifndef YDSH_SYMBOLTABLE_H
#define YDSH_SYMBOLTABLE_H

#include "TypePool.h"
#include "DSType.h"
#include "FieldHandle.h"

namespace ydsh {
namespace core {

class Scope {
private:
    unsigned int curVarIndex;
    std::unordered_map<std::string, FieldHandle *> handleMap;

public:
    /**
     * equivalent to Scope(0)
     */
    Scope() : Scope(0) { }

    Scope(unsigned int curVarIndex) :
            curVarIndex(curVarIndex), handleMap() { }

    ~Scope();

    /**
     * return null, if not exist.
     */
    FieldHandle *lookupHandle(const std::string &symbolName);

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
    SymbolTable();

    ~SymbolTable();

    /**
     * return null, if not found.
     */
    FieldHandle *lookupHandle(const std::string &symbolName);

    /**
     * return null, if found duplicated handle.
     */
    FieldHandle *registerHandle(const std::string &symbolName, DSType *type, bool readOnly);

    /**
     * return null, if found duplicated handle.
     */
    FunctionHandle *registerFuncHandle(const std::string &funcName, DSType *returnType,
                                       const std::vector<DSType *> &paramTypes);

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
    unsigned int getMaxVarIndex();

    /**
     * max number of global variable index.
     */
    unsigned int getMaxGVarIndex();

    bool inGlobalScope();
};

} // namespace core
} // namespace ydsh

#endif //YDSH_SYMBOLTABLE_H
