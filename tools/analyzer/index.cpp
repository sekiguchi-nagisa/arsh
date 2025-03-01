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

namespace arsh::lsp {

// ######################
// ##     DeclBase     ##
// ######################

void DeclBase::addRef(SymbolRef ref) {
  auto iter = std::lower_bound(this->refs.begin(), this->refs.end(), ref);
  if (iter != this->refs.end() && iter->getModId() == ref.getModId() &&
      iter->getPos() == ref.getPos()) {
    *iter = ref; // update
  } else {
    this->refs.insert(iter, ref);
  }
}

// ########################
// ##     DeclSymbol     ##
// ########################

bool DeclSymbol::has(Attr a) const { return hasFlag(this->getAttr(), a); }

std::pair<ModId, bool> DeclSymbol::getInfoAsModId() const {
  auto ref = this->getInfo();
  auto value = convertToNum10<int>(ref.begin(), ref.end());
  if (value && value.value <= UINT16_MAX && value.value >= 0) {
    return {ModId{static_cast<unsigned short>(value.value)}, true};
  }
  return {BUILTIN_MOD_ID, false};
}

std::string DeclSymbol::mangle(StringRef recvTypeName, Kind k, StringRef name) {
  std::string value;
  switch (k) {
  case Kind::BUILTIN_CMD:
  case Kind::CMD:
    value = toCmdFullName(name);
    break;
  case Kind::BUILTIN_TYPE:
  case Kind::TYPE_ALIAS:
  case Kind::ERROR_TYPE_DEF:
  case Kind::CONSTRUCTOR:
    value = toTypeAliasFullName(name);
    break;
  case Kind::METHOD:
  case Kind::GENERIC_METHOD: {
    value = name.toString();
    value += METHOD_SYMBOL_SUFFIX;
    break;
  }
  case Kind::VAR:
  case Kind::LET:
  case Kind::IMPORT_ENV:
  case Kind::EXPORT_ENV:
  case Kind::PREFIX_ENV:
  case Kind::THIS:
  case Kind::CONST:
  case Kind::FUNC:
  case Kind::MOD:
  case Kind::MOD_CONST:
  case Kind::PARAM:
  case Kind::GENERIC_METHOD_PARAM:
  case Kind::HERE_START:
    value = name.toString();
    break;
  }

  if (!recvTypeName.empty()) {
    value += "@";
    value += recvTypeName;
  }
  return value;
}

std::pair<StringRef, StringRef> DeclSymbol::demangleWithRecv(Kind k, Attr a,
                                                             StringRef mangledName) {
  StringRef recvTypeName;
  if (hasFlag(a, Attr::MEMBER)) {
    auto pos = mangledName.lastIndexOf("@");
    assert(pos != StringRef::npos);
    recvTypeName = mangledName.substr(pos + 1);
    mangledName = mangledName.substr(0, pos);
  }

  switch (k) {
  case Kind::BUILTIN_CMD:
  case Kind::CMD:
    mangledName.removeSuffix(strlen(CMD_SYMBOL_SUFFIX));
    break;
  case Kind::BUILTIN_TYPE:
  case Kind::TYPE_ALIAS:
  case Kind::ERROR_TYPE_DEF:
  case Kind::CONSTRUCTOR:
    mangledName.removeSuffix(strlen(TYPE_ALIAS_SYMBOL_SUFFIX));
    break;
  case Kind::METHOD:
  case Kind::GENERIC_METHOD:
    mangledName.removeSuffix(strlen(METHOD_SYMBOL_SUFFIX));
    break;
  case Kind::VAR:
  case Kind::LET:
  case Kind::IMPORT_ENV:
  case Kind::EXPORT_ENV:
  case Kind::PREFIX_ENV:
  case Kind::THIS:
  case Kind::CONST:
  case Kind::FUNC:
  case Kind::MOD:
  case Kind::MOD_CONST:
  case Kind::HERE_START:
    break;
  case Kind::PARAM:
  case Kind::GENERIC_METHOD_PARAM: {
    auto r = mangledName.find('+');
    assert(r != StringRef::npos);
    mangledName = mangledName.substr(0, r);
    break;
  }
  }
  return {recvTypeName, mangledName};
}

std::string DeclSymbol::demangle(Kind k, Attr a, StringRef mangledName) {
  return demangleWithRecv(k, a, mangledName).second.toString();
}

// #################################
// ##     PackedParamTypesMap     ##
// #################################

bool PackedParamTypesMap::addPackedParamTypes(unsigned int type, std::string &&packed) {
  auto pair = this->types.emplace(type, std::move(packed));
  return pair.second;
}

bool PackedParamTypesMap::addSymbol(SymbolRef symbol, unsigned int typeId) {
  auto pair = this->ids.emplace(symbol.getPos(), typeId);
  return pair.second;
}

const std::string *PackedParamTypesMap::lookupByTypeId(unsigned int typeId) const {
  auto iter = this->types.find(typeId);
  return iter != this->types.end() ? &iter->second : nullptr;
}

const std::string *PackedParamTypesMap::lookupByPos(unsigned int pos) const {
  auto iter = this->ids.find(pos);
  if (iter == this->ids.end()) {
    return nullptr;
  }
  auto packed = this->lookupByTypeId(iter->second);
  assert(packed); // always found
  return packed;
}

ModId TypeWrapper::resolveBelongedModId() const {
  if (StringRef ref = this->getValue(); isQualifiedTypeName(ref)) {
    auto retPos = ref.find('.');
    assert(retPos != StringRef::npos);
    ref = ref.slice(0, retPos);
    ref.removePrefix(strlen(MOD_SYMBOL_PREFIX));
    auto pair = convertToNum10<uint32_t>(ref.begin(), ref.end());
    assert(pair && pair.value <= SYS_LIMIT_MOD_ID);
    return static_cast<ModId>(pair.value);
  }
  return BUILTIN_MOD_ID;
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

const ForeignDecl *SymbolIndex::findForeignDecl(ModId modId, const std::string &mangledName) const {
  auto key = std::make_pair(modId, mangledName);
  if (auto iter = this->foreignNames.find(key); iter != this->foreignNames.end()) {
    const SymbolRequest request{
        .modId = iter->second.getModId(),
        .pos = iter->second.getPos(),
    };
    return this->findForeignDecl(request);
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

const TypeWrapper *SymbolIndex::findBaseType(StringRef qualifiedTypeName) const {
  TypeWrapper type(qualifiedTypeName);
  if (auto iter = this->inheritanceMap.find(type); iter != this->inheritanceMap.end()) {
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
  if (iter != this->indexes.end() && (*iter)->modId == index->modId) {
    *iter = std::move(index); // update
  } else {
    this->indexes.insert(iter, std::move(index));
  }
}

SymbolIndexPtr SymbolIndexes::find(ModId modId) const {
  auto iter =
      std::lower_bound(this->indexes.begin(), this->indexes.end(), modId, SymbolIndex::Compare());
  if (iter != this->indexes.end()) {
    if (auto ret = *iter; ret->modId == modId) {
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

static const DeclSymbol *findDeclFromForeignIndex(const SymbolIndexes &indexes,
                                                  const ForeignDecl &foreign) {
  if (auto index = indexes.find(foreign.getModId())) {
    if (auto *ref = index->findGlobal(foreign.getMangledName())) {
      return index->findDecl(ref->getPos());
    }
  }
  return nullptr;
}

bool findDeclaration(const SymbolIndexes &indexes, SymbolRequest request,
                     const std::function<void(const FindDeclResult &)> &consumer) {
  if (auto index = indexes.find(request.modId)) {
    if (const auto *symbol = index->findSymbol(request.pos)) {
      if (symbol->getDeclModId() == index->modId) { // find own decl
        if (const auto *decl = index->findDecl(symbol->getDeclPos())) {
          if (consumer) {
            FindDeclResult ret = {
                .decl = *decl,
                .request = *symbol,
            };
            consumer(ret);
          }
          return true;
        }
      } else {
        request = {.modId = symbol->getDeclModId(), .pos = symbol->getDeclPos()};
        if (const auto *foreign = index->findForeignDecl(request)) { // find from foreign decl
          if (auto *decl = findDeclFromForeignIndex(indexes, *foreign)) {
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
      }
    }
  }
  return false;
}

unsigned int findAllReferences(const SymbolIndexes &indexes, const DeclSymbol &decl,
                               bool ignoreBuiltin,
                               const std::function<void(const FindRefsResult &)> &consumer) {
  unsigned int count = 0;
  if (decl.has(DeclSymbol::Attr::BUILTIN) && ignoreBuiltin) {
    return 0;
  }

  // add its self
  count++;
  if (consumer) {
    FindRefsResult ret = {
        .symbol = decl.toRef(),
        .request = decl,
    };
    consumer(ret);
  }

  // search local ref
  for (const auto &e : decl.getRefs()) {
    count++;
    if (consumer) {
      FindRefsResult ret = {
          .symbol = e,
          .request = decl,
      };
      consumer(ret);
    }
  }

  // search foreign ref
  const auto modId = decl.getModId();
  const auto mangledName = decl.getMangledName().toString();
  for (const auto &index : indexes) {
    if (index->modId == modId) {
      continue;
    }
    if (const auto *foreign = index->findForeignDecl(modId, mangledName)) {
      for (const auto &e : foreign->getRefs()) {
        count++;
        if (consumer) {
          FindRefsResult ret = {
              .symbol = e,
              .request = decl,
          };
          consumer(ret);
        }
      }
    }
  }
  return count;
}

} // namespace arsh::lsp