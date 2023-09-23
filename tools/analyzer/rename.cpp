/*
 * Copyright (C) 2023 Nagisa Sekiguchi
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

#include <lexer.h>
#include <misc/format.hpp>

#include "rename.h"

namespace ydsh::lsp {

static std::unordered_set<StringRef, StrRefHash> initStatementKeywordSet() {
  std::unordered_set<StringRef, StrRefHash> set;
  TokenKind table[] = {
#define GEN_ITEM(T) TokenKind::T,
      EACH_LA_statement(GEN_ITEM)
#undef GEN_ITEM
  };
  for (auto &e : table) {
    StringRef keyword = toString(e);
    if (isValidIdentifier(keyword)) {
      set.emplace(keyword);
    }
  }
  // add old keyword
  set.emplace("import-env");
  set.emplace("export-env");
  return set;
}

static std::string quoteCommandName(StringRef name) {
  static const auto keywordSet = initStatementKeywordSet();
  if (keywordSet.find(name) != keywordSet.end()) {
    std::string ret = "\\";
    ret += name;
    return ret;
  }

  std::string ret;
  if (quoteAsCmdOrShellArg(name, ret, true)) {
    return ret;
  }
  return "";
}

static void resolveGlobalImportedIndexes(const SymbolIndexes &indexes,
                                         const SymbolIndexPtr &thisIndex, SymbolIndexes &resolved) {
  for (auto &e : thisIndex->getLinks()) {
    auto &link = e.second;
    if (hasFlag(link.getImportAttr(), IndexLink::ImportAttr::GLOBAL)) {
      auto importedIndex = indexes.find(link.getModId());
      assert(importedIndex);
      resolved.add(std::move(importedIndex));
      if (hasFlag(link.getImportAttr(), IndexLink::ImportAttr::INLINED)) {
        resolveGlobalImportedIndexes(indexes, importedIndex, resolved);
      }
    }
  }
}

/**
 * get imported modules at this module
 * @param indexes
 * @param thisIndex
 * @return
 */
static SymbolIndexes resolveGlobalImportedIndexes(const SymbolIndexes &indexes,
                                                  const SymbolIndexPtr &thisIndex) {
  SymbolIndexes resolved;
  auto builtin = indexes.find(BUILTIN_MOD_ID);
  assert(builtin);
  resolved.add(builtin);
  resolveGlobalImportedIndexes(indexes, thisIndex, resolved);
  return resolved;
}

/**
 * get modules that import this module
 * @param indexes
 * @param thisIndex
 * @return
 */
static SymbolIndexes collectGlobalImportingIndexes(const SymbolIndexes &indexes,
                                                   const SymbolIndexPtr &thisIndex) {
  SymbolIndexes importing;
  (void)indexes;
  (void)thisIndex;
  return importing;
}

static bool checkNameConflict(const SymbolIndexes &indexes, const DeclSymbol &decl,
                              StringRef newName,
                              const std::function<void(const RenameResult &)> &consumer) {
  switch (decl.getKind()) {
  case DeclSymbol::Kind::VAR:
  case DeclSymbol::Kind::LET:
  case DeclSymbol::Kind::IMPORT_ENV:
  case DeclSymbol::Kind::EXPORT_ENV:
    if (hasFlag(decl.getAttr(), DeclSymbol::Attr::MEMBER)) {
      return false; // TODO: support field
    }
    break;
  default:
    return false; // TODO: support other symbols
  }

  auto recvTypeName =
      DeclSymbol::demangleWithRecv(decl.getKind(), decl.getAttr(), decl.getMangledName()).first;
  auto mangledName = DeclSymbol::mangle(recvTypeName, decl.getKind(), newName);
  auto declIndex = indexes.find(decl.getModId());
  assert(declIndex);

  // check name conflict in global/inlined imported indexes (also include builtin index)
  auto importedIndexes = resolveGlobalImportedIndexes(indexes, declIndex);
  for (auto &importedIndex : importedIndexes) { // FIXME: import order aware conflict check
    if (auto *r = importedIndex->findGlobal(mangledName)) {
      if (consumer) {
        consumer(Err(RenameConflict(*r)));
      }
      return false;
    }
  }

  // check name conflict in this index // FIXME: check constructor field
  for (auto &e : declIndex->getDecls()) {
    if (!decl.getScopeInfo().isIncluding(e.getScopeInfo())) {
      continue;
    }
    if (decl.getScopeInfo() != e.getScopeInfo() && decl.getPos() > e.getPos()) {
      /**
       * ignore following case
       *   { { decl } e }
       */
      continue;
    }
    if (e.getMangledName() == mangledName) {
      if (consumer) {
        consumer(Err(RenameConflict(e.toRef())));
      }
      return false;
    }
  }

  // check name conflict in other indexes that importing this index
  if (hasFlag(decl.getAttr(), DeclSymbol::Attr::GLOBAL)) {
    auto importing = collectGlobalImportingIndexes(indexes, declIndex);
  }
  return true;
}

