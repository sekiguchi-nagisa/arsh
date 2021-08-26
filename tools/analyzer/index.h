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

#include <functional>
#include <vector>

#include <misc/buffer.hpp>
#include <misc/result.hpp>
#include <misc/token.hpp>

namespace ydsh::lsp {

class SymbolRef {
private:
  Token token;
  unsigned short declModId;
  unsigned int declPos;

public:
  Token getToken() const { return this->token; }

  unsigned short getDeclModId() const { return this->declModId; }

  unsigned short getDeclPos() const { return this->declPos; }
};

class Symbol {
public:
  enum class Kind : unsigned short {
    VAR,
    FUNC,
    CMD,
    TYPE_ALIAS,
  };

  struct RefLoc {
    unsigned short modID;
    unsigned int pos;
  };

private:
  unsigned int pos;
  unsigned short size;
  Kind kind;
  FlexBuffer<RefLoc> refs;

public:
  static Optional<Symbol> create(Kind kind, Token token) {
    if (token.size > UINT16_MAX) {
      return {};
    }
    return Symbol(kind, token.pos, static_cast<unsigned short>(token.size));
  }

  Symbol(Kind kind, unsigned int pos, unsigned short size) : pos(pos), size(size), kind(kind) {}

  Kind getKind() const { return this->kind; }

  Token getToken() const {
    return Token{
        .pos = this->pos,
        .size = this->size,
    };
  }

  const FlexBuffer<RefLoc> &getRefs() const { return this->refs; }

  void addRef(RefLoc loc) { this->refs.push_back(loc); }
};

class SymbolIndex {
private:
  unsigned short modId;
  int version;
  std::vector<Symbol> decls;
  std::vector<SymbolRef> refs;

public:
  SymbolIndex(unsigned short modId, int version, std::vector<Symbol> &&decls,
              std::vector<SymbolRef> &&refs)
      : modId(modId), version(version), decls(std::move(decls)), refs(std::move(refs)) {}

  unsigned short getModId() const { return this->modId; }

  int getVersion() const { return this->version; }

  const Symbol *findDecl(unsigned int pos) const;

  const SymbolRef *findRef(unsigned int pos) const;

  const std::vector<SymbolRef> &getRefs() const { return this->refs; }
};

class SymbolIndexes {
private:
  std::vector<SymbolIndex> indexes;

public:
  void add(SymbolIndex &&index);

  const SymbolIndex *find(unsigned short modId) const;

  void remove(unsigned short id);
};

const Symbol *findDeclaration(const SymbolIndexes &indexes, Symbol::RefLoc ref);

bool findAllReferences(const SymbolIndexes &indexes, Symbol::RefLoc decl,
                       const std::function<void(const SymbolRef &)> &cosumer);

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_INDEX_H
