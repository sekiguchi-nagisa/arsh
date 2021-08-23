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

#ifndef YDSH_TOOLS_ANALYZER_INDEX_H
#define YDSH_TOOLS_ANALYZER_INDEX_H

#include <vector>

#include <misc/buffer.hpp>
#include <misc/token.hpp>

namespace ydsh::lsp {

class SymbolRef {
private:
  Token token;
  unsigned short modID;
  unsigned int symbolID;

public:
  Token getToken() const { return this->token; }

  unsigned short getModID() const { return this->modID; }

  unsigned short getSymbolID() const { return this->symbolID; }
};

class Symbol {
public:
  enum class Kind : unsigned short {
    VAR,
    FUNC,
    TYPE_ALIAS,
  };

  struct RefLoc {
    unsigned short modID;
    unsigned int pos;
  };

private:
  Token token;
  Kind kind;
  unsigned short modID;
  FlexBuffer<RefLoc> refs;

public:
  Symbol(Kind kind, Token token, unsigned short modID) : token(token), kind(kind), modID(modID) {}

  Kind getKind() const { return this->kind; }

  Token getToken() const { return this->token; }

  unsigned short getModID() const { return this->modID; }

  const FlexBuffer<RefLoc> &getRefs() const { return this->refs; }

  void addRef(RefLoc loc) { this->refs.push_back(loc); }
};

class SymbolIndex {
private:
  std::vector<Symbol> decls;
  std::vector<SymbolRef> refs;

public:
  SymbolIndex(std::vector<Symbol> &&decls, std::vector<SymbolRef> &&refs)
      : decls(std::move(decls)), refs(std::move(refs)) {}

  const Symbol *findDecl(unsigned int pos) const;

  const SymbolRef *findRef(unsigned int pos) const;

  const std::vector<SymbolRef> &getRefs() const { return this->refs; }
};

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_INDEX_H
