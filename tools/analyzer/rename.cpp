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

RenameValidationStatus validateRename(const SymbolIndexes &indexes, SymbolRequest request,
                                      StringRef newName,
                                      const std::function<void(const RenameResult &)> &consumer) {
  const DeclSymbol *decl = nullptr;
  findDeclaration(indexes, request, [&decl](const FindDeclResult &r) { decl = &r.decl; });
  if (!decl || decl->getKind() == DeclSymbol::Kind::HERE_START) {
    return RenameValidationStatus::INVALID_SYMBOL;
  }
  if (isBuiltinMod(decl->getModId()) || hasFlag(decl->getAttr(), DeclSymbol::Attr::BUILTIN) ||
      decl->getKind() == DeclSymbol::Kind::THIS) {
    return RenameValidationStatus::BUILTIN;
  }

  // check newName
  std::string declName = decl->toDemangledName();
  if (declName == newName) {
    return RenameValidationStatus::DO_NOTHING;
  }
  auto actualNewName = newName.toString();
  assert(decl->getKind() != DeclSymbol::Kind::BUILTIN_CMD);
  if (decl->getKind() == DeclSymbol::Kind::CMD) {
    actualNewName = quoteCommandName(actualNewName);
    if (actualNewName.empty()) {
      return RenameValidationStatus::INVALID_NAME;
    }
  } else {
    if (!isValidIdentifier(actualNewName)) {
      return RenameValidationStatus::INVALID_NAME;
    }
  }

  // check name conflict of builtin type (for constructor/type alias)

  (void)consumer;
  return RenameValidationStatus::DO_NOTHING;
}

TextEdit RenameResult::toTextEdit(const SourceManager &srcMan) const {
  auto src = srcMan.findById(this->symbol.getModId());
  assert(src);
  auto range = toRange(*src, this->symbol.getToken()).unwrap(); // FIXME: resolve actual range?
  return {
      .range = range,
      .newText = this->newName.toString(),
  };
}

} // namespace ydsh::lsp