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

#include "symbol.h"

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

} // namespace ydsh::lsp