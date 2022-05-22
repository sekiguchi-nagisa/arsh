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

#include "analyzer.h"
#include "index.h"

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

  const DeclSymbol *addDecl(const NameInfo &info, const DSType &type,
                            DeclSymbol::Kind kind = DeclSymbol::Kind::VAR);

  const DeclSymbol *addDecl(const NameInfo &info, DeclSymbol::Kind kind, const char *hover) {
    return this->addDeclImpl(nullptr, info, kind, hover, DeclInsertOp::NORMAL);
  }

  const Symbol *addSymbol(const NameInfo &info, DeclSymbol::Kind kind, const HandlePtr &hd) {
    return this->addSymbolImpl(nullptr, info, kind, hd.get());
  }

  bool addThis(const NameInfo &info, const HandlePtr &hd);

  bool addMember(const DSType &recv, const NameInfo &nameInfo, DeclSymbol::Kind kind,
                 const Handle &handle);

  bool addMember(const MethodHandle &handle, const NameInfo &nameInfo) {
    return this->addMember(this->getPool().get(handle.getRecvTypeId()), nameInfo,
                           DeclSymbol::Kind::METHOD, handle);
  }

  const DeclSymbol *addMemberDecl(const DSType &recv, const NameInfo &nameInfo, const DSType &type,
                                  DeclSymbol::Kind kind);

  const DeclSymbol *addMemberDecl(const DSType &recv, const NameInfo &nameInfo,
                                  DeclSymbol::Kind kind, const char *info) {
    if (recv.isUnresolved()) {
      return nullptr;
    }
    return this->addDeclImpl(&recv, nameInfo, kind, info, DeclInsertOp::MEMBER);
  }

  const DeclSymbol *findDecl(const Symbol &symbol) const;

  void addLink(Token token, unsigned int short targetModId, const std::string &link) {
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
                                const char *hover, DeclInsertOp op);

  const Symbol *addSymbolImpl(const DSType *recv, const NameInfo &nameInfo, DeclSymbol::Kind kind,
                              const Handle *handle);

  /**
   * create new DeclSymbol and insert to decl list
   * @param k
   * @param attr
   * @param token
   * @param mangledName
   * @param info
   * @param op
   * @return
   */
  DeclSymbol *insertNewDecl(DeclSymbol::Kind k, DeclSymbol::Attr attr, Token token,
                            const std::string &mangledName, const char *info, DeclInsertOp op);

  /**
   * create new Symbol from DeclVBase and insert to symbol list
   * @param token
   * @param decl
   * @return
   */
  const Symbol *insertNewSymbol(Token token, const DeclBase *decl);
};

class SymbolIndexer : protected ydsh::NodeVisitor, public NodeConsumer {
private:
  const SysConfig &sysConfig;
  SymbolIndexes &indexes;
  std::vector<IndexBuilder> builders;
  int visitingDepth{0};

public:
  SymbolIndexer(const SysConfig &config, SymbolIndexes &indexes)
      : sysConfig(config), indexes(indexes) {}

  ~SymbolIndexer() override = default;

  bool enterModule(unsigned short modId, int version,
                   const std::shared_ptr<TypePool> &pool) override;
  bool exitModule(std::unique_ptr<Node> &&node) override;
  bool consume(std::unique_ptr<Node> &&node) override;

protected:
  void visitTypeNode(TypeNode &node) override;
  void visitNumberNode(NumberNode &node) override;
  void visitStringNode(StringNode &node) override;
  void visitStringExprNode(StringExprNode &node) override;
  void visitRegexNode(RegexNode &node) override;
  void visitArrayNode(ArrayNode &node) override;
  void visitMapNode(MapNode &node) override;
  void visitTupleNode(TupleNode &node) override;
  void visitVarNode(VarNode &node) override;
  void visitAccessNode(AccessNode &node) override;
  void visitTypeOpNode(TypeOpNode &node) override;
  void visitUnaryOpNode(UnaryOpNode &node) override;
  void visitBinaryOpNode(BinaryOpNode &node) override;
  void visitArgsNode(ArgsNode &node) override;
  void visitApplyNode(ApplyNode &node) override;
  void visitEmbedNode(EmbedNode &node) override;
  void visitNewNode(NewNode &node) override;
  void visitForkNode(ForkNode &node) override;
  void visitCmdNode(CmdNode &node) override;
  void visitCmdArgNode(CmdArgNode &node) override;
  void visitArgArrayNode(ArgArrayNode &node) override;
  void visitRedirNode(RedirNode &node) override;
  void visitWildCardNode(WildCardNode &node) override;
  void visitPipelineNode(PipelineNode &node) override;
  void visitWithNode(WithNode &node) override;
  void visitAssertNode(AssertNode &node) override;
  void visitBlockNode(BlockNode &node) override;
  void visitTypeDefNode(TypeDefNode &node) override;
  void visitDeferNode(DeferNode &node) override;
  void visitLoopNode(LoopNode &node) override;
  void visitIfNode(IfNode &node) override;
  void visitCaseNode(CaseNode &node) override;
  void visitArmNode(ArmNode &node) override;
  void visitJumpNode(JumpNode &node) override;
  void visitCatchNode(CatchNode &node) override;
  void visitTryNode(TryNode &node) override;
  void visitVarDeclNode(VarDeclNode &node) override;
  void visitAssignNode(AssignNode &node) override;
  void visitElementSelfAssignNode(ElementSelfAssignNode &node) override;
  void visitPrefixAssignNode(PrefixAssignNode &node) override;
  void visitFunctionNode(FunctionNode &node) override;
  void visitUserDefinedCmdNode(UserDefinedCmdNode &node) override;
  void visitSourceNode(SourceNode &node) override;
  void visitSourceListNode(SourceListNode &node) override;
  void visitCodeCompNode(CodeCompNode &node) override;
  void visitErrorNode(ErrorNode &node) override;
  void visitEmptyNode(EmptyNode &node) override;
  void visit(Node &node) override;

private:
  template <typename T, enable_when<std::is_convertible<T *, Node *>::value> = nullptr>
  void visit(T *node) {
    if (node) {
      this->visit(*node);
    }
  }

  template <typename T, enable_when<std::is_convertible<T *, Node *>::value> = nullptr>
  void visit(const std::unique_ptr<T> &node) {
    if (node) {
      this->visit(*node);
    }
  }

  template <typename T, enable_when<std::is_convertible<T *, Node *>::value> = nullptr>
  void visitEach(const std::vector<std::unique_ptr<T>> &nodes) {
    for (const auto &item : nodes) {
      this->visit(item);
    }
  }

  void visitBlockWithCurrentScope(const BlockNode &blockNode) {
    this->visitEach(blockNode.getNodes());
  }

  IndexBuilder &builder() { return this->builders.back(); }

  bool isTopLevel() const { return this->visitingDepth == 1; }

  void addBuiltinSymbols();
};

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_INDEXER_H
