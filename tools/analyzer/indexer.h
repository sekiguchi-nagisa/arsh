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

#ifndef YDSH_TOOLS_ANALYZER_INDEXER_H
#define YDSH_TOOLS_ANALYZER_INDEXER_H

#include <sysconfig.h>

#include "index.h"
#include "pass.h"

namespace ydsh::lsp {

enum class ScopeKind {
  GLOBAL,
  FUNC,
  CONSTRUCTOR,
  METHOD,
  BLOCK,
};

class IndexBuilder {
private:
  const unsigned short modId;
  const int version;
  std::vector<DeclSymbol> decls;
  std::vector<Symbol> symbols;
  std::vector<ForeignDecl> foreigns;
  std::vector<std::pair<SymbolRef, std::string>> links;

  class ScopeEntry : public RefCount<ScopeEntry> {
  private:
    /**
     * mangled name => DeclSymbol reference
     */
    std::unordered_map<std::string, SymbolRef> map; // FIXME: replace string with StringRef

    const DSType *resolvedType{nullptr}; // for constructor

  public:
    const IntrusivePtr<ScopeEntry> parent;

    const ScopeKind kind;

    ScopeEntry(const IntrusivePtr<ScopeEntry> &parent, ScopeKind kind, const DSType *type)
        : resolvedType(type), parent(parent), kind(kind) {}

    ScopeEntry() : ScopeEntry(nullptr, ScopeKind::GLOBAL, nullptr) {}

    bool isGlobal() const { return !this->parent; }

    bool isConstructor() const { return this->kind == ScopeKind::CONSTRUCTOR; }

    const ScopeEntry *findMethodScope() const {
      for (auto *cur = this; cur != nullptr; cur = cur->parent.get()) {
        if (cur->kind == ScopeKind::METHOD) {
          return cur;
        }
      }
      return nullptr;
    }

    const DSType *getResolvedType() const { return this->resolvedType; }

    bool addDecl(const DeclSymbol &decl);

    const SymbolRef *find(const std::string &name) const {
      for (auto *cur = this; cur != nullptr; cur = cur->parent.get()) {
        auto iter = cur->map.find(name);
        if (iter != cur->map.end()) {
          return &iter->second;
        }
      }
      return nullptr;
    }

    auto take() && { return std::move(this->map); }
  };

  IntrusivePtr<ScopeEntry> scope;

  std::shared_ptr<TypePool> pool;

  const SymbolIndexes &indexes;

public:
  IndexBuilder(unsigned short modId, int version, std::shared_ptr<TypePool> pool,
               const SymbolIndexes &indexes)
      : modId(modId), version(version), scope(IntrusivePtr<ScopeEntry>::create()),
        pool(std::move(pool)), indexes(indexes) {}

  SymbolIndex build() && {
    return {this->modId,
            this->version,
            std::move(this->decls),
            std::move(this->symbols),
            std::move(this->foreigns),
            std::move(*this->scope).take(),
            std::move(this->links)};
  }

  const TypePool &getPool() const { return *this->pool; }

  const ScopeEntry &curScope() const { return *this->scope; }

  auto intoScope(ScopeKind kind, const DSType *type = nullptr) {
    this->scope = IntrusivePtr<ScopeEntry>::create(this->scope, kind, type);
    return finally([&] { this->scope = this->scope->parent; });
  }

  bool isGlobal() const { return this->scope->isGlobal(); }

  bool isReceiver(const std::string &varName, const Handle &handle) const {
    return this->curScope().findMethodScope() && varName == VAR_THIS && handle.getIndex() == 0;
  }

  const DeclSymbol *addDecl(const NameInfo &info, const DSType &type, Token token,
                            DeclSymbol::Kind kind = DeclSymbol::Kind::VAR);

  const DeclSymbol *addDecl(const NameInfo &info, DeclSymbol::Kind kind, const char *hover,
                            Token token) {
    return this->addDeclImpl(nullptr, info, kind, hover, token, DeclInsertOp::NORMAL);
  }

  const Symbol *addSymbol(const NameInfo &info, DeclSymbol::Kind kind, const HandlePtr &hd) {
    return this->addSymbolImpl(nullptr, info, kind, hd.get());
  }

