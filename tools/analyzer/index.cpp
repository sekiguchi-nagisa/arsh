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

std::pair<ModId, bool> DeclSymbol::getInfoAsModId() const {
  auto ref = this->getInfo();
  auto value = convertToDecimal<int>(ref.begin(), ref.end());
  if (value && value.value <= UINT16_MAX && value.value >= 0) {
    return {ModId{static_cast<unsigned short>(value.value)}, true};
  }
  return {BUILTIN_MOD_ID, false};
}

std::string DeclSymbol::mangle(StringRef recvTypeName, Kind k, StringRef name) {
  std::string value;
  switch (k) {
  case DeclSymbol::Kind::BUILTIN_CMD:
  case DeclSymbol::Kind::CMD:
    value = toCmdFullName(name);
    break;
  case DeclSymbol::Kind::TYPE_ALIAS:
  case DeclSymbol::Kind::ERROR_TYPE_DEF:
  case DeclSymbol::Kind::CONSTRUCTOR:
    value = toTypeAliasFullName(name);
    break;
  case DeclSymbol::Kind::METHOD: {
    value = name.toString();
    value += METHOD_SYMBOL_SUFFIX;
    break;
  }
  case DeclSymbol::Kind::VAR:
  case DeclSymbol::Kind::LET:
  case DeclSymbol::Kind::EXPORT_ENV:
  case DeclSymbol::Kind::IMPORT_ENV:
  case DeclSymbol::Kind::CONST:
  case DeclSymbol::Kind::FUNC:
  case DeclSymbol::Kind::MOD:
  case DeclSymbol::Kind::MOD_CONST:
  case DeclSymbol::Kind::HERE_START:
    value = name.toString();
    break;
  }

  if (!recvTypeName.empty()) {
    value += "@";
    value += recvTypeName;
  }
  return value;
}

std::string DeclSymbol::demangle(Kind k, Attr a, StringRef mangledName) {
  if (hasFlag(a, Attr::MEMBER)) {
    auto pos = mangledName.lastIndexOf("@");
    assert(pos != StringRef::npos);
    mangledName = mangledName.substr(0, pos);
  }

  switch (k) {
  case DeclSymbol::Kind::BUILTIN_CMD:
  case DeclSymbol::Kind::CMD:
    mangledName.removeSuffix(strlen(CMD_SYMBOL_SUFFIX));
    break;
  case DeclSymbol::Kind::TYPE_ALIAS:
  case DeclSymbol::Kind::ERROR_TYPE_DEF:
  case DeclSymbol::Kind::CONSTRUCTOR:
    mangledName.removeSuffix(strlen(TYPE_ALIAS_SYMBOL_SUFFIX));
    break;
  case DeclSymbol::Kind::METHOD:
    mangledName.removeSuffix(strlen(METHOD_SYMBOL_SUFFIX));
    break;
  case DeclSymbol::Kind::VAR:
  case DeclSymbol::Kind::LET:
  case DeclSymbol::Kind::EXPORT_ENV:
  case DeclSymbol::Kind::IMPORT_ENV:
  case DeclSymbol::Kind::CONST:
  case DeclSymbol::Kind::FUNC:
  case DeclSymbol::Kind::MOD:
  case DeclSymbol::Kind::MOD_CONST:
  case DeclSymbol::Kind::HERE_START:
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

const SymbolRef *SymbolIndex::findGlobal(const std::string &mangledName) const {
  auto iter = this->globals.find(mangledName);
  if (iter != this->globals.end()) {
    return &iter->second;
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

SymbolIndexPtr SymbolIndexes::find(ModId modId) const {
  auto iter =
      std::lower_bound(this->indexes.begin(), this->indexes.end(), modId, SymbolIndex::Compare());
  if (iter != this->indexes.end()) {
    if (auto &ret = *iter; ret->getModId() == modId) {
      return ret;
    }
  }
  return nullptr;
}

void SymbolIndexes::remove(ModId id) {
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
                       const std::function<void(const FindRefsResult &)> &consumer,
                       bool ignoreBuiltin) {
  unsigned int count = 0;
  if (auto *decl = indexes.findDecl(request)) {
    if (hasFlag(decl->getAttr(), DeclSymbol::Attr::BUILTIN) && ignoreBuiltin) {
      return false;
    }

    // add its self
    count++;
    if (consumer) {
      FindRefsResult ret = {
          .symbol = decl->toRef(),
          .request = *decl,
      };
      consumer(ret);
    }

    // search local ref
    for (auto &e : decl->getRefs()) {
      count++;
      if (consumer) {
        FindRefsResult ret = {
            .symbol = e,
            .request = *decl,
        };
        consumer(ret);
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
          if (consumer) {
            FindRefsResult ret = {
                .symbol = e,
                .request = *decl,
            };
            consumer(ret);
          }
        }
      }
    }
  }
  return count > 0;
}

RenameValidationStatus validateRename(const SymbolIndexes &indexes, SymbolRequest request,
                                      const std::string &newName,
                                      const std::function<void(const FindRefsResult &)> &consumer) {
  const DeclSymbol *decl = nullptr;
  findDeclaration(indexes, request, [&decl](const FindDeclResult &r) { decl = &r.decl; });
  if (!decl) {
    return RenameValidationStatus::INVALID_SYMBOL;
  }

  (void)newName;
  (void)consumer;
  return RenameValidationStatus::DO_NOTHING;
}

} // namespace ydsh::lsp