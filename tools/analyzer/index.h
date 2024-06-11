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

#ifndef ARSH_TOOLS_ANALYZER_INDEX_H
#define ARSH_TOOLS_ANALYZER_INDEX_H

#include <functional>
#include <vector>

#include <constant.h>

#include <misc/buffer.hpp>
#include <misc/enum_util.hpp>
#include <misc/resource.hpp>
#include <misc/result.hpp>
#include <misc/string_ref.hpp>
#include <misc/token.hpp>

namespace arsh::lsp {

class SymbolRef {
private:
  unsigned int pos;
  unsigned short size;
  ModId modId;

public:
  static Optional<SymbolRef> create(Token token, ModId modId) {
    if (token.size > UINT16_MAX) {
      return {};
    }
    return SymbolRef(token.pos, static_cast<unsigned short>(token.size), modId);
  }

  SymbolRef(unsigned int pos, unsigned short size, ModId modId)
      : pos(pos), size(size), modId(modId) {}

  unsigned int getPos() const { return this->pos; }

  Token getToken() const {
    return Token{
        .pos = this->pos,
        .size = this->size,
    };
  }

  ModId getModId() const { return this->modId; }

  unsigned short getSize() const { return this->size; }

  bool operator<(SymbolRef o) const {
    return this->getModId() < o.getModId() ||
           (!(o.getModId() < this->getModId()) && this->getPos() < o.getPos());
  }
};

// for symbol lookup
struct SymbolRequest {
  ModId modId;
  unsigned int pos;
};

class DeclBase {
private:
  unsigned int pos;
  unsigned short size;
  ModId modId;
  FlexBuffer<SymbolRef> refs;

protected:
  DeclBase(unsigned int pos, unsigned short size, ModId modId)
      : pos(pos), size(size), modId(modId) {}

public:
  unsigned int getPos() const { return this->pos; }

  unsigned short getSize() const { return this->size; }

  ModId getModId() const { return this->modId; }

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

  bool operator==(const DeclBase &o) const {
    return this->getModId() == o.getModId() && this->getToken() == o.getToken();
  }

  bool operator!=(const DeclBase &o) const { return !(*this == o); }
};

class DeclSymbol : public DeclBase {
public:
  enum class Kind : unsigned char {
    VAR,
    LET,
    IMPORT_ENV,
    EXPORT_ENV,
    PREFIX_ENV, // for prefix env assignment (ENV=value)
    THIS,
    CONST,
    MOD_CONST,
    FUNC,
    CONSTRUCTOR,
    METHOD,
    GENERIC_METHOD,
    BUILTIN_CMD,
    CMD,
    BUILTIN_TYPE,
    TYPE_ALIAS,
    ERROR_TYPE_DEF,
    MOD, // for named import
    HERE_START,
  };

  enum class Attr : unsigned char {
    GLOBAL = 1u << 0u,
    PUBLIC = 1u << 1u,
    BUILTIN = 1u << 2u, // for builtin type field/method
    MEMBER = 1u << 3u,
  };

  struct Name {
    Token token;
    CStrPtr name;
  };

private:
  Kind kind;
  Attr attr;
  unsigned int scopeId;
  CStrPtr mangledName;
  CStrPtr info; // hover information
  Token body;

public:
  static Optional<DeclSymbol> create(Kind kind, Attr attr, Name &&name, ModId modId,
                                     const char *info, Token body, unsigned int scopeId) {
    if (name.token.size > UINT16_MAX) {
      return {};
    }
    return DeclSymbol(kind, attr, name.token.pos, static_cast<unsigned short>(name.token.size),
                      modId, std::move(name.name), info != nullptr ? info : "(dummy)", body,
                      scopeId);
  }

  DeclSymbol(Kind kind, Attr attr, unsigned int pos, unsigned short size, ModId mod, CStrPtr &&name,
             const char *info, Token body, unsigned int scopeId)
      : DeclBase(pos, size, mod), kind(kind), attr(attr), scopeId(scopeId),
        mangledName(std::move(name)), info(CStrPtr(strdup(info))), body(body) {}

  Kind getKind() const { return this->kind; }

  Attr getAttr() const { return this->attr; }

  bool is(Kind k) const { return this->getKind() == k; }

  bool has(Attr a) const;

  unsigned int getScopeId() const { return this->scopeId; }

  StringRef getMangledName() const { return this->mangledName.get(); }

  StringRef getInfo() const { return this->info.get(); }

