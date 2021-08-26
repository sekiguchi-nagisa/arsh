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

struct SymbolComp {
  bool operator()(const Symbol &x, unsigned int y) const { return x.getToken().endPos() < y; }

  bool operator()(unsigned int x, const Symbol &y) const { return x < y.getToken().pos; }
};

const Symbol *SymbolIndex::findDecl(unsigned int pos) const {
  auto iter = std::lower_bound(this->decls.begin(), this->decls.end(), pos, SymbolComp());
  if (iter != this->decls.end()) {
    auto &symbol = *iter;
    if (pos >= symbol.getToken().pos && pos <= symbol.getToken().endPos()) {
      return &symbol;
    }
  }
  return nullptr;
}

struct SymbolRefComp {
  bool operator()(const SymbolRef &x, unsigned int y) const { return x.getToken().endPos() < y; }

  bool operator()(unsigned int x, const SymbolRef &y) const { return x < y.getToken().pos; }
};

const SymbolRef *SymbolIndex::findRef(unsigned int pos) const {
  auto iter = std::lower_bound(this->refs.begin(), this->refs.end(), pos, SymbolRefComp());
  if (iter != this->refs.end()) {
    auto &ref = *iter;
    if (pos >= ref.getToken().pos && pos <= ref.getToken().endPos()) {
      return &ref;
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

struct IndexComp {
  bool operator()(const SymbolIndex &x, unsigned short id) const { return x.getModId() < id; }

  bool operator()(unsigned short id, const SymbolIndex &y) const { return id < y.getModId(); }
};

const SymbolIndex *SymbolIndexes::find(unsigned short modId) const {
  auto iter = std::lower_bound(this->indexes.begin(), this->indexes.end(), modId, IndexComp());
  if (iter != this->indexes.end()) {
    return &*iter;
  }
  return nullptr;
}

void SymbolIndexes::remove(unsigned short id) {
  auto iter = std::lower_bound(this->indexes.begin(), this->indexes.end(), id, IndexComp());
  if (iter != this->indexes.end()) {
    this->indexes.erase(iter);
  }
}

const Symbol *findDeclaration(const SymbolIndexes &indexes, Symbol::RefLoc ref) {
  if (auto *index = indexes.find(ref.modID); index) {
    if (auto *refSymbol = index->findRef(ref.pos); refSymbol) {
      if (auto *decl = indexes.find(refSymbol->getDeclModId()); decl) {
        return decl->findDecl(refSymbol->getDeclPos());
      }
    }
  }
  return nullptr;
}

// bool findAllReferences(const SymbolIndexes &indexes, Symbol::RefLoc decl,
//                        const std::function<void(const SymbolRef &)> &cosumer) {
//   unsigned int count = 0;
//   if (auto *index = indexes.find(decl.modID); index) {
//     if (auto *declSymbol = index->findDecl(decl.pos); declSymbol) {
//       for (auto &e : declSymbol->getRefs()) {
//       }
//     }
//   }
//   return count > 0;
// }

} // namespace ydsh::lsp