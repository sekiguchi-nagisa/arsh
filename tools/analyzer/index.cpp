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
#include <constant.h>
#include <misc/num_util.hpp>

namespace ydsh::lsp {

// ######################
// ##     DeclBase     ##
// ######################

void DeclBase::addRef(SymbolRef ref) {
  auto iter = std::lower_bound(this->refs.begin(), this->refs.end(), ref);
  if (iter != this->refs.end() && (*iter).getModId() == ref.getModId() &&
      (*iter).getPos() == ref.getPos()) {
    *iter = ref; // update
  } else {
    this->refs.insert(iter, ref);
  }
}

// ########################
// ##     DeclSymbol     ##
// ########################

std::pair<unsigned short, bool> DeclSymbol::getInfoAsModId() const {
  auto ref = this->getInfo();
  auto value = convertToNum<int>(ref.begin(), ref.end());
  if (value.second && value.first <= UINT16_MAX && value.first >= 0) {
    return {static_cast<unsigned short>(value.first), true};
  }
  return {0, false};
}

std::string DeclSymbol::mangle(Kind k, StringRef name) {
  switch (k) {
  case DeclSymbol::Kind::BUILTIN_CMD:
  case DeclSymbol::Kind::CMD:
    return toCmdFullName(name);
  case DeclSymbol::Kind::TYPE_ALIAS:
    return toTypeAliasFullName(name);
  case DeclSymbol::Kind::VAR:
  case DeclSymbol::Kind::LET:
  case DeclSymbol::Kind::EXPORT_ENV:
  case DeclSymbol::Kind::IMPORT_ENV:
  case DeclSymbol::Kind::CONST:
  case DeclSymbol::Kind::FUNC:
  case DeclSymbol::Kind::METHOD:
  case DeclSymbol::Kind::MOD:
  case DeclSymbol::Kind::MOD_CONST:
    break;
  }
  return name.toString();
}

std::string DeclSymbol::demangle(Kind k, StringRef mangledName) {
  switch (k) {
  case DeclSymbol::Kind::BUILTIN_CMD:
  case DeclSymbol::Kind::CMD:
    mangledName.removeSuffix(strlen(CMD_SYMBOL_SUFFIX));
    break;
  case DeclSymbol::Kind::TYPE_ALIAS:
    mangledName.removeSuffix(strlen(TYPE_ALIAS_SYMBOL_SUFFIX));
    break;
  case DeclSymbol::Kind::VAR:
  case DeclSymbol::Kind::LET:
  case DeclSymbol::Kind::EXPORT_ENV:
  case DeclSymbol::Kind::IMPORT_ENV:
  case DeclSymbol::Kind::CONST:
  case DeclSymbol::Kind::FUNC:
  case DeclSymbol::Kind::METHOD:
  case DeclSymbol::Kind::MOD:
  case DeclSymbol::Kind::MOD_CONST:
    break;
  }
  return mangledName.toString();
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
    if (request.modId != decl.getModId()) {
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

void SymbolIndexes::add(SymbolIndexPtr index) {
  auto iter =
      std::lower_bound(this->indexes.begin(), this->indexes.end(), index, SymbolIndex::Compare());
  if (iter != this->indexes.end() && (*iter)->getModId() == index->getModId()) {
    *iter = std::move(index); // update
  } else {
    this->indexes.insert(iter, std::move(index));
  }
}

SymbolIndexPtr SymbolIndexes::find(unsigned short modId) const {
  auto iter =
      std::lower_bound(this->indexes.begin(), this->indexes.end(), modId, SymbolIndex::Compare());
  if (iter != this->indexes.end()) {
    if (auto &ret = *iter; ret->getModId() == modId) {
      return ret;
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
  if (auto index = indexes.find(request.modId)) {
    if (auto *symbol = index->findSymbol(request.pos)) {
      auto *decl = indexes.findDecl({.modId = symbol->getDeclModId(), .pos = symbol->getDeclPos()});
      if (!decl) {
        return false;
      }
      if (consumer) {
        FindDeclResult ret = {
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
  if (auto *decl = indexes.findDecl(request);
      decl && !hasFlag(decl->getAttr(), DeclSymbol::Attr::BUILTIN)) {
    // add its self
    count++;
    if (cosumer) {
      FindRefsResult ret = {
          .symbol = decl->toRef(),
          .request = *decl,
      };
      cosumer(ret);
    }

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
      if (index->getModId() == request.modId) {
        continue;
      }
      if (auto *foreign = index->findForeignDecl(request)) {
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