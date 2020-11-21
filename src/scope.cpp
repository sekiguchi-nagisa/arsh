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

#include <algorithm>

#include "scope.h"

namespace ydsh {

// #######################
// ##     NameScope     ##
// #######################

static bool isAllowedScopePair(NameScope::Kind parent, NameScope::Kind child) {
    const struct {
        NameScope::Kind parent;
        NameScope::Kind child;
    } table[] = {
            {NameScope::GLOBAL, NameScope::FUNC},
            {NameScope::GLOBAL, NameScope::BLOCK},
            {NameScope::FUNC, NameScope::BLOCK},
            {NameScope::BLOCK, NameScope::BLOCK},
    };
    for(auto &e : table) {
        if(e.parent == parent && e.child == child) {
            return true;
        }
    }
    return false;
}

IntrusivePtr<NameScope> NameScope::enterScope(Kind newKind) {
    if(isAllowedScopePair(this->kind, newKind)) {
        if(this->kind == NameScope::GLOBAL && newKind == NameScope::FUNC) {
            return IntrusivePtr<NameScope>::create(newKind, this->fromThis(), this->maxVarCount);
        } else if(this->kind == NameScope::GLOBAL && newKind == NameScope::BLOCK) {
            return IntrusivePtr<NameScope>::create(newKind, this->fromThis(), std::ref(this->curLocalIndex));
        } else if(this->kind == NameScope::FUNC && newKind == NameScope::BLOCK) {
            return IntrusivePtr<NameScope>::create(newKind, this->fromThis(), std::ref(this->curLocalIndex));
        } else if(this->kind == NameScope::BLOCK && newKind == NameScope::BLOCK) {
            auto scope = IntrusivePtr<NameScope>::create(newKind, this->fromThis(), this->maxVarCount);
            scope->curLocalIndex = this->curLocalIndex;
            return scope;
        }
    }
    return nullptr;
}

static bool definedInBuiltin(const NameScope &scope, const std::string &name) {
    if(scope.isGlobal() && scope.parent) {
        if(scope.parent->isBuiltinModule() && scope.parent->contains(name)) {
            assert(scope.parent->isGlobal());
            return true;
        }
    }
    return false;
}

NameLookupResult NameScope::defineHandle(std::string &&name, const DSType &type, FieldAttribute attr) {
    if(definedInBuiltin(*this, name)) {
        return Err(NameLookupError::DEFINED);
    }
    return this->addNewHandle(std::move(name), type, attr);
}

NameLookupResult NameScope::defineAlias(std::string &&name, const FieldHandle &handle) {
    if(definedInBuiltin(*this, name)) {
        return Err(NameLookupError::DEFINED);
    }
    return this->addNewAlias(std::move(name), handle);
}

NameLookupResult NameScope::defineTypeAlias(const TypePool &pool, std::string &&name, const DSType &type) {
    if(this->isGlobal()) {
        auto ret = pool.getType(name);
        if(ret) {
            return Err(NameLookupError::DEFINED);
        }
    }
    return this->defineAlias(toTypeAliasFullName(name), FieldHandle(0, type, 0, FieldAttribute{}, 0));
}

static bool needGlobalImport(FlexBuffer<ChildModEntry> &entries, ChildModEntry entry) {
    auto iter = std::lower_bound(entries.begin(), entries.end(), entry,
                                 [](ChildModEntry x, ChildModEntry y) {
                                     return toTypeId(x) < toTypeId(y);
                                 });
    if(iter != entries.end() && toTypeId(*iter) == toTypeId(entry)) {
        if(isGlobal(*iter)) {
            return false;
        }
        if(isGlobal(entry)) {
            *iter = entry;
        }
    } else {
        entries.insert(iter, entry);
    }
    return isGlobal(entry);
}

std::string NameScope::importForeignHandles(const ModType &type, bool global) {
    if(!needGlobalImport(this->children, toChildModEntry(type, global))) {
        return "";
    }

    for(auto &e : type.getHandleMap()) {
        assert(!hasFlag(e.second.attr(), FieldAttribute::BUILTIN));
        assert(this->modId != e.second.getModID());
        StringRef ref = e.first;
        if(ref.startsWith("_")) {
            continue;
        }
        std::string symbolName = e.first;
        const auto &handle = e.second;
        auto ret = this->addNewForeignHandle(std::move(symbolName), handle);
        if(!ret) {
            StringRef name = symbolName;
            if(isCmdFullName(name)) {
                name.removeSuffix(strlen(CMD_SYMBOL_SUFFIX));
            } else if(isTypeAliasFullName(name)) {
                name.removeSuffix(strlen(TYPE_ALIAS_SYMBOL_SUFFIX));
            }
            return name.toString();
        }
    }
    return "";
}

const FieldHandle * NameScope::lookup(const std::string &name) const {
    for(auto *scope = this; scope != nullptr; scope = scope->parent.get()) {
        auto *handle = scope->find(name);
        if(handle) {
            return handle;
        }
    }
    return nullptr;
}

void NameScope::discard(ScopeDiscardPoint discardPoint) {
    for(auto iter = this->handles.begin(); iter != this->handles.end();) {
        if(iter->second.getCommitID() >= discardPoint.commitIdOffset) {
            iter = this->handles.erase(iter);
        } else {
            ++iter;
        }
    }
    this->children.clear();
}

NameLookupResult NameScope::add(std::string &&name, FieldHandle &&handle) {
    assert(handle.getModID() == this->modId);
    assert(this->kind != FUNC);

    const auto attr = handle.attr();

    // check var index limit
    if(!hasFlag(attr, FieldAttribute::ALIAS)) {
        if(!hasFlag(attr, FieldAttribute::GLOBAL)) {
            assert(!this->isGlobal());
            if(this->curLocalIndex == UINT8_MAX) {
                return Err(NameLookupError::LIMIT);
            }
        }
    }

    auto pair = this->handles.emplace(std::move(name), std::move(handle));
    if(!pair.second) {
        return Err(NameLookupError::DEFINED);
    }

    // increment var index count
    if(!hasFlag(attr, FieldAttribute::ALIAS)) {
        assert(this->isGlobal() == hasFlag(attr, FieldAttribute::GLOBAL));
        if(hasFlag(attr, FieldAttribute::GLOBAL)) {
            this->maxVarCount.get()++;
        } else {    // local
            assert(this->kind == BLOCK);
            this->curLocalIndex++;
            this->localSize++;

            if(this->curLocalIndex > this->maxVarCount.get()) {
                this->maxVarCount.get() = this->curLocalIndex;
            }
        }
    }
    return Ok(&pair.first->second);
}

} // namespace ydsh