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
  const ModId modId;
  const int version;
  unsigned int scopeIdCount{0};
  std::vector<DeclSymbol> decls;
  std::vector<Symbol> symbols;
  std::vector<ForeignDecl> foreign;
  std::vector<std::pair<SymbolRef, IndexLink>> links;

  class ScopeEntry : public RefCount<ScopeEntry> {
  private:
    /**
     * mangled name => DeclSymbol reference
     */
    std::unordered_map<std::string, SymbolRef> map;

    const DSType *resolvedType{nullptr}; // for constructor

  public:
    const IntrusivePtr<ScopeEntry> parent;

    const ScopeKind kind;

    const unsigned int scopeId;

    const unsigned int scopeDepth;

    ScopeEntry(const IntrusivePtr<ScopeEntry> &parent, ScopeKind kind, unsigned int scopeId,
               const DSType *type)
        : resolvedType(type), parent(parent), kind(kind), scopeId(scopeId),
          scopeDepth(this->parent ? this->parent->scopeDepth + 1 : 0) {}

    explicit ScopeEntry(unsigned int scopeId)
        : ScopeEntry(nullptr, ScopeKind::GLOBAL, scopeId, nullptr) {}

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

    /**
     * low-level api, normally unused
     * @param name
     * must be proper mangled name
     * @param decl
     * @return
     * if successfully insert, return true
     */
    bool addDeclWithSpecifiedName(std::string &&name, const DeclSymbol &decl) {
      return this->map.emplace(std::move(name), decl.toRef()).second;
    }

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

    DeclSymbol::ScopeInfo getScopeInfo() const {
      return {
          .id = this->scopeId,
          .depth = this->scopeDepth,
      };
    }

    auto take() && { return std::move(this->map); }
  };

  IntrusivePtr<ScopeEntry> scope;

  std::shared_ptr<TypePool> pool;

  const SymbolIndexes &indexes;

public:
  IndexBuilder(ModId modId, int version, std::shared_ptr<TypePool> pool,
               const SymbolIndexes &indexes)
      : modId(modId), version(version), scope(IntrusivePtr<ScopeEntry>::create(this->scopeIdCount)),
        pool(std::move(pool)), indexes(indexes) {}

  SymbolIndex build() && {
    return {this->modId,
            this->version,
            std::move(this->decls),
            std::move(this->symbols),
            std::move(this->foreign),
            std::move(*this->scope).take(),
            std::move(this->links)};
  }

  const TypePool &getPool() const { return *this->pool; }

  const ScopeEntry &curScope() const { return *this->scope; }

  auto intoScope(ScopeKind kind, const DSType *type = nullptr) {
    this->scope = IntrusivePtr<ScopeEntry>::create(this->scope, kind, ++this->scopeIdCount, type);
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

  void addLink(Token token, ModId targetModId, ImportedModKind modKind, const std::string &link) {
    auto ref = SymbolRef::create(token, targetModId);
    if (ref.hasValue()) {
      IndexLink::ImportAttr importAttr{};
      if (hasFlag(modKind, ImportedModKind::GLOBAL)) {
        setFlag(importAttr, IndexLink::ImportAttr::GLOBAL);
      }
      if (hasFlag(modKind, ImportedModKind::INLINED)) {
        setFlag(importAttr, IndexLink::ImportAttr::INLINED);
      }

      this->links.emplace_back(ref.unwrap(), IndexLink(targetModId, importAttr, std::string(link)));
    }
  }

  void addHereDocStartEnd(const NameInfo &start, Token end);

private:
  const SymbolRef *lookup(const std::string &mangledName, DeclSymbol::Kind kind,
                          const Handle *handle) const;

  enum class DeclInsertOp {
    NORMAL,
    BUILTIN,
    MEMBER,
    NONE, // no scope check
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

  bool enterModule(ModId modId, int version, const std::shared_ptr<TypePool> &pool) override;
  bool exitModule(const std::unique_ptr<Node> &node) override;

protected:
  void visitBase(BaseTypeNode &node) override;
  void visitQualified(QualifiedTypeNode &node) override;

  void visitVarNode(VarNode &node) override;
  void visitAccessNode(AccessNode &node) override;
  void visitApplyNode(ApplyNode &node) override;
  void visitCmdNode(CmdNode &node) override;
  void visitRedirNode(RedirNode &node) override;
  void visitBlockNode(BlockNode &node) override;
  void visitTypeDefNode(TypeDefNode &node) override;
  void visitLoopNode(LoopNode &node) override;
  void visitIfNode(IfNode &node) override;
  void visitCatchNode(CatchNode &node) override;
  void visitVarDeclNode(VarDeclNode &node) override;
  void visitPrefixAssignNode(PrefixAssignNode &node) override;
  void visitFunctionNode(FunctionNode &node) override;
  void visitUserDefinedCmdNode(UserDefinedCmdNode &node) override;
  void visitFuncListNode(FuncListNode &node) override;
  void visitSourceNode(SourceNode &node) override;

private:
  void visitBlockWithCurrentScope(const BlockNode &blockNode) {
    this->visitEach(blockNode.getNodes());
  }

  enum class FuncVisitOp : unsigned int {
    VISIT_NAME = 1u << 0u,
    VISIT_BODY = 1u << 1u,
  };

  void visitFunctionImpl(FunctionNode &node, FuncVisitOp op);

  void visitUserDefinedCmdImpl(UserDefinedCmdNode &node, FuncVisitOp op);

  IndexBuilder &builder() { return this->builders.back(); }

  void addBuiltinSymbols();
};

} // namespace ydsh::lsp

namespace ydsh {

template <>
struct allow_enum_bitop<lsp::SymbolIndexer::FuncVisitOp> : std::true_type {};

} // namespace ydsh

#endif // YDSH_TOOLS_ANALYZER_INDEXER_H
