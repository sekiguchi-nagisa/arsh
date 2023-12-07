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
  constexpr TokenKind table[] = {
#define GEN_ITEM(T) TokenKind::T,
      EACH_LA_statement(GEN_ITEM)
#undef GEN_ITEM
  };
  for (const auto &e : table) {
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

static bool isKeyword(StringRef name) {
  static const auto keywordSet = initStatementKeywordSet();
  return keywordSet.find(name) != keywordSet.end();
}

static std::string quoteCommandName(StringRef name) {
  if (isKeyword(name)) {
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

static void
resolveInlinedImportedIndexes(const SymbolIndexes &indexes, const SymbolIndexPtr &thisIndex,
                              const SymbolRef ref, std::unordered_set<ModId> &foundModSet,
                              std::vector<std::pair<SymbolRef, SymbolIndexPtr>> &results) {
  for (const auto &pair : thisIndex->getLinks()) {
    const auto attr = pair.second.getImportAttr();
    if (!hasFlag(attr, IndexLink::ImportAttr::INLINED)) {
      continue;
    }
    const auto modId = pair.second.getModId();
    if (foundModSet.find(modId) != foundModSet.end()) { // already found
      continue;
    }
    foundModSet.emplace(modId);
    auto index = indexes.find(modId);
    results.emplace_back(ref, index);
    resolveInlinedImportedIndexes(indexes, index, ref, foundModSet, results);
  }
}

static std::vector<std::pair<SymbolRef, SymbolIndexPtr>>
resolveGlobalImportedIndexes(const SymbolIndexes &indexes, const SymbolIndexPtr &thisIndex) {
  std::vector<std::pair<SymbolRef, SymbolIndexPtr>> results;
  results.emplace_back(SymbolRef(0, 0, BUILTIN_MOD_ID), indexes.find(BUILTIN_MOD_ID));
  for (const auto &pair : thisIndex->getLinks()) {
    const auto attr = pair.second.getImportAttr();
    if (!hasFlag(attr, IndexLink::ImportAttr::GLOBAL)) {
      continue;
    }
    auto index = indexes.find(pair.second.getModId());
    results.emplace_back(pair.first, index);
    std::unordered_set<ModId> idSet;
    resolveInlinedImportedIndexes(indexes, index, pair.first, idSet, results);
  }
  return results;
}

static bool isInlinedImportingIndex(const SymbolIndexes &indexes, const ModId targetModId,
                                    const SymbolIndexPtr &index) {
  for (const auto &e : index->getLinks()) {
    if (!hasFlag(e.second.getImportAttr(), IndexLink::ImportAttr::INLINED)) {
      continue;
    }
    if (e.second.getModId() == targetModId) {
      return true;
    }
    if (isInlinedImportingIndex(indexes, targetModId, indexes.find(e.second.getModId()))) {
      return true;
    }
  }
  return false;
}

static bool isImportingIndex(const SymbolIndexes &indexes, const ModId targetModId,
                             const IndexLink &link) {
  if (!hasFlag(link.getImportAttr(), IndexLink::ImportAttr::GLOBAL)) {
    return false;
  }
  if (targetModId == link.getModId()) {
    return true;
  }
  auto index = indexes.find(link.getModId());
  return isInlinedImportingIndex(indexes, targetModId, index);
}

static bool equalsName(const DeclSymbol &decl, const std::string &mangledNewDeclName,
                       const DeclSymbol &target) {
  if (target.getKind() == DeclSymbol::Kind::MOD) {
    switch (decl.getKind()) {
    case DeclSymbol::Kind::TYPE_ALIAS:
    case DeclSymbol::Kind::ERROR_TYPE_DEF:
    case DeclSymbol::Kind::CONSTRUCTOR:
    case DeclSymbol::Kind::CMD:
      if (!hasFlag(decl.getAttr(), DeclSymbol::Attr::MEMBER)) {
        auto name = target.toDemangledName();
        auto mangledName = DeclSymbol::mangle(decl.getKind(), name);
        if (mangledName == mangledNewDeclName) {
          return true;
        }
      }
      break;
    default:
      break;
    }
  }
  return target != decl && target.getMangledName() == mangledNewDeclName;
}

static bool checkMangledNames(const DeclSymbol &decl, const DeclSymbol &target,
                              const std::vector<std::string> &mangledNewNames,
                              const std::function<void(const RenameResult &)> &consumer) {
  for (const auto &mangledNewName : mangledNewNames) {
    if (equalsName(decl, mangledNewName, target)) {
      if (consumer) {
        consumer(Err(RenameConflict(target.toRef())));
      }
      return false;
    }
  }
  return true;
}

static bool mayBeConflict(const ScopeInterval &declScope, const SymbolRef declRef,
                          const ScopeInterval &targetScope, const SymbolRef targetRef) {
  /**
   * should check name conflict in the following case
   *
   * { { decl } { target } } => no check
   * { { target } { decl } } => no check
   * { decl { target } } => check name conflict
   * { { decl } target } => no check
   * { target { decl } } => check name conflict
   * { { target } decl } => no check
   */
  if (declScope.isIncluding(targetScope) || targetScope.isIncluding(declScope)) {
    if (!declScope.isIncluding(targetScope) && targetRef.getPos() > declRef.getPos()) {
      /**
       * ignore the following case
       *
       * { { decl } target }
       */
      return false;
    }
    if (!targetScope.isIncluding(declScope) && declRef.getPos() > targetRef.getPos()) {
      /**
       * ignore the following case
       *
       * { { target } decl }
       */
      return false;
    }
    return true;
  }
  return false;
}

static bool checkNameConflict(const SymbolIndexes &indexes, const DeclSymbol &decl,
                              StringRef newName,
                              const std::function<void(const RenameResult &)> &consumer) {
  switch (decl.getKind()) {
  case DeclSymbol::Kind::VAR:
  case DeclSymbol::Kind::LET:
  case DeclSymbol::Kind::IMPORT_ENV:
  case DeclSymbol::Kind::EXPORT_ENV:
  case DeclSymbol::Kind::FUNC:
  case DeclSymbol::Kind::TYPE_ALIAS:
    if (hasFlag(decl.getAttr(), DeclSymbol::Attr::MEMBER)) {
      return false; // TODO: support field
    }
    break;
  case DeclSymbol::Kind::ERROR_TYPE_DEF:
  case DeclSymbol::Kind::CONSTRUCTOR:
  case DeclSymbol::Kind::METHOD:
  case DeclSymbol::Kind::CMD:
  case DeclSymbol::Kind::MOD:
    break;
  default:
    return false; // TODO: support other symbols
  }

  std::vector<std::string> mangledNewNames;
  {
    auto recvTypeName =
        DeclSymbol::demangleWithRecv(decl.getKind(), decl.getAttr(), decl.getMangledName()).first;
    auto mangledNewName = DeclSymbol::mangle(recvTypeName, decl.getKind(), newName);
    mangledNewNames.push_back(std::move(mangledNewName));

    // for mod variable
    if (decl.getKind() == DeclSymbol::Kind::MOD) {
      mangledNewNames.push_back(DeclSymbol::mangle(DeclSymbol::Kind::TYPE_ALIAS, newName));
      mangledNewNames.push_back(DeclSymbol::mangle(DeclSymbol::Kind::CMD, newName));
    }
  }

  const auto declIndex = indexes.find(decl.getModId());
  assert(declIndex);

  // check name conflict in global/inlined imported indexes (also include builtin index)
  auto importedIndexes = resolveGlobalImportedIndexes(indexes, declIndex);
  for (auto &[ref, importedIndex] : importedIndexes) {
    if (!isBuiltinMod(importedIndex->getModId())) {
      const auto &declScope = declIndex->getScopes()[decl.getScopeId()];
      const auto &targetScope = declIndex->getScopes()[0]; // always global scope
      if (!mayBeConflict(declScope, decl.toRef(), targetScope, ref)) {
        continue;
      }
    }

    for (auto &mangledNewName : mangledNewNames) {
      if (!isBuiltinMod(importedIndex->getModId()) && mangledNewName[0] == '_') {
        continue; // ignore private symbol
      }
      if (auto *r = importedIndex->findGlobal(mangledNewName)) {
        if (consumer) {
          consumer(Err(RenameConflict(*r)));
        }
        return false;
      }
    }
  }

  // check name conflict in this index
  if (decl.getKind() == DeclSymbol::Kind::CMD ||
      decl.getKind() == DeclSymbol::Kind::MOD) { // check already used external command names
    const auto &set = declIndex->getExternalCmdSet();
    if (set.find(newName.toString()) != set.end()) {
      return false;
    }
  }
  for (const auto &target : declIndex->getDecls()) { // FIXME: check constructor field
    const auto &declScope = declIndex->getScopes()[decl.getScopeId()];
    const auto &targetScope = declIndex->getScopes()[target.getScopeId()];
    if (!mayBeConflict(declScope, decl.toRef(), targetScope, target.toRef())) {
      continue;
    }
    if (!checkMangledNames(decl, target, mangledNewNames, consumer)) {
      return false;
    }
  }

  // check name conflict in other indexes that importing this index
  if (!hasFlag(decl.getAttr(), DeclSymbol::Attr::GLOBAL)) {
    return true;
  }
  for (const auto &index : indexes) {
    if (index->getModId() == decl.getModId()) {
      continue; // ignore this index
    }
    for (const auto &e : index->getLinks()) {
      if (!isImportingIndex(indexes, decl.getModId(), e.second)) {
        continue;
      }
      for (const auto &target : index->getDecls()) {
        const auto &declScope = index->getScopes()[0]; // always global scope
        const auto &targetScope = index->getScopes()[target.getScopeId()];
        if (!mayBeConflict(declScope, e.first, targetScope, target.toRef())) {
          continue;
        }
        if (!checkMangledNames(decl, target, mangledNewNames, consumer)) {
          return false;
        }
      }
    }
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
    if (decl.getKind() == DeclSymbol::Kind::MOD) {
      if (isKeyword(actualNewName)) {
        return RenameValidationStatus::KEYWORD;
      }
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
  if ((decl == nullptr) || decl->getKind() == DeclSymbol::Kind::HERE_START) {
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
  auto token = src->stripAppliedNameSigil(this->symbol.getToken());
  return {
      .range = src->toRange(token).unwrap(),
      .newText = this->newName.toString(),
  };
}

} // namespace ydsh::lsp