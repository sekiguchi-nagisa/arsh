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

#ifndef YDSH_TOOLS_ANALYZER_SYMBOL_H
#define YDSH_TOOLS_ANALYZER_SYMBOL_H

#include "index.h"
#include "lsp.h"

namespace ydsh::lsp {

class SourceManager;
class Source;

std::string generateHoverContent(const SourceManager &srcMan, const Source &src,
                                 const DeclSymbol &decl, bool markup = true);

SymbolKind toSymbolKind(DeclSymbol::Kind kind);

std::string toString(ConstEntry entry);

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_SYMBOL_H
