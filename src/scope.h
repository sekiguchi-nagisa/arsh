/*
 * Copyright (C) 2020 Nagisa Sekiguchi
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

#ifndef YDSH_SCOPE_H
#define YDSH_SCOPE_H

#include <cassert>
#include <functional>

#include "symbol_table.h"
#include "type_pool.h"
#include "misc/resource.hpp"

namespace ydsh {

enum class NameLookupError {
    DEFINED,
    LIMIT,
};

using NameLookupResult = Result<const FieldHandle *, NameLookupError>;

struct ScopeDiscardPoint {
    unsigned int commitIdOffset;
};

class NameScope : public RefCount<NameScope> {
public:
    const enum Kind : unsigned char {
        GLOBAL,
        FUNC,
        BLOCK,
    } kind;

    /**
     * indicates belonged module id
     */
    const unsigned short modId;

    /**
     * may be null
     */
    const IntrusivePtr<NameScope> parent;

private:
    unsigned int curLocalIndex{0};

    /**
     * indicate number of local variables defined in this scope
     */
    unsigned int localSize{0};

    const std::reference_wrapper<unsigned int> maxVarCount;

    std::unordered_map<std::string, FieldHandle> handles;

    FlexBuffer<ChildModEntry> children;

public:
    /**
     * for builtin module scope construction
     * @param gvarCount
     */
    explicit NameScope(std::reference_wrapper<unsigned int> gvarCount) :
            kind(GLOBAL), modId(0), maxVarCount(gvarCount) {}

    /**
     * for module scope construction.
     * normally called from ModuleLoader
     * @param parent
     * @param modId
     */
    NameScope(IntrusivePtr<NameScope> parent, unsigned short modId) :
            kind(GLOBAL), modId(modId), parent(parent), maxVarCount(parent->maxVarCount) {
        assert(this->parent->isGlobal());
    }

    /**
     * for func/block scope construction
     * only called from enterScope()
     * @param kind
     * @param parent
     * @param varCount
     */
    NameScope(Kind kind, IntrusivePtr<NameScope> parent, std::reference_wrapper<unsigned int> varCount) :
            kind(kind), modId(parent->modId), parent(parent), maxVarCount(varCount) {}

    bool isGlobal() const {
        return this->kind == GLOBAL;
    }

    bool isBuiltinModule() const {
        return this->modId == 0;
    }

    bool isRootModule() const {
        return this->modId == 1;
    }

    unsigned int getCurLocalIndex() const {
        return this->curLocalIndex;
    }

    unsigned int getLocalSize() const {
        return this->localSize;
    }

    unsigned int getBaseIndex() const {
        return this->getCurLocalIndex() - this->getLocalSize();
    }

    unsigned int getMaxGlobalVarIndex() const {
        assert(this->isGlobal());
        return this->maxVarCount.get();
    }

    unsigned int getMaxLocalVarIndex() const {
        return this->kind == BLOCK ? this->maxVarCount.get() : this->curLocalIndex;
    }

    const std::unordered_map<std::string, FieldHandle> &getHandles() const {
        return this->handles;
    }

    auto begin() const {
        return this->handles.begin();
    }

    auto end() const {
        return this->handles.end();
    }

    const FieldHandle *find(const std::string &name) const {
        auto iter = this->handles.find(name);
        if(iter != this->handles.end()) {
            return &iter->second;
        }
        return nullptr;
    }

    bool contains(const std::string &name) const {
        return this->find(name) != nullptr;
    }

    // for scope construction
    /**
     * create new scope
     * @param kind
     * must not be GLOBAL
     * @return
     * if illegal kind, (ex. BLOCK->GLOBAL, GLOBAL->GLOBAL) return null
     */
    IntrusivePtr<NameScope> enterScope(Kind kind);

    /**
     *
     * @return
     * return parent
     */
    IntrusivePtr<NameScope> exitScope() {
        return this->parent;
    }

    void clearLocalSize() {
        assert(this->isGlobal());
        this->curLocalIndex = 0;
        this->localSize = 0;
    }

    // for name registration
    NameLookupResult defineHandle(std::string &&name, const DSType &type, FieldAttribute attr);

    NameLookupResult defineAlias(std::string &&name, const FieldHandle &handle);

    NameLookupResult defineTypeAlias(const TypePool &pool, std::string &&name, const DSType &type);

    /**
     * import handle from foreign module (ModType)
     * @param type
     * @param global
     * @return
     * if found name conflict, return conflicted name
     */
    std::string importForeignHandles(const ModType &type, bool global);

    const ModType &toModType(TypePool &pool) const;

    // for name lookup
    /**
     * lookup handle
     * @param name
     * @return
     */
    const FieldHandle *lookup(const std::string &name) const;

    const FieldHandle *lookupField(const DSType &recv, const std::string &fieldName) const {
        auto *handle = recv.lookupField(fieldName);
        if(handle) {
            if(handle->getModID() == 0) {
                return handle;
            }
            if(this->modId != handle->getModID()) {
                StringRef ref = fieldName;
                if(ref[0] == '_') {
                    return nullptr;
                }
            }
        }
        return handle;
    }

    // for symbol discard
    ScopeDiscardPoint getDiscardPoint() const {
        return ScopeDiscardPoint {
            .commitIdOffset = static_cast<unsigned int>(this->handles.size()),
        };
    }

    void discard(ScopeDiscardPoint discardPoint);   //FIXME: discard more var index

private:
    unsigned int commitId() const {
        return this->handles.size();
    }

    IntrusivePtr<NameScope> fromThis() {
        return IntrusivePtr<NameScope>(this);
    }

    /**
     * just add newly created handle.
     * only called from addNew* api
     * @param name
     * @param handle
     * @return
     */
    NameLookupResult add(std::string &&name, FieldHandle &&handle);

    /**
     * define local/global variable name
     * @param name
     * @param type
     * @param attr
     * @return
     */
    NameLookupResult addNewHandle(std::string &&name, const DSType &type, FieldAttribute attr) {
        if(this->isGlobal()) {
            setFlag(attr, FieldAttribute::GLOBAL);
        } else {
            unsetFlag(attr, FieldAttribute::GLOBAL);
        }
        unsigned int index = this->isGlobal() ? this->getMaxGlobalVarIndex() : this->getCurLocalIndex();
        return this->add(std::move(name), FieldHandle(this->commitId(), type, index, attr, this->modId));
    }

    /**
     * for alias definition
     * @param name
     * @param handle
     * @return
     */
    NameLookupResult addNewAlias(std::string &&name, const FieldHandle &handle) {
        auto newAttr = handle.attr();
        setFlag(newAttr, FieldAttribute::ALIAS);
        return this->add(std::move(name), FieldHandle(this->commitId(), handle, newAttr, this->modId));
    }

    NameLookupResult addNewForeignHandle(std::string &&name, const FieldHandle &handle) {
        auto newAttr = handle.attr();
        setFlag(newAttr, FieldAttribute::ALIAS);
        return this->add(std::move(name), FieldHandle(this->commitId(), handle, newAttr, handle.getModID()));
    }
};

} // namespace ydsh

#endif //YDSH_SCOPE_H
