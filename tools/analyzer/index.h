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
#include <misc/enum_util.hpp>
#include <misc/resource.hpp>
#include <misc/result.hpp>
#include <misc/string_ref.hpp>
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

  bool operator<(SymbolRef o) const {
    return this->getModId() < o.getModId() ||
           (!(o.getModId() < this->getModId()) && this->getPos() < o.getPos());
  }
};

// for sybmol lookup
struct SymbolRequest {
  unsigned short modId;
  unsigned int pos;
};

class Refs {
private:
  FlexBuffer<SymbolRef> refs;

public:
  const FlexBuffer<SymbolRef> &getRefs() const { return this->refs; }

  void addRef(SymbolRef ref);
};

class DeclSymbol : public Refs {
public:
  enum class Kind : unsigned char {
    VAR,
    FUNC,
    CMD,
    TYPE_ALIAS,
  };

  enum class Attr : unsigned char {
    GLOBAL = 1u << 0u,
    PUBLIC = 1u << 1u,
  };

private:
  unsigned int pos;
  unsigned short size;
  Kind kind;
  Attr attr;
  CStrPtr mangledName;
  CStrPtr info; // hover information

public:
  static Optional<DeclSymbol> create(Kind kind, Attr attr, Token token, const std::string &name,
                                     const char *info = nullptr) {
    if (token.size > UINT16_MAX) {
      return {};
    }
    return DeclSymbol(kind, attr, token.pos, static_cast<unsigned short>(token.size), name,
                      info != nullptr ? info : "(dummy)");
  }

  DeclSymbol(Kind kind, Attr attr, unsigned int pos, unsigned short size, const std::string &name,
             const char *info)
      : pos(pos), size(size), kind(kind), attr(attr), mangledName(CStrPtr(strdup(name.c_str()))),
        info(CStrPtr(strdup(info))) {}

  Kind getKind() const { return this->kind; }

  Attr getAttr() const { return this->attr; }

  Token getToken() const {
    return Token{
        .pos = this->pos,
        .size = this->size,
    };
  }

  unsigned int getPos() const { return this->pos; }

  StringRef getMangledName() const { return this->mangledName.get(); }

  StringRef getInfo() const { return this->info.get(); }

  struct Compare {
    bool operator()(const DeclSymbol &x, unsigned int y) const { return x.getToken().endPos() < y; }

    bool operator()(unsigned int x, const DeclSymbol &y) const { return x < y.getToken().pos; }
  };
};

class ForeignDecl : public Refs {
private:
  unsigned int declPos;
  unsigned short size;
  unsigned short declModId;

public:
  static ForeignDecl create(unsigned short declModId, const DeclSymbol &decl) {
    Token token = decl.getToken();
    return {token.pos, static_cast<unsigned short>(token.size), declModId};
  }

  ForeignDecl(unsigned int pos, unsigned short size, unsigned short modId)
      : declPos(pos), size(size), declModId(modId) {}

  unsigned int getDeclPos() const { return this->declPos; }

  unsigned short getDeclModId() const { return this->declModId; }

  Token getToken() const {
    return Token{
        .pos = this->declPos,
        .size = this->size,
    };
  }

  bool operator<(const ForeignDecl &o) const {
    return this->getDeclModId() < o.getDeclModId() || (!(o.getDeclModId() < this->getDeclModId()) &&
                                                       this->getToken().endPos() < o.getDeclPos());
  }

  struct Compare {
    bool operator()(const ForeignDecl &x, const SymbolRequest &y) const {
      return x.getDeclModId() < y.modId ||
             (!(y.modId < x.getDeclModId()) && x.getToken().endPos() < y.pos);
    }

    bool operator()(const SymbolRequest &x, const ForeignDecl &y) const {
      return x.modId < y.getDeclModId() ||
             (!(y.getDeclModId() < x.modId) && x.pos < y.getDeclPos());
    }
  };
};

class SymbolIndex {
private:
  unsigned short modId;
  int version;
  std::vector<DeclSymbol> decls;
  std::vector<Symbol> symbols;
  std::vector<ForeignDecl> foreignDecls;

public:
  SymbolIndex(unsigned short modId, int version, std::vector<DeclSymbol> &&decls,
              std::vector<Symbol> &&symbols, std::vector<ForeignDecl> &&foreignDecls)
      : modId(modId), version(version), decls(std::move(decls)), symbols(std::move(symbols)),
        foreignDecls(std::move(foreignDecls)) {}

  unsigned short getModId() const { return this->modId; }

  int getVersion() const { return this->version; }

  const DeclSymbol *findDecl(unsigned int pos) const;

  const Symbol *findSymbol(unsigned int pos) const;

  const ForeignDecl *findForeignDecl(SymbolRequest request) const;

  const std::vector<DeclSymbol> &getDecls() const { return this->decls; }

  const std::vector<Symbol> &getSymbols() const { return this->symbols; }

  const std::vector<ForeignDecl> &getForeignDecls() const { return this->foreignDecls; }

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

  const DeclSymbol *findDecl(SymbolRequest req) const {
    if (auto *index = this->find(req.modId); index) {
      return index->findDecl(req.pos);
    }
    return nullptr;
  }

  auto begin() const { return this->indexes.cbegin(); }

  auto end() const { return this->indexes.cend(); }
};

bool findDeclaration(const SymbolIndexes &indexes, SymbolRequest request,
                     const std::function<void(unsigned short, const DeclSymbol &)> &consumer);

bool findAllReferences(const SymbolIndexes &indexes, SymbolRequest request,
                       const std::function<void(const SymbolRef &)> &cosumer);

} // namespace ydsh::lsp

namespace ydsh {

template <>
struct allow_enum_bitop<lsp::DeclSymbol::Attr> : std::true_type {};

} // namespace ydsh

#endif // YDSH_TOOLS_ANALYZER_INDEX_H