  /**
   * for Kind::MOD
   * @return
   * if kind is not Kind::MOD, return {0, false}
   */
  std::pair<ModId, bool> getInfoAsModId() const;

  Token getBody() const { return this->body; }

  std::string toDemangledName() const {
    return demangle(this->getKind(), this->getAttr(), this->getMangledName());
  }

  struct Compare {
    bool operator()(const DeclSymbol &x, unsigned int y) const {
      return x.getToken().endPos() - 1 < y;
    }

    bool operator()(unsigned int x, const DeclSymbol &y) const { return x < y.getToken().pos; }
  };

  static std::string mangle(Kind k, StringRef name) { return mangle("", k, name); }

  static std::string mangle(StringRef recvTypeName, Kind k, StringRef name);

  static std::string demangle(Kind k, StringRef mangledName) {
    return demangle(k, Attr(), mangledName);
  }

  static std::string demangle(Kind k, Attr a, StringRef mangledName);

  /**
   *
   * @param k
   * @param a
   * @param mangledName
   * @return
   * (recvTypeName, symbolName)
   */
  static std::pair<StringRef, StringRef> demangleWithRecv(Kind k, Attr a, StringRef mangledName);

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
    case Kind::VAR:
      return "var";
    case Kind::LET:
    case Kind::THIS:
      return "let";
    case Kind::EXPORT_ENV:
    case Kind::PREFIX_ENV:
      return "exportenv";
    case Kind::IMPORT_ENV:
      return "importenv";
    default:
      return "";
    }
  }
};

class Symbol {
private:
  unsigned int pos;
  unsigned short size;
  ModId declModId;
  unsigned int declPos;

public:
  static Optional<Symbol> create(Token token, const DeclBase &decl) {
    if (token.size > UINT16_MAX) {
      return {};
    }
    return Symbol(token.pos, static_cast<unsigned short>(token.size), decl.getModId(),
                  decl.getPos());
  }

  Symbol(unsigned int pos, unsigned short size, ModId declModId, unsigned int declPos)
      : pos(pos), size(size), declModId(declModId), declPos(declPos) {}

  unsigned int getPos() const { return this->pos; }

  Token getToken() const {
    return Token{
        .pos = this->pos,
        .size = this->size,
    };
  }

  ModId getDeclModId() const { return this->declModId; }

  unsigned int getDeclPos() const { return this->declPos; }

  struct Compare {
    bool operator()(const Symbol &x, unsigned int y) const { return x.getToken().endPos() - 1 < y; }

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
      return x.getModId() < y.modId ||
             (!(y.modId < x.getModId()) && x.getToken().endPos() - 1 < y.pos);
    }

    bool operator()(const SymbolRequest &x, const ForeignDecl &y) const {
      return x.modId < y.getModId() || (!(y.getModId() < x.modId) && x.pos < y.getPos());
    }
  };
};

class IndexLink {
public:
  enum class ImportAttr : unsigned char {
    GLOBAL = 1u << 0u,
    INLINED = 1u << 1u,
  };

private:
  ModId modId{0};
  ImportAttr importAttr{};
  std::string pathName;

public:
  IndexLink(ModId modId, ImportAttr importKind, std::string &&pathName)
      : modId(modId), importAttr(importKind), pathName(std::move(pathName)) {}

  ModId getModId() const { return this->modId; }

  ImportAttr getImportAttr() const { return this->importAttr; }

  const std::string &getPathName() const { return this->pathName; }
};

struct ScopeInterval {
  unsigned int beginPos; // inclusive
  unsigned int endPos;   // exclusive

  static ScopeInterval create(Token token) {
    return {
        .beginPos = token.pos,
        .endPos = token.endPos(),
    };
  }

  bool operator==(const ScopeInterval &o) const {
    return this->beginPos == o.beginPos && this->endPos == o.endPos;
  }

  bool operator!=(const ScopeInterval &o) const { return !(*this == o); }

  bool isIncluding(const ScopeInterval &o) const {
    return this->beginPos <= o.beginPos && o.endPos <= this->endPos;
  }

  bool isIncluding(const SymbolRef symbol) const {
    auto token = symbol.getToken();
    return this->beginPos <= token.pos && token.endPos() <= this->endPos;
  }
};

class PackedParamTypesMap {
private:
  std::unordered_map<unsigned int, unsigned int> ids;  // pos => id
  std::unordered_map<unsigned int, std::string> types; // id => packedParamTypes

public:
  bool addPackedParamTypes(unsigned int type, std::string &&packed);

