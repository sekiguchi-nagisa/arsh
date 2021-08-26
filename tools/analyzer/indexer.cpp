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

#include "indexer.h"

namespace ydsh::lsp {

// ###########################
// ##     SymbolIndexer     ##
// ###########################

void SymbolIndexer::visitTypeNode(TypeNode &node) {
  switch (node.typeKind) {
  case TypeNode::Base:
    break;
  case TypeNode::Qualified: {
    auto &type = cast<QualifiedTypeNode>(node);
    this->visit(type.getRecvTypeNode());
    break;
  }
  case TypeNode::Reified: {
    auto &type = cast<ReifiedTypeNode>(node);
    this->visitEach(type.getElementTypeNodes());
    break;
  }
  case TypeNode::Func: {
    auto &type = cast<FuncTypeNode>(node);
    this->visitEach(type.getParamTypeNodes());
    this->visit(type.getReturnTypeNode());
    break;
  }
  case TypeNode::Return: {
    auto &type = cast<ReturnTypeNode>(node);
    this->visitEach(type.getTypeNodes());
    break;
  }
  case TypeNode::TypeOf: {
    auto &type = cast<TypeOfNode>(node);
    this->visit(type.getExprNode());
    break;
  }
  }
}

void SymbolIndexer::visitNumberNode(NumberNode &) {}

void SymbolIndexer::visitStringNode(StringNode &) {}

void SymbolIndexer::visitStringExprNode(StringExprNode &node) {
  this->visitEach(node.getExprNodes());
}

void SymbolIndexer::visitRegexNode(RegexNode &) {}

void SymbolIndexer::visitArrayNode(ArrayNode &node) { this->visitEach(node.getExprNodes()); }

void SymbolIndexer::visitMapNode(MapNode &node) {
  this->visitEach(node.getKeyNodes());
  this->visitEach(node.getValueNodes());
}

void SymbolIndexer::visitTupleNode(TupleNode &node) { this->visitEach(node.getNodes()); }

void SymbolIndexer::visitVarNode(VarNode &) {} // FIXME:

void SymbolIndexer::visitAccessNode(AccessNode &node) { // FIXME: field name
  this->visit(node.getRecvNode());
}

void SymbolIndexer::visitTypeOpNode(TypeOpNode &node) {
  this->visit(node.getExprNode());
  this->visit(node.getTargetTypeNode());
}

void SymbolIndexer::visitUnaryOpNode(UnaryOpNode &node) {
  this->visit(node.getExprNode());
  this->visit(node.getApplyNode());
}

void SymbolIndexer::visitBinaryOpNode(BinaryOpNode &node) {
  this->visit(node.getLeftNode());
  this->visit(node.getRightNode());
  this->visit(node.getOptNode());
}

void SymbolIndexer::visitArgsNode(ArgsNode &node) { // FIXME: method name ?
  this->visitEach(node.getNodes());
}

void SymbolIndexer::visitApplyNode(ApplyNode &node) { // FIXME: method call?
  this->visit(node.getExprNode());
  this->visit(node.getArgsNode());
}

void SymbolIndexer::visitEmbedNode(EmbedNode &node) { this->visit(node.getExprNode()); }

void SymbolIndexer::visitNewNode(NewNode &node) { // FIXME: constructor name ?
  this->visit(node.getTargetTypeNode());
  this->visit(node.getArgsNode());
}

void SymbolIndexer::visitForkNode(ForkNode &node) { this->visit(node.getExprNode()); }

void SymbolIndexer::visitCmdNode(CmdNode &node) {
  this->visit(node.getNameNode());
  this->visitEach(node.getArgNodes());
}

void SymbolIndexer::visitCmdArgNode(CmdArgNode &node) { this->visitEach(node.getSegmentNodes()); }

void SymbolIndexer::visitArgArrayNode(ArgArrayNode &node) {
  this->visitEach(node.getCmdArgNodes());
}

void SymbolIndexer::visitRedirNode(RedirNode &node) { this->visit(node.getTargetNode()); }

void SymbolIndexer::visitWildCardNode(WildCardNode &) {}

void SymbolIndexer::visitPipelineNode(PipelineNode &node) { this->visitEach(node.getNodes()); }

void SymbolIndexer::visitWithNode(WithNode &node) { this->visit(node.getExprNode()); }

void SymbolIndexer::visitAssertNode(AssertNode &node) {
  this->visit(node.getCondNode());
  this->visit(node.getCondNode());
}

void SymbolIndexer::visitBlockNode(BlockNode &node) { // FIXME: scope
  this->visitEach(node.getNodes());
}

void SymbolIndexer::visitTypeAliasNode(TypeAliasNode &node) { // FIXME: add alias decl
  this->visit(node.getTargetTypeNode());
}

void SymbolIndexer::visitLoopNode(LoopNode &node) { // FIXME: merge loop-init scope
  this->visit(node.getInitNode());
  this->visit(node.getCondNode());
  this->visit(node.getBlockNode());
  this->visit(node.getIterNode());
}

void SymbolIndexer::visitIfNode(IfNode &node) {
  this->visit(node.getCondNode());
  this->visit(node.getThenNode());
  this->visit(node.getElseNode());
}

void SymbolIndexer::visitCaseNode(CaseNode &node) {
  this->visit(node.getExprNode());
  this->visitEach(node.getArmNodes());
}

void SymbolIndexer::visitArmNode(ArmNode &node) {
  this->visitEach(node.getPatternNodes()); // FIXME: ignore not-constant expression
  this->visit(node.getActionNode());
}

void SymbolIndexer::visitJumpNode(JumpNode &node) { this->visit(node.getExprNode()); }

void SymbolIndexer::visitCatchNode(CatchNode &node) { // FIXME: var name
  this->visit(node.getTypeNode());

  this->visit(node.getBlockNode());
}

void SymbolIndexer::visitTryNode(TryNode &node) {
  this->visit(node.getExprNode());
  this->visitEach(node.getCatchNodes());
  this->visit(node.getFinallyNode());
}

void SymbolIndexer::visitVarDeclNode(VarDeclNode &node) {
  this->visit(node.getExprNode()); // FIXME: add decl
}

void SymbolIndexer::visitAssignNode(AssignNode &node) {
  this->visit(node.getLeftNode());
  this->visit(node.getRightNode());
}

void SymbolIndexer::visitElementSelfAssignNode(ElementSelfAssignNode &node) {
  this->visit(node.getRecvNode());
  this->visit(node.getIndexNode());
  this->visit(node.getRightNode());
}

void SymbolIndexer::visitPrefixAssignNode(PrefixAssignNode &) {} // FIXME:

void SymbolIndexer::visitFunctionNode(FunctionNode &) {} // FIXME:

void SymbolIndexer::visitInterfaceNode(InterfaceNode &) {}

void SymbolIndexer::visitUserDefinedCmdNode(UserDefinedCmdNode &) {} // FIXME:

void SymbolIndexer::visitSourceNode(SourceNode &) {} // FIXME:

void SymbolIndexer::visitSourceListNode(SourceListNode &) {} // FIXME:

void SymbolIndexer::visitCodeCompNode(CodeCompNode &) {}

void SymbolIndexer::visitErrorNode(ErrorNode &) {}

void SymbolIndexer::visitEmptyNode(EmptyNode &) {}

void SymbolIndexer::visit(Node &node) {
  NodeVisitor::visit(node); // FIXME:
}

void SymbolIndexer::enterModule(unsigned short modID, int version,
                                const std::shared_ptr<TypePool> &p) {
  this->builders.emplace_back(modID, version, p);
}

void SymbolIndexer::exitModule(std::unique_ptr<Node> &&node) {
  assert(!this->builders.empty());
  auto index = std::move(this->builder()).build();
  this->builders.pop_back();
  this->indexes.add(std::move(index));
  this->visit(node);
}

void SymbolIndexer::consume(std::unique_ptr<Node> &&node) { this->visit(node); }

} // namespace ydsh::lsp