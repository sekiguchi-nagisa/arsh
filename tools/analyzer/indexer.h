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

class IndexBuilder {
private:
  unsigned short modId;
  int version;
  std::shared_ptr<TypePool> pool;
  std::vector<DeclSymbol> decls;
  std::vector<Symbol> symbols;

  struct ScopeEntry : public RefCount<ScopeEntry> {
    std::unordered_map<std::string, SymbolRef> map;

    const IntrusivePtr<ScopeEntry> parent;

    explicit ScopeEntry(const IntrusivePtr<ScopeEntry> &parent) : parent(parent) {}
  };

  IntrusivePtr<ScopeEntry> scope;

public:
  IndexBuilder(unsigned short modId, int version, const std::shared_ptr<TypePool> &pool)
      : modId(modId), version(version), pool(pool),
        scope(IntrusivePtr<ScopeEntry>::create(nullptr)) {}

  SymbolIndex build() && {
    return {this->modId, this->version, std::move(this->decls), std::move(this->symbols)};
  }

  auto intoScope() {
    this->scope = IntrusivePtr<ScopeEntry>::create(this->scope);
    return finally([&] { this->scope = this->scope->parent; });
  }

  bool addDecl(const NameInfo &info, DeclSymbol::Kind kind = DeclSymbol::Kind::VAR);

  bool addSymbol(const NameInfo &info, DeclSymbol::Kind kind = DeclSymbol::Kind::VAR);

private:
  const SymbolRef *findDeclRef(const std::string &name) const;

  DeclSymbol *addDeclImpl(DeclSymbol::Kind k, Token token);

  bool addSymbolImpl(Token token, unsigned short declModId, const DeclSymbol &decl);
};

class SymbolIndexer : protected ydsh::NodeVisitor, public NodeConsumer {
private:
  SymbolIndexes &indexes;
  std::vector<IndexBuilder> builders;

public:
  explicit SymbolIndexer(SymbolIndexes &indexes) : indexes(indexes) {}

  ~SymbolIndexer() override = default;

  void enterModule(unsigned short modId, int version,
                   const std::shared_ptr<TypePool> &pool) override;
  void exitModule(std::unique_ptr<Node> &&node) override;
  void consume(std::unique_ptr<Node> &&node) override;

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
  void visitTypeAliasNode(TypeAliasNode &node) override;
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
};

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_INDEXER_H