RenameValidationStatus validateRename(const SymbolIndexes &indexes, SymbolRequest request,
                                      StringRef newName,
                                      const std::function<void(const RenameResult &)> &consumer) {
  const auto resolved = resolveRenameLocation(indexes, request);
  if (!resolved.hasValue()) {
    return RenameValidationStatus::INVALID_SYMBOL;
  }
  const auto &decl = resolved.unwrap().decl;

  if (isBuiltinMod(decl.getModId()) || hasFlag(decl.getAttr(), DeclSymbol::Attr::BUILTIN) ||
      decl.getKind() == DeclSymbol::Kind::THIS) {
    return RenameValidationStatus::BUILTIN;
  }

  // check newName
  std::string declName = decl.toDemangledName();
  if (declName == newName) {
    return RenameValidationStatus::DO_NOTHING;
  }
  auto actualNewName = newName.toString();
  assert(decl.getKind() != DeclSymbol::Kind::BUILTIN_CMD);
  if (decl.getKind() == DeclSymbol::Kind::CMD) {
    actualNewName = quoteCommandName(actualNewName);
    if (actualNewName.empty()) {
      return RenameValidationStatus::INVALID_NAME;
    }
  } else {
    if (!isValidIdentifier(actualNewName)) {
      return RenameValidationStatus::INVALID_NAME;
    }
  }

  if (!checkNameConflict(indexes, decl, newName, consumer)) {
    return RenameValidationStatus::NAME_CONFLICT;
  }

  if (consumer) {
    findAllReferences(indexes, decl, false, [&](const FindRefsResult &ret) {
      consumer(Ok(RenameTarget(ret.symbol, actualNewName)));
    });
  }
  return RenameValidationStatus::CAN_RENAME;
}

Optional<FindDeclResult> resolveRenameLocation(const SymbolIndexes &indexes,
                                               SymbolRequest request) {
  const DeclSymbol *decl = nullptr;
  const Symbol *symbol = nullptr;
  findDeclaration(indexes, request, [&decl, &symbol](const FindDeclResult &r) {
    decl = &r.decl;
    symbol = &r.request;
  });
  if (!decl || decl->getKind() == DeclSymbol::Kind::HERE_START) {
    return {};
  }
  return FindDeclResult{
      .decl = *decl,
      .request = *symbol,
  };
}

TextEdit RenameTarget::toTextEdit(const SourceManager &srcMan) const {
  auto src = srcMan.findById(this->symbol.getModId());
  assert(src);
  auto token = this->symbol.getToken();
  if (auto ref = src->toStrRef(token); ref.startsWith("$") && ref.size() > 1) {
    token = token.sliceFrom(1); // remove prefix '$'
    ref = src->toStrRef(token);
    if (ref.startsWith("{") && ref.endsWith("}") && ref.size() > 2) {
      token = token.sliceFrom(1); // remove surrounded '{ }'
      token.size--;
    }
  }
  return {
      .range = src->toRange(token).unwrap(),
      .newText = this->newName.toString(),
  };
}

} // namespace ydsh::lsp