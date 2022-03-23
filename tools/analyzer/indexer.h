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
  std::unordered_set<unsigned short> globallyImportedModIds;
  std::unordered_set<unsigned short> inlinedModIds;

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
  };

  IntrusivePtr<ScopeEntry> builtinCmd;

  IntrusivePtr<ScopeEntry> scope;

  /**
   * if indicate method, 2nd element is `true`
   */
  using Key = std::tuple<unsigned int, bool, std::string>;

  struct Hash {
    std::size_t operator()(const Key &key) const {
      auto &name = get<2>(key);
      auto hash = FNVHash::compute(name.c_str(), name.c_str() + name.size());
      union {
        char b[4];
        unsigned int i;
      } wrap;
      wrap.i = get<0>(key);
      for (auto b : wrap.b) {
        FNVHash::update(hash, b);
      }
      FNVHash::update(hash, get<1>(key) ? 1 : 0);
      return hash;
    }
  };

  class LazyMemberMap {
  public:
    using MapType = std::unordered_map<Key, SymbolRef, Hash>;

    const SymbolIndexes &indexes;

  private:
    /**
     * (type id, mangled name) => DeclSymbol reference
     */
    MapType map;

    std::unordered_set<unsigned short> cachedModIds;

    std::shared_ptr<TypePool> pool;

  public:
    LazyMemberMap(const SymbolIndexes &indexes, std::shared_ptr<TypePool> pool)
        : indexes(indexes), pool(std::move(pool)) {}

    /**
     *
     * @param recvType
     * @param memberName
     * must be mangled name
     * @param isMethod
     * @return
     */
    const SymbolRef *find(const DSType &recvType, const std::string &memberName, bool isMethod);

    const TypePool &getPool() const { return *this->pool; }

    void add(const DSType &recvType, const DeclSymbol &decl);

  private:
    void buildModCache(const DSType &recvType);

    const SymbolRef *findImpl(const DSType &recvType, const std::string &memberName,
                              bool isMethod) const {
      auto iter = this->map.find({recvType.typeId(), isMethod, memberName});
      if (iter != this->map.end()) {
        return &iter->second;
      }
      return nullptr;
    }
  };

  LazyMemberMap memberMap;

public:
  IndexBuilder(unsigned short modId, int version, std::shared_ptr<TypePool> pool,
               const SymbolIndexes &indexes)
      : modId(modId), version(version), builtinCmd(IntrusivePtr<ScopeEntry>::create()),
        scope(IntrusivePtr<ScopeEntry>::create()), memberMap(indexes, std::move(pool)) {}

  SymbolIndex build() && {
    FlexBuffer<unsigned short> inlinedModIdList;
    for (auto &e : this->inlinedModIds) {
      inlinedModIdList.push_back(e);
    }
    return {this->modId,
            this->version,
            std::move(this->decls),
            std::move(this->symbols),
            std::move(this->foreigns),
            std::move(inlinedModIdList)};
  }

  const TypePool &getPool() const { return this->memberMap.getPool(); }

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
    return this->addDeclImpl(info, kind, hover);
  }

  const Symbol *addSymbol(const NameInfo &info, DeclSymbol::Kind kind = DeclSymbol::Kind::VAR);

  void addThis(const NameInfo &info);

  bool importForeignDecls(unsigned short foreignModId, bool inlined);

  const Symbol *addMember(const DSType &recv, const NameInfo &nameInfo, DeclSymbol::Kind kind) {
    return this->addMemberImpl(recv, nameInfo, kind, nullptr);
  }

  const Symbol *addMember(const DSType &recv, const NameInfo &nameInfo, const Handle &handle) {
    return this->addMemberImpl(recv, nameInfo, DeclSymbol::Kind::VAR, &handle);
  }

  const Symbol *addMember(const MethodHandle &handle, const NameInfo &nameInfo) {
    return this->addMemberImpl(this->getPool().get(handle.getRecvTypeId()), nameInfo,
                               DeclSymbol::Kind::METHOD, &handle);
  }

  const DeclSymbol *addMemberDecl(const DSType &recv, const NameInfo &nameInfo, const DSType &type,
                                  DeclSymbol::Kind kind);

  const DeclSymbol *addMemberDecl(const DSType &recv, const NameInfo &nameInfo,
                                  DeclSymbol::Kind kind, const char *info);

  const DeclSymbol *findDecl(const Symbol &symbol) const;

private:
  const DeclSymbol *addDeclImpl(const NameInfo &info, DeclSymbol::Kind kind, const char *hover,
                                bool checkScope = true);

  const Symbol *addMemberImpl(const DSType &recv, const NameInfo &nameInfo, DeclSymbol::Kind kind,
                              const Handle *handle);

  DeclBase *addBuiltinFieldOrMethod(const DSType &recv, const NameInfo &nameInfo,
                                    const Handle &handle);

  DeclBase *resolveMemberDecl(const SymbolRef &entry);

  /**
   * create new DeclSymbol and insert to decl list
   * @param k
   * @param attr
   * @param nameInfo
   * @param info
   * @param checkScope
   * @return
   */
  DeclSymbol *insertNewDecl(DeclSymbol::Kind k, DeclSymbol::Attr attr, const NameInfo &nameInfo,
                            const char *info, bool checkScope = true);

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
  void visitInterfaceNode(InterfaceNode &node) override;
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