  bool addThis(const NameInfo &info, const HandlePtr &hd);

  bool addMember(const DSType &recv, const NameInfo &nameInfo, DeclSymbol::Kind kind,
                 const Handle &handle, Token token);

  bool addMember(const MethodHandle &handle, const NameInfo &nameInfo, Token token) {
    return this->addMember(this->getPool().get(handle.getRecvTypeId()), nameInfo,
                           DeclSymbol::Kind::METHOD, handle, token);
  }

  const DeclSymbol *addMemberDecl(const DSType &recv, const NameInfo &nameInfo, const DSType &type,
                                  DeclSymbol::Kind kind, Token token);

  const DeclSymbol *addMemberDecl(const DSType &recv, const NameInfo &nameInfo,
                                  DeclSymbol::Kind kind, const char *info, Token token) {
    if (recv.isUnresolved()) {
      return nullptr;
    }
    return this->addDeclImpl(&recv, nameInfo, kind, info, token, DeclInsertOp::MEMBER);
  }

  const DeclSymbol *findDecl(const Symbol &symbol) const;

  void addLink(Token token, unsigned short targetModId, const std::string &link) {
    auto ref = SymbolRef::create(token, targetModId);
    if (ref.hasValue()) {
      this->links.emplace_back(ref.unwrap(), link);
    }
  }

private:
  const SymbolRef *lookup(const std::string &mangledName, DeclSymbol::Kind kind,
                          const Handle *handle) const;

  enum class DeclInsertOp {
    NORMAL,
    BUILTIN,
    MEMBER,
  };

  const DeclSymbol *addDeclImpl(const DSType *recv, const NameInfo &info, DeclSymbol::Kind kind,
                                const char *hover, Token body, DeclInsertOp op);

  const Symbol *addSymbolImpl(const DSType *recv, const NameInfo &nameInfo, DeclSymbol::Kind kind,
                              const Handle *handle);

  /**
   * create new DeclSymbol and insert to decl list
   * @param k
   * @param attr
   * @param name
   * @param info
   * @param body
   * @param op
   * @return
   */
  DeclSymbol *insertNewDecl(DeclSymbol::Kind k, DeclSymbol::Attr attr, DeclSymbol::Name &&name,
                            const char *info, Token body, DeclInsertOp op);

  /**
   * create new Symbol from DeclVBase and insert to symbol list
   * @param token
   * @param decl
   * @return
   */
  const Symbol *insertNewSymbol(Token token, const DeclBase *decl);
};

class SymbolIndexer : public NodePass {
private:
  const SysConfig &sysConfig;
  SymbolIndexes &indexes;
  std::vector<IndexBuilder> builders;

public:
  SymbolIndexer(const SysConfig &config, SymbolIndexes &indexes)
      : sysConfig(config), indexes(indexes) {}

  ~SymbolIndexer() override = default;

  bool enterModule(unsigned short modId, int version,
                   const std::shared_ptr<TypePool> &pool) override;
  bool exitModule(const std::unique_ptr<Node> &node) override;

protected:
  void visitBase(BaseTypeNode &node) override;
  void visitQualified(QualifiedTypeNode &node) override;

  void visitVarNode(VarNode &node) override;
  void visitAccessNode(AccessNode &node) override;
  void visitApplyNode(ApplyNode &node) override;
  void visitCmdNode(CmdNode &node) override;
  void visitBlockNode(BlockNode &node) override;
  void visitTypeDefNode(TypeDefNode &node) override;
  void visitLoopNode(LoopNode &node) override;
  void visitCatchNode(CatchNode &node) override;
  void visitVarDeclNode(VarDeclNode &node) override;
  void visitPrefixAssignNode(PrefixAssignNode &node) override;
  void visitFunctionNode(FunctionNode &node) override;
  void visitUserDefinedCmdNode(UserDefinedCmdNode &node) override;
  void visitSourceNode(SourceNode &node) override;

private:
  void visitBlockWithCurrentScope(const BlockNode &blockNode) {
    this->visitEach(blockNode.getNodes());
  }

  IndexBuilder &builder() { return this->builders.back(); }

  void addBuiltinSymbols();
};

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_INDEXER_H
