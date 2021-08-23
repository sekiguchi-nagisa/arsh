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

#include "index_builder.h"

namespace ydsh::lsp {

// ################################
// ##     SymbolIndexBuilder     ##
// ################################

void IndexBuilder::visitTypeNode(TypeNode &node) {
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

void IndexBuilder::visitNumberNode(NumberNode &) {}

void IndexBuilder::visitStringNode(StringNode &) {}

void IndexBuilder::visitStringExprNode(StringExprNode &node) {
  this->visitEach(node.getExprNodes());
}

void IndexBuilder::visitRegexNode(RegexNode &) {}

void IndexBuilder::visitArrayNode(ArrayNode &node) { this->visitEach(node.getExprNodes()); }

void IndexBuilder::visitMapNode(MapNode &node) {
  this->visitEach(node.getKeyNodes());
  this->visitEach(node.getValueNodes());
}

void IndexBuilder::visitTupleNode(TupleNode &node) { this->visitEach(node.getNodes()); }

void IndexBuilder::visitVarNode(VarNode &) {} // FIXME:

void IndexBuilder::visitAccessNode(AccessNode &node) { // FIXME: field name
  this->visit(node.getRecvNode());
}

void IndexBuilder::visitTypeOpNode(TypeOpNode &node) {
  this->visit(node.getExprNode());
  this->visit(node.getTargetTypeNode());
}

void IndexBuilder::visitUnaryOpNode(UnaryOpNode &node) {
  this->visit(node.getExprNode());
  this->visit(node.getApplyNode());
}

void IndexBuilder::visitBinaryOpNode(BinaryOpNode &node) {
  this->visit(node.getLeftNode());
  this->visit(node.getRightNode());
  this->visit(node.getOptNode());
}

void IndexBuilder::visitArgsNode(ArgsNode &node) { // FIXME: method name ?
  this->visitEach(node.getNodes());
}

void IndexBuilder::visitApplyNode(ApplyNode &node) { // FIXME: method call?
  this->visit(node.getExprNode());
  this->visit(node.getArgsNode());
}

void IndexBuilder::visitEmbedNode(EmbedNode &node) { this->visit(node.getExprNode()); }

void IndexBuilder::visitNewNode(NewNode &node) { // FIXME: constructor name ?
  this->visit(node.getTargetTypeNode());
  this->visit(node.getArgsNode());
}

void IndexBuilder::visitForkNode(ForkNode &node) { this->visit(node.getExprNode()); }

void IndexBuilder::visitCmdNode(CmdNode &node) {
  this->visit(node.getNameNode());
  this->visitEach(node.getArgNodes());
}

void IndexBuilder::visitCmdArgNode(CmdArgNode &node) { this->visitEach(node.getSegmentNodes()); }

void IndexBuilder::visitArgArrayNode(ArgArrayNode &node) { this->visitEach(node.getCmdArgNodes()); }

void IndexBuilder::visitRedirNode(RedirNode &node) { this->visit(node.getTargetNode()); }

void IndexBuilder::visitWildCardNode(WildCardNode &) {}

void IndexBuilder::visitPipelineNode(PipelineNode &node) { this->visitEach(node.getNodes()); }

void IndexBuilder::visitWithNode(WithNode &node) { this->visit(node.getExprNode()); }

void IndexBuilder::visitAssertNode(AssertNode &node) {
  this->visit(node.getCondNode());
  this->visit(node.getCondNode());
}

void IndexBuilder::visitBlockNode(BlockNode &node) { // FIXME: scope
  this->visitEach(node.getNodes());
}

void IndexBuilder::visitTypeAliasNode(TypeAliasNode &node) { // FIXME: add alias decl
  this->visit(node.getTargetTypeNode());
}

void IndexBuilder::visitLoopNode(LoopNode &node) { // FIXME: merge loop-init scope
  this->visit(node.getInitNode());
  this->visit(node.getCondNode());
  this->visit(node.getBlockNode());
  this->visit(node.getIterNode());
}

void IndexBuilder::visitIfNode(IfNode &node) {
  this->visit(node.getCondNode());
  this->visit(node.getThenNode());
  this->visit(node.getElseNode());
}

void IndexBuilder::visitCaseNode(CaseNode &node) {
  this->visit(node.getExprNode());
  this->visitEach(node.getArmNodes());
}

void IndexBuilder::visitArmNode(ArmNode &node) {
  this->visitEach(node.getPatternNodes()); // FIXME: ignore not-constant expression
  this->visit(node.getActionNode());
}

void IndexBuilder::visitJumpNode(JumpNode &node) { this->visit(node.getExprNode()); }

void IndexBuilder::visitCatchNode(CatchNode &node) { // FIXME: var name
  this->visit(node.getTypeNode());

  this->visit(node.getBlockNode());
}

void IndexBuilder::visitTryNode(TryNode &node) {
  this->visit(node.getExprNode());
  this->visitEach(node.getCatchNodes());
  this->visit(node.getFinallyNode());
}

void IndexBuilder::visitVarDeclNode(VarDeclNode &node) {
  this->visit(node.getExprNode()); // FIXME: add decl
}

void IndexBuilder::visitAssignNode(AssignNode &node) {
  this->visit(node.getLeftNode());
  this->visit(node.getRightNode());
}

void IndexBuilder::visitElementSelfAssignNode(ElementSelfAssignNode &node) {
  this->visit(node.getRecvNode());
  this->visit(node.getIndexNode());
  this->visit(node.getRightNode());
}

void IndexBuilder::visitPrefixAssignNode(PrefixAssignNode &) {} // FIXME:

void IndexBuilder::visitFunctionNode(FunctionNode &) {} // FIXME:

void IndexBuilder::visitInterfaceNode(InterfaceNode &) {}

void IndexBuilder::visitUserDefinedCmdNode(UserDefinedCmdNode &) {} // FIXME:

void IndexBuilder::visitSourceNode(SourceNode &) {} // FIXME:

void IndexBuilder::visitSourceListNode(SourceListNode &) {} // FIXME:

void IndexBuilder::visitCodeCompNode(CodeCompNode &) {}

void IndexBuilder::visitErrorNode(ErrorNode &) {}

void IndexBuilder::visitEmptyNode(EmptyNode &) {}

void IndexBuilder::visit(Node &node) {
  NodeVisitor::visit(node); // FIXME:
}

void IndexBuilder::enterModule(unsigned short modID, const std::shared_ptr<TypePool> &p) {
  (void)modID;
  this->pool = p;
}

void IndexBuilder::exitModule(std::unique_ptr<Node> &&node) {
  this->visit(node); // FIXME:
}

void IndexBuilder::consume(std::unique_ptr<Node> &&node) { this->visit(node); }

} // namespace ydsh::lsp