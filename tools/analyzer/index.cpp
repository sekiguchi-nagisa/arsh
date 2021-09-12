/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#include "index.h"

namespace ydsh::lsp {

// ##################
// ##     Refs     ##
// ##################

void Refs::addRef(SymbolRef ref) {
  auto iter = std::lower_bound(this->refs.begin(), this->refs.end(), ref);
  if (iter != this->refs.end()) {
    if (iter->getModId() == ref.getModId() && iter->getPos() == ref.getPos()) {
      *iter = ref; // update
    } else {
      this->refs.insert(iter, ref);
    }
  } else {
    this->refs.push_back(ref);
  }
}

// #########################
// ##     SymbolIndex     ##
// #########################

const DeclSymbol *SymbolIndex::findDecl(unsigned int pos) const {
  auto iter = std::lower_bound(this->decls.begin(), this->decls.end(), pos, DeclSymbol::Compare());
  if (iter != this->decls.end()) {
    auto &decl = *iter;
    if (pos >= decl.getToken().pos && pos <= decl.getToken().endPos()) {
      return &decl;
    }
  }
  return nullptr;
}

const Symbol *SymbolIndex::findSymbol(unsigned int pos) const {
  auto iter = std::lower_bound(this->symbols.begin(), this->symbols.end(), pos, Symbol::Compare());
  if (iter != this->symbols.end()) {
    auto &symbol = *iter;
    if (pos >= symbol.getToken().pos && pos <= symbol.getToken().endPos()) {
      return &symbol;
    }
  }
  return nullptr;
}

const ForeignDecl *SymbolIndex::findForeignDecl(SymbolRequest request) const {
  auto iter = std::lower_bound(this->foreignDecls.begin(), this->foreignDecls.end(), request,
                               ForeignDecl::Compare());
  if (iter != this->foreignDecls.end()) {
    auto &decl = *iter;
    if (request.modId != decl.getDeclModId()) {
      return nullptr;
    }
    if (request.pos >= decl.getToken().pos && request.pos <= decl.getToken().endPos()) {
      return &decl;
    }
  }
  return nullptr;
}

// ###########################
// ##     SymbolIndexes     ##
// ###########################

void SymbolIndexes::add(SymbolIndex &&index) {
  auto iter = std::lower_bound(
      this->indexes.begin(), this->indexes.end(), index,
      [](const SymbolIndex &x, const SymbolIndex &y) { return x.getModId() < y.getModId(); });
  if (iter != this->indexes.end()) {
    if (iter->getModId() == index.getModId()) { // update
      *iter = std::move(index);
    } else {
      this->indexes.insert(iter, std::move(index));
    }
  } else {
    this->indexes.push_back(std::move(index));
  }
}

const SymbolIndex *SymbolIndexes::find(unsigned short modId) const {
  auto iter =
      std::lower_bound(this->indexes.begin(), this->indexes.end(), modId, SymbolIndex::Compare());
  if (iter != this->indexes.end()) {
    if (auto &ret = *iter; ret.getModId() == modId) {
      return &ret;
    }
  }
  return nullptr;
}

void SymbolIndexes::remove(unsigned short id) {
  auto iter =
      std::lower_bound(this->indexes.begin(), this->indexes.end(), id, SymbolIndex::Compare());
  if (iter != this->indexes.end()) {
    this->indexes.erase(iter);
  }
}

bool findDeclaration(const SymbolIndexes &indexes, SymbolRequest request,
                     const std::function<void(const FindDeclResult &)> &consumer) {
  if (auto *index = indexes.find(request.modId); index) {
    if (auto *symbol = index->findSymbol(request.pos); symbol) {
      auto *decl = indexes.findDecl({.modId = symbol->getDeclModId(), .pos = symbol->getDeclPos()});
      if (!decl) {
        return false;
      }
      if (consumer) {
        FindDeclResult ret = {
            .declModId = symbol->getDeclModId(),
            .decl = *decl,
            .request = *symbol,
        };
        consumer(ret);
      }
      return true;
    }
  }
  return false;
}

bool findAllReferences(const SymbolIndexes &indexes, SymbolRequest request,
                       const std::function<void(const FindRefsResult &)> &cosumer) {
  unsigned int count = 0;
  if (auto *decl = indexes.findDecl(request); decl) {
    // search local ref
    for (auto &e : decl->getRefs()) {
      count++;
      if (cosumer) {
        FindRefsResult ret = {
            .symbol = e,
            .request = *decl,
        };
        cosumer(ret);
      }
    }

    // search foreign ref
    for (auto &index : indexes) {
      if (index.getModId() == request.modId) {
        continue;
      }
      if (auto *foreign = index.findForeignDecl(request); foreign) {
        for (auto &e : foreign->getRefs()) {
          count++;
          if (cosumer) {
            FindRefsResult ret = {
                .symbol = e,
                .request = *decl,
            };
            cosumer(ret);
          }
        }
      }
    }
  }
  return count > 0;
}

} // namespace ydsh::lsp