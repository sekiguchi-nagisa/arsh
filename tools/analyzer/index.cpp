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
    return &*iter;
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

const DeclSymbol *findDeclaration(const SymbolIndexes &indexes, SymbolRef ref) {
  if (auto *index = indexes.find(ref.getModId()); index) {
    if (auto *symbol = index->findSymbol(ref.getPos()); symbol) {
      return indexes.findDecl(symbol->getDeclModId(), symbol->getDeclPos());
    }
  }
  return nullptr;
}

bool findAllReferences(const SymbolIndexes &indexes, SymbolRef ref,
                       const std::function<void(const SymbolRef &)> &cosumer) {
  unsigned int count = 0;
  if (auto *decl = indexes.findDecl(ref.getModId(), ref.getPos()); decl) {
    for (auto &e : decl->getRefs()) {
      count++;
      if (cosumer) {
        cosumer(e);
      }
    }
  }
  return count > 0;
}

} // namespace ydsh::lsp