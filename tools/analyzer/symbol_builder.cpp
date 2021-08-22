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

#include "symbol_builder.h"

namespace ydsh::lsp {

// ################################
// ##     SymbolIndexBuilder     ##
// ################################

void SymbolIndexBuilder::visitTypeNode(TypeNode &node) {
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

void SymbolIndexBuilder::visitNumberNode(NumberNode &) {}

void SymbolIndexBuilder::visitStringNode(StringNode &) {}

void SymbolIndexBuilder::visitStringExprNode(StringExprNode &node) {
  this->visitEach(node.getExprNodes());
}

void SymbolIndexBuilder::visitRegexNode(RegexNode &) {}

void SymbolIndexBuilder::visitArrayNode(ArrayNode &node) { this->visitEach(node.getExprNodes()); }

void SymbolIndexBuilder::visitMapNode(MapNode &node) {
  this->visitEach(node.getKeyNodes());
  this->visitEach(node.getValueNodes());
}

void SymbolIndexBuilder::visitTupleNode(TupleNode &node) { this->visitEach(node.getNodes()); }

void SymbolIndexBuilder::visitVarNode(VarNode &) {} // FIXME:

void SymbolIndexBuilder::visitAccessNode(AccessNode &node) { // FIXME: field name
  this->visit(node.getRecvNode());
}

void SymbolIndexBuilder::visitTypeOpNode(TypeOpNode &node) {
  this->visit(node.getExprNode());
  this->visit(node.getTargetTypeNode());
}

void SymbolIndexBuilder::visitUnaryOpNode(UnaryOpNode &node) {
  this->visit(node.getExprNode());
  this->visit(node.getApplyNode());
}

void SymbolIndexBuilder::visitBinaryOpNode(BinaryOpNode &node) {
  this->visit(node.getLeftNode());
  this->visit(node.getRightNode());
  this->visit(node.getOptNode());
}

void SymbolIndexBuilder::visitArgsNode(ArgsNode &node) { // FIXME: method name ?
  this->visitEach(node.getNodes());
}

void SymbolIndexBuilder::visitApplyNode(ApplyNode &node) { // FIXME: method call?
  this->visit(node.getExprNode());
  this->visit(node.getArgsNode());
}

void SymbolIndexBuilder::visitEmbedNode(EmbedNode &node) { this->visit(node.getExprNode()); }

void SymbolIndexBuilder::visitNewNode(NewNode &node) { // FIXME: constructor name ?
  this->visit(node.getTargetTypeNode());
  this->visit(node.getArgsNode());
}

void SymbolIndexBuilder::visitForkNode(ForkNode &node) { this->visit(node.getExprNode()); }

void SymbolIndexBuilder::visitCmdNode(CmdNode &node) {
  this->visit(node.getNameNode());
  this->visitEach(node.getArgNodes());
}

void SymbolIndexBuilder::visitCmdArgNode(CmdArgNode &node) {
  this->visitEach(node.getSegmentNodes());
}

void SymbolIndexBuilder::visitArgArrayNode(ArgArrayNode &node) {
  this->visitEach(node.getCmdArgNodes());
}

void SymbolIndexBuilder::visitRedirNode(RedirNode &node) { this->visit(node.getTargetNode()); }

void SymbolIndexBuilder::visitWildCardNode(WildCardNode &) {}

void SymbolIndexBuilder::visitPipelineNode(PipelineNode &node) { this->visitEach(node.getNodes()); }

void SymbolIndexBuilder::visitWithNode(WithNode &node) { this->visit(node.getExprNode()); }

void SymbolIndexBuilder::visitAssertNode(AssertNode &node) {
  this->visit(node.getCondNode());
  this->visit(node.getCondNode());
}

void SymbolIndexBuilder::visitBlockNode(BlockNode &node) { // FIXME: scope
  this->visitEach(node.getNodes());
}

void SymbolIndexBuilder::visitTypeAliasNode(TypeAliasNode &node) { // FIXME: add alias decl
  this->visit(node.getTargetTypeNode());
}

void SymbolIndexBuilder::visitLoopNode(LoopNode &node) { // FIXME: merge loop-init scope
  this->visit(node.getInitNode());
  this->visit(node.getCondNode());
  this->visit(node.getBlockNode());
  this->visit(node.getIterNode());
}

void SymbolIndexBuilder::visitIfNode(IfNode &node) {
  this->visit(node.getCondNode());
  this->visit(node.getThenNode());
  this->visit(node.getElseNode());
}

void SymbolIndexBuilder::visitCaseNode(CaseNode &node) {
  this->visit(node.getExprNode());
  this->visitEach(node.getArmNodes());
}

void SymbolIndexBuilder::visitArmNode(ArmNode &node) {
  this->visitEach(node.getPatternNodes()); // FIXME: ignore not-constant expression
  this->visit(node.getActionNode());
}

void SymbolIndexBuilder::visitJumpNode(JumpNode &node) { this->visit(node.getExprNode()); }

void SymbolIndexBuilder::visitCatchNode(CatchNode &node) { // FIXME: var name
  this->visit(node.getTypeNode());

  this->visit(node.getBlockNode());
}

void SymbolIndexBuilder::visitTryNode(TryNode &node) {
  this->visit(node.getExprNode());
  this->visitEach(node.getCatchNodes());
  this->visit(node.getFinallyNode());
}

void SymbolIndexBuilder::visitVarDeclNode(VarDeclNode &node) {
  this->visit(node.getExprNode()); // FIXME: add decl
}

void SymbolIndexBuilder::visitAssignNode(AssignNode &node) {
  this->visit(node.getLeftNode());
  this->visit(node.getRightNode());
}

void SymbolIndexBuilder::visitElementSelfAssignNode(ElementSelfAssignNode &node) {
  this->visit(node.getRecvNode());
  this->visit(node.getIndexNode());
  this->visit(node.getRightNode());
}

void SymbolIndexBuilder::visitPrefixAssignNode(PrefixAssignNode &) {} // FIXME:

void SymbolIndexBuilder::visitFunctionNode(FunctionNode &) {} // FIXME:

void SymbolIndexBuilder::visitInterfaceNode(InterfaceNode &) {}

void SymbolIndexBuilder::visitUserDefinedCmdNode(UserDefinedCmdNode &) {} // FIXME:

void SymbolIndexBuilder::visitSourceNode(SourceNode &) {} // FIXME:

void SymbolIndexBuilder::visitSourceListNode(SourceListNode &) {} // FIXME:

void SymbolIndexBuilder::visitCodeCompNode(CodeCompNode &) {}

void SymbolIndexBuilder::visitErrorNode(ErrorNode &) {}

void SymbolIndexBuilder::visitEmptyNode(EmptyNode &) {}

void SymbolIndexBuilder::visit(Node &node) {
  NodeVisitor::visit(node); // FIXME:
}

void SymbolIndexBuilder::enterModule(unsigned short modID, const std::shared_ptr<TypePool> &p) {
  (void)modID;
  this->pool = p;
}

void SymbolIndexBuilder::exitModule(std::unique_ptr<Node> &&node) {
  this->visit(node); // FIXME:
}

void SymbolIndexBuilder::consume(std::unique_ptr<Node> &&node) { this->visit(node); }

} // namespace ydsh::lsp