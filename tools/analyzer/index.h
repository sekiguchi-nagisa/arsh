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

  unsigned short getSize() const { return this->size; }

  bool operator<(SymbolRef o) const {
    return this->getModId() < o.getModId() ||
           (!(o.getModId() < this->getModId()) && this->getPos() < o.getPos());
  }
};

// for symbol lookup
struct SymbolRequest {
  unsigned short modId;
  unsigned int pos;
};

class DeclBase {
private:
  unsigned int pos;
  unsigned short size;
  unsigned short modId;
  FlexBuffer<SymbolRef> refs;

protected:
  DeclBase(unsigned int pos, unsigned short size, unsigned short modId)
      : pos(pos), size(size), modId(modId) {}

public:
  unsigned int getPos() const { return this->pos; }

  unsigned short getSize() const { return this->size; }

  unsigned short getModId() const { return this->modId; }

  Token getToken() const {
    return Token{
        .pos = this->pos,
        .size = this->size,
    };
  }

  const FlexBuffer<SymbolRef> &getRefs() const { return this->refs; }

  void addRef(SymbolRef ref);

  SymbolRef toRef() const { return {this->pos, this->size, this->modId}; }

  bool operator<(const DeclBase &o) const {
    return this->getModId() < o.getModId() ||
           (!(o.getModId() < this->getModId()) && this->getToken().endPos() < o.getPos());
  }
};

class DeclSymbol : public DeclBase {
public:
  enum class Kind : unsigned char {
    VAR,
    LET,
    IMPORT_ENV,
    EXPORT_ENV,
    CONST,
    MOD_CONST,
    FUNC,
    CONSTRUCTOR,
    METHOD,
    BUILTIN_CMD,
    CMD,
    TYPE_ALIAS,
    ERROR_TYPE_DEF,
    MOD, // for named import
  };

  enum class Attr : unsigned char {
    GLOBAL = 1u << 0u,
    PUBLIC = 1u << 1u,
    BUILTIN = 1u << 2u, // for builtin type field/method
    MEMBER = 1u << 3u,
  };

private:
  Kind kind;
  Attr attr;
  CStrPtr mangledName;
  CStrPtr info; // hover information

public:
  static Optional<DeclSymbol> create(Kind kind, Attr attr, Token token, unsigned short modId,
                                     const std::string &name, const char *info = nullptr) {
    if (token.size > UINT16_MAX) {
      return {};
    }
    return DeclSymbol(kind, attr, token.pos, static_cast<unsigned short>(token.size), modId, name,
                      info != nullptr ? info : "(dummy)");
  }

  DeclSymbol(Kind kind, Attr attr, unsigned int pos, unsigned short size, unsigned short mod,
             const std::string &name, const char *info)
      : DeclBase(pos, size, mod), kind(kind), attr(attr),
        mangledName(CStrPtr(strdup(name.c_str()))), info(CStrPtr(strdup(info))) {}

  Kind getKind() const { return this->kind; }

  Attr getAttr() const { return this->attr; }

  StringRef getMangledName() const { return this->mangledName.get(); }

  StringRef getInfo() const { return this->info.get(); }

  /**
   * for Kind::MOD
   * @return
   * if kind is not Kind::MOD, return {0, false}
   */
  std::pair<unsigned short, bool> getInfoAsModId() const;

  struct Compare {
    bool operator()(const DeclSymbol &x, unsigned int y) const { return x.getToken().endPos() < y; }

    bool operator()(unsigned int x, const DeclSymbol &y) const { return x < y.getToken().pos; }
  };

  static std::string mangle(Kind k, StringRef name) { return mangle("", k, name); }

  static std::string mangle(StringRef recvTypeName, Kind k, StringRef name);

  static std::string demangle(Kind k, StringRef mangledName) {
    return demangle(k, Attr(), mangledName);
  }

  static std::string demangle(Kind k, Attr a, StringRef mangledName);

  static bool mayBeMemberName(StringRef ref) { return ref.contains('@'); }

  static bool isVarName(Kind k) {
    switch (k) {
    case Kind::BUILTIN_CMD:
    case Kind::CMD:
    case Kind::TYPE_ALIAS:
    case Kind::ERROR_TYPE_DEF:
    case Kind::CONSTRUCTOR:
      //    case Kind::METHOD:  //FIXME:
      return false;
    default:
      return true;
    }
  }

  static const char *getVarDeclPrefix(Kind k) {
    switch (k) {
    case DeclSymbol::Kind::VAR:
      return "var";
    case DeclSymbol::Kind::LET:
      return "let";
    case DeclSymbol::Kind::EXPORT_ENV:
      return "export-env";
    case DeclSymbol::Kind::IMPORT_ENV:
      return "import-env";
    default:
      return "";
    }
  }
};