  bool addSymbol(SymbolRef symbol, unsigned int typeId);

  const std::string *lookupByTypeId(unsigned int typeId) const;

  const std::string *lookupByPos(unsigned int pos) const;
};

class SymbolIndex;
using SymbolIndexPtr = std::shared_ptr<const SymbolIndex>;

class SymbolIndex {
private:
  ModId modId;
  int version;
  std::vector<DeclSymbol> decls;
  std::vector<Symbol> symbols;
  std::vector<ForeignDecl> foreignDecls;
  std::unordered_map<std::string, SymbolRef> globals; // for global decl reference
  std::vector<std::pair<SymbolRef, IndexLink>> links; // for importing module
  std::vector<ScopeInterval> scopes;
  PackedParamTypesMap packedParamTypesMap;        // for generic method hover
  std::unordered_set<std::string> externalCmdSet; // for user-defined command rename

public:
  SymbolIndex(ModId modId, int version, std::vector<DeclSymbol> &&decls,
              std::vector<Symbol> &&symbols, std::vector<ForeignDecl> &&foreignDecls,
              std::unordered_map<std::string, SymbolRef> &&globals,
              std::vector<std::pair<SymbolRef, IndexLink>> &&links,
              std::vector<ScopeInterval> &&scopes, PackedParamTypesMap &&packedParamTypesMap,
              std::unordered_set<std::string> &&externalCmdSet)
      : modId(modId), version(version), decls(std::move(decls)), symbols(std::move(symbols)),
        foreignDecls(std::move(foreignDecls)), globals(std::move(globals)), links(std::move(links)),
        scopes(std::move(scopes)), packedParamTypesMap(std::move(packedParamTypesMap)),
        externalCmdSet(std::move(externalCmdSet)) {}

  ModId getModId() const { return this->modId; }

  int getVersion() const { return this->version; }

  const DeclSymbol *findDecl(unsigned int pos) const;

  const Symbol *findSymbol(unsigned int pos) const;

  const ForeignDecl *findForeignDecl(SymbolRequest request) const;

  const SymbolRef *findGlobal(const std::string &mangledName) const;

  const std::vector<DeclSymbol> &getDecls() const { return this->decls; }

  const std::vector<Symbol> &getSymbols() const { return this->symbols; }

  const auto &getLinks() const { return this->links; }

  const auto &getScopes() const { return this->scopes; }

  const auto &getPackedParamTypesMap() const { return this->packedParamTypesMap; }

  const auto &getExternalCmdSet() const { return this->externalCmdSet; }

  struct Compare {
    bool operator()(const SymbolIndexPtr &x, ModId id) const { return x->getModId() < id; }

    bool operator()(ModId id, const SymbolIndexPtr &y) const { return id < y->getModId(); }

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

  SymbolIndexPtr find(ModId modId) const;

  void remove(ModId id);

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
  const DeclSymbol &decl; // found declaration
  const Symbol &request;  // resolved request symbol
};

bool findDeclaration(const SymbolIndexes &indexes, SymbolRequest request,
                     const std::function<void(const FindDeclResult &)> &consumer);

struct FindRefsResult {
  const SymbolRef symbol;    // found symbol (reference)
  const DeclSymbol &request; // resolved request declaration
};

unsigned int findAllReferences(const SymbolIndexes &indexes, const DeclSymbol &decl,
                               bool ignoreBuiltin,
                               const std::function<void(const FindRefsResult &)> &consumer);

inline bool findAllReferences(const SymbolIndexes &indexes, SymbolRequest request,
                              const std::function<void(const FindRefsResult &)> &consumer,
                              bool ignoreBuiltin = false) {
  const DeclSymbol *decl = nullptr;
  findDeclaration(indexes, request, [&decl](const FindDeclResult &r) { decl = &r.decl; });
  if (!decl) {
    return false;
  }
  return findAllReferences(indexes, *decl, ignoreBuiltin, consumer) > 0;
}

} // namespace arsh::lsp

namespace arsh {

template <>
struct allow_enum_bitop<lsp::DeclSymbol::Attr> : std::true_type {};

template <>
struct allow_enum_bitop<lsp::IndexLink::ImportAttr> : std::true_type {};

} // namespace arsh

#endif // ARSH_TOOLS_ANALYZER_INDEX_H
