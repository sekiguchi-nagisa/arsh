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

class Symbol {
private:
  unsigned int pos;
  unsigned short size;
  unsigned short declModId;
  unsigned int declPos;

public:
  static Optional<Symbol> create(Token token, unsigned short declModId, unsigned int declPos) {
    if (token.size > UINT16_MAX) {
      return {};
    }
    return Symbol(token.pos, static_cast<unsigned short>(token.size), declModId, declPos);
  }

  Symbol(unsigned int pos, unsigned short size, unsigned short declModId, unsigned int declPos)
      : pos(pos), size(size), declModId(declModId), declPos(declPos) {}

  unsigned int getPos() const { return this->pos; }

  Token getToken() const {
    return Token{
        .pos = this->pos,
        .size = this->size,
    };
  }

  unsigned short getDeclModId() const { return this->declModId; }

  unsigned short getDeclPos() const { return this->declPos; }

  struct Compare {
    bool operator()(const Symbol &x, unsigned int y) const { return x.getToken().endPos() < y; }

    bool operator()(unsigned int x, const Symbol &y) const { return x < y.getToken().pos; }
  };
};

class SymbolRef {
private:
  unsigned int pos;
  unsigned short size;
  unsigned short modId;

public:
  static Optional<SymbolRef> create(Token token, unsigned short modId) {
    if (token.size > UINT16_MAX) {
      return {};
    }
    return SymbolRef(token.pos, static_cast<unsigned short>(token.size), modId);
  }

  SymbolRef(unsigned int pos, unsigned short size, unsigned short modId)
      : pos(pos), size(size), modId(modId) {}

  unsigned int getPos() const { return this->pos; }

  Token getToken() const {
    return Token{
        .pos = this->pos,
        .size = this->size,
    };
  }

  unsigned short getModId() const { return this->modId; }
};

class DeclSymbol {
public:
  enum class Kind : unsigned short {
    VAR,
    FUNC,
    CMD,
    TYPE_ALIAS,
  };

private:
  unsigned int pos;
  unsigned short size;
  Kind kind;
  FlexBuffer<SymbolRef> refs;

public:
  static Optional<DeclSymbol> create(Kind kind, Token token) {
    if (token.size > UINT16_MAX) {
      return {};
    }
    return DeclSymbol(kind, token.pos, static_cast<unsigned short>(token.size));
  }

  DeclSymbol(Kind kind, unsigned int pos, unsigned short size) : pos(pos), size(size), kind(kind) {}

  Kind getKind() const { return this->kind; }

  Token getToken() const {
    return Token{
        .pos = this->pos,
        .size = this->size,
    };
  }

  unsigned int getPos() const { return this->pos; }

  const FlexBuffer<SymbolRef> &getRefs() const { return this->refs; }

  void addRef(SymbolRef ref) { this->refs.push_back(ref); }

  struct Compare {
    bool operator()(const DeclSymbol &x, unsigned int y) const { return x.getToken().endPos() < y; }

    bool operator()(unsigned int x, const DeclSymbol &y) const { return x < y.getToken().pos; }
  };
};

class SymbolIndex {
private:
  unsigned short modId;
  int version;
  std::vector<DeclSymbol> decls;
  std::vector<Symbol> symbols;

public:
  SymbolIndex(unsigned short modId, int version, std::vector<DeclSymbol> &&decls,
              std::vector<Symbol> &&symbols)
      : modId(modId), version(version), decls(std::move(decls)), symbols(std::move(symbols)) {}

  unsigned short getModId() const { return this->modId; }

  int getVersion() const { return this->version; }

  const DeclSymbol *findDecl(unsigned int pos) const;

  const Symbol *findSymbol(unsigned int pos) const;

  const std::vector<DeclSymbol> &getDecls() const { return this->decls; }

  const std::vector<Symbol> &getSymbols() const { return this->symbols; }

  struct Compare {
    bool operator()(const SymbolIndex &x, unsigned short id) const { return x.getModId() < id; }

    bool operator()(unsigned short id, const SymbolIndex &y) const { return id < y.getModId(); }
  };
};

class SymbolIndexes {
private:
  std::vector<SymbolIndex> indexes;

public:
  void add(SymbolIndex &&index);

  const SymbolIndex *find(unsigned short modId) const;

  void remove(unsigned short id);

  const DeclSymbol *findDecl(unsigned short modId, unsigned int pos) const {
    if (auto *index = this->find(modId); index) {
      return index->findDecl(pos);
    }
    return nullptr;
  }
};

const DeclSymbol *findDeclaration(const SymbolIndexes &indexes, SymbolRef ref);

bool findAllReferences(const SymbolIndexes &indexes, SymbolRef ref,
                       const std::function<void(const SymbolRef &)> &cosumer);

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_INDEX_H