class Symbol {
private:
  unsigned int pos;
  unsigned short size;
  unsigned short declModId;
  unsigned int declPos;

public:
  static Optional<Symbol> create(Token token, const DeclBase &decl) {
    if (token.size > UINT16_MAX) {
      return {};
    }
    return Symbol(token.pos, static_cast<unsigned short>(token.size), decl.getModId(),
                  decl.getPos());
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

  unsigned int getDeclPos() const { return this->declPos; }

  struct Compare {
    bool operator()(const Symbol &x, unsigned int y) const { return x.getToken().endPos() < y; }

    bool operator()(unsigned int x, const Symbol &y) const { return x < y.getToken().pos; }
  };
};

class ForeignDecl : public DeclBase {
public:
  explicit ForeignDecl(SymbolRef declRef)
      : DeclBase(declRef.getPos(), declRef.getSize(), declRef.getModId()) {}

  explicit ForeignDecl(const DeclSymbol &decl) : ForeignDecl(decl.toRef()) {}

  struct Compare {
    bool operator()(const ForeignDecl &x, const SymbolRequest &y) const {
      return x.getModId() < y.modId || (!(y.modId < x.getModId()) && x.getToken().endPos() < y.pos);
    }

    bool operator()(const SymbolRequest &x, const ForeignDecl &y) const {
      return x.modId < y.getModId() || (!(y.getModId() < x.modId) && x.pos < y.getPos());
    }
  };
};

class SymbolIndex;
using SymbolIndexPtr = std::shared_ptr<const SymbolIndex>;

class SymbolIndex {
private:
  unsigned short modId;
  int version;
  std::vector<DeclSymbol> decls;
  std::vector<Symbol> symbols;
  std::vector<ForeignDecl> foreignDecls;
  std::unordered_map<std::string, SymbolRef> globals; // for global decl reference

public:
  SymbolIndex(unsigned short modId, int version, std::vector<DeclSymbol> &&decls,
              std::vector<Symbol> &&symbols, std::vector<ForeignDecl> &&foreignDecls,
              std::unordered_map<std::string, SymbolRef> &&globals)
      : modId(modId), version(version), decls(std::move(decls)), symbols(std::move(symbols)),
        foreignDecls(std::move(foreignDecls)), globals(std::move(globals)) {}

  unsigned short getModId() const { return this->modId; }

  int getVersion() const { return this->version; }

  const DeclSymbol *findDecl(unsigned int pos) const;

  const Symbol *findSymbol(unsigned int pos) const;

  const ForeignDecl *findForeignDecl(SymbolRequest request) const;

  const SymbolRef *findGlobal(const std::string &mangledName) const;

  const std::vector<DeclSymbol> &getDecls() const { return this->decls; }

  const std::vector<Symbol> &getSymbols() const { return this->symbols; }

  struct Compare {
    bool operator()(const SymbolIndexPtr &x, unsigned short id) const { return x->getModId() < id; }

    bool operator()(unsigned short id, const SymbolIndexPtr &y) const { return id < y->getModId(); }

    bool operator()(const SymbolIndexPtr &x, const SymbolIndexPtr &y) const {
      return x->getModId() < y->getModId();
    }
  };
};

class SymbolIndexes {
private:
  std::vector<SymbolIndexPtr> indexes;

public:
  void add(SymbolIndexPtr index);

  SymbolIndexPtr find(unsigned short modId) const;

  void remove(unsigned short id);

  const DeclSymbol *findDecl(SymbolRequest req) const {
    if (auto index = this->find(req.modId)) {
      return index->findDecl(req.pos);
    }
    return nullptr;
  }

  auto begin() const { return this->indexes.cbegin(); }

  auto end() const { return this->indexes.cend(); }
};

struct FindDeclResult {
  const DeclSymbol &decl;
  const Symbol &request;
};

bool findDeclaration(const SymbolIndexes &indexes, SymbolRequest request,
                     const std::function<void(const FindDeclResult &)> &consumer);

struct FindRefsResult {
  const SymbolRef &symbol;
  const DeclSymbol &request;
};

bool findAllReferences(const SymbolIndexes &indexes, SymbolRequest request,
                       const std::function<void(const FindRefsResult &)> &consumer);

} // namespace ydsh::lsp

namespace ydsh {

template <>
struct allow_enum_bitop<lsp::DeclSymbol::Attr> : std::true_type {};

} // namespace ydsh

#endif // YDSH_TOOLS_ANALYZER_INDEX_H
