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

#ifndef ARSH_TOOLS_ANALYZER_RENAME_H
#define ARSH_TOOLS_ANALYZER_RENAME_H

#include "index.h"
#include "source.h"

namespace arsh::lsp {

Optional<FindDeclResult> resolveRenameLocation(const SymbolIndexes &indexes, SymbolRequest request);

enum class RenameValidationStatus {
  CAN_RENAME,
  DO_NOTHING, // not perform rename since new name is equivalent to old new
  INVALID_SYMBOL,
  INVALID_NAME,
  KEYWORD, // not perform rename since new name is equivalent to keyword
  BUILTIN, // not perform rename since builtin method/field/command
  NAME_CONFLICT,
};

struct RenameTarget {
  const SymbolRef symbol;  // rename target symbol
  const StringRef newName; // may be escaped (for user-defined command)

  RenameTarget(SymbolRef symbol, StringRef newName) : symbol(symbol), newName(newName) {}

  TextEdit toTextEdit(const SourceManager &srcMan) const;
};

struct RenameConflict {
  const SymbolRef symbol; // conflict symbol

  explicit RenameConflict(SymbolRef symbol) : symbol(symbol) {}
};

using RenameResult = Result<RenameTarget, RenameConflict>;

RenameValidationStatus validateRename(const SymbolIndexes &indexes, SymbolRequest request,
                                      StringRef newName,
                                      const std::function<void(const RenameResult &)> &consumer);

} // namespace arsh::lsp

#endif // ARSH_TOOLS_ANALYZER_RENAME_H
