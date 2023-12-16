/*
 * Copyright (C) 2022 Nagisa Sekiguchi
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

#ifndef ARSH_TOOLS_ANALYZER_CONSUMER_H
#define ARSH_TOOLS_ANALYZER_CONSUMER_H

#include <node.h>
#include <type_pool.h>

#include "source.h"

namespace arsh::lsp {

/**
 * for common class for node traverse
 * if a node is untyped, not visit it
 */
class NodePass : protected arsh::NodeVisitor {
protected:
  int64_t visitingDepth{0};

public:
  virtual bool enterModule(const SourcePtr &src, const std::shared_ptr<TypePool> &pool) = 0;
  virtual bool exitModule(const std::unique_ptr<Node> &node) = 0;

  virtual bool consume(const std::unique_ptr<Node> &node);

  static Optional<NameInfo> getConstArg(const std::vector<std::unique_ptr<Node>> &argsNode,
                                        unsigned int offset = 0);

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
  void visitBraceSeqNode(BraceSeqNode &node) override;
  void visitPipelineNode(PipelineNode &node) override;
  void visitWithNode(WithNode &node) override;
  void visitTimeNode(TimeNode &node) override;
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
  void visitAttributeNode(AttributeNode &node) override;
  void visitAssignNode(AssignNode &node) override;
  void visitElementSelfAssignNode(ElementSelfAssignNode &node) override;
  void visitPrefixAssignNode(PrefixAssignNode &node) override;
  void visitFunctionNode(FunctionNode &node) override;
  void visitUserDefinedCmdNode(UserDefinedCmdNode &node) override;
  void visitFuncListNode(FuncListNode &node) override;
  void visitSourceNode(SourceNode &node) override;
  void visitSourceListNode(SourceListNode &node) override;
  void visitCodeCompNode(CodeCompNode &node) override;
  void visitErrorNode(ErrorNode &node) override;
  void visitEmptyNode(EmptyNode &node) override;
  void visit(Node &node) override;

  // for type node visiting
  virtual void visitBase(BaseTypeNode &node);
  virtual void visitQualified(QualifiedTypeNode &node);
  virtual void visitReified(ReifiedTypeNode &node);
  virtual void visitFunc(FuncTypeNode &node);
  virtual void visitTypeOf(TypeOfNode &node);

  template <typename T, enable_when<std::is_convertible_v<T *, Node *>> = nullptr>
  void visit(T *node) {
    if (node) {
      this->visit(*node);
    }
  }

  template <typename T, enable_when<std::is_convertible_v<T *, Node *>> = nullptr>
  void visit(const std::unique_ptr<T> &node) {
    if (node) {
      this->visit(*node);
    }
  }

  template <typename T, enable_when<std::is_convertible_v<T *, Node *>> = nullptr>
  void visitEach(const std::vector<std::unique_ptr<T>> &nodes) {
    for (const auto &item : nodes) {
      this->visit(item);
    }
  }

  bool isTopLevel() const { return this->visitingDepth == 1; }
};

class MultipleNodePass : public NodePass {
private:
  std::vector<ObserverPtr<NodePass>> passes;

public:
  bool enterModule(const SourcePtr &src, const std::shared_ptr<TypePool> &pool) override;
  bool exitModule(const std::unique_ptr<Node> &node) override;
  bool consume(const std::unique_ptr<Node> &node) override;

  void add(ObserverPtr<NodePass> v) { this->passes.push_back(v); }
};

} // namespace arsh::lsp

#endif // ARSH_TOOLS_ANALYZER_CONSUMER_H
