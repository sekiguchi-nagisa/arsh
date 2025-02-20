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

#include "pass.h"

namespace arsh::lsp {

// ######################
// ##     NodePass     ##
// ######################

bool NodePass::consume(const std::unique_ptr<Node> &node) {
  NodePass::visit(node);
  return true;
}

Optional<NameInfo> NodePass::getConstArg(const std::vector<std::unique_ptr<Node>> &argsNode,
                                         unsigned int offset) {
  const CmdArgNode *argNode = nullptr;
  unsigned int offsetCount = 0;
  for (unsigned int i = 0; i < argsNode.size() && offset <= offsetCount; i++) {
    if (isa<CmdArgNode>(*argsNode[i]) && offset == offsetCount++) {
      argNode = cast<CmdArgNode>(argsNode[i].get());
      break;
    }
  }
  if (argNode && argNode->getSegmentNodes().size() == 1 &&
      isa<StringNode>(*argNode->getSegmentNodes()[0])) {
    auto &strNode = cast<StringNode>(*argNode->getSegmentNodes()[0]);
    return NameInfo(strNode.getToken(), strNode.getValue());
  }
  return {};
}

void NodePass::visitTypeNode(TypeNode &node) {
  switch (node.typeKind) {
  case TypeNode::Base:
    this->visitBase(cast<BaseTypeNode>(node));
    break;
  case TypeNode::Qualified:
    this->visitQualified(cast<QualifiedTypeNode>(node));
    break;
  case TypeNode::Reified:
    this->visitReified(cast<ReifiedTypeNode>(node));
    break;
  case TypeNode::Func:
    this->visitFunc(cast<FuncTypeNode>(node));
    break;
  case TypeNode::TypeOf:
    this->visitTypeOf(cast<TypeOfNode>(node));
    break;
  }
}

void NodePass::visitNumberNode(NumberNode &) {}

void NodePass::visitStringNode(StringNode &) {}

void NodePass::visitStringExprNode(StringExprNode &node) { this->visitEach(node.getExprNodes()); }

void NodePass::visitRegexNode(RegexNode &) {}

void NodePass::visitArrayNode(ArrayNode &node) { this->visitEach(node.getExprNodes()); }

void NodePass::visitMapNode(MapNode &node) {
  assert(node.getKeyNodes().size() == node.getValueNodes().size());
  unsigned int size = node.getKeyNodes().size();
  for (unsigned int i = 0; i < size; i++) {
    this->visit(node.getKeyNodes()[i]);
    this->visit(node.getValueNodes()[i]);
  }
}

void NodePass::visitTupleNode(TupleNode &node) { this->visitEach(node.getNodes()); }

void NodePass::visitVarNode(VarNode &) {}

void NodePass::visitAccessNode(AccessNode &node) { this->visit(node.getRecvNode()); }

void NodePass::visitTypeOpNode(TypeOpNode &node) {
  this->visit(node.getExprNode());
  this->visit(node.getTargetTypeNode());
}

void NodePass::visitUnaryOpNode(UnaryOpNode &node) {
  this->visit(node.getExprNode());
  this->visit(node.getApplyNode());
}

void NodePass::visitBinaryOpNode(BinaryOpNode &node) {
  this->visit(node.getLeftNode());
  this->visit(node.getRightNode());
  this->visit(node.getOptNode());
}

void NodePass::visitArgsNode(ArgsNode &node) { this->visitEach(node.getNodes()); }

void NodePass::visitApplyNode(ApplyNode &node) {
  this->visit(node.getExprNode());
  this->visit(node.getArgsNode());
}

void NodePass::visitEmbedNode(EmbedNode &node) { this->visit(node.getExprNode()); }

void NodePass::visitNewNode(NewNode &node) {
  this->visit(node.getTargetTypeNode());
  this->visit(node.getArgsNode());
}

void NodePass::visitForkNode(ForkNode &node) { this->visit(node.getExprNode()); }

void NodePass::visitCmdNode(CmdNode &node) {
  this->visit(node.getNameNode());
  this->visitEach(node.getArgNodes());
}

void NodePass::visitCmdArgNode(CmdArgNode &node) { this->visitEach(node.getSegmentNodes()); }

void NodePass::visitArgArrayNode(ArgArrayNode &node) { this->visitEach(node.getCmdArgNodes()); }

void NodePass::visitRedirNode(RedirNode &node) { this->visit(node.getTargetNode()); }

void NodePass::visitWildCardNode(WildCardNode &) {}
void NodePass::visitBraceSeqNode(BraceSeqNode &) {}

void NodePass::visitPipelineNode(PipelineNode &node) { this->visitEach(node.getNodes()); }

void NodePass::visitWithNode(WithNode &node) {
  this->visit(node.getExprNode());
  this->visitEach(node.getRedirNodes());
}

void NodePass::visitTimeNode(TimeNode &node) { this->visit(node.getExprNode()); }

void NodePass::visitAssertNode(AssertNode &node) {
  this->visit(node.getCondNode());
  this->visit(node.getMessageNode());
}

void NodePass::visitBlockNode(BlockNode &node) { this->visitEach(node.getNodes()); }

void NodePass::visitTypeDefNode(TypeDefNode &node) { this->visit(node.getTargetTypeNode()); }

void NodePass::visitDeferNode(DeferNode &node) { this->visit(node.getBlockNode()); }

void NodePass::visitLoopNode(LoopNode &node) {
  this->visit(node.getInitNode());
  this->visit(node.getCondNode());
  this->visit(node.getBlockNode());
  this->visit(node.getIterNode());
}

void NodePass::visitIfNode(IfNode &node) {
  this->visit(node.getCondNode());
  this->visit(node.getThenNode());
  this->visit(node.getElseNode());
}

void NodePass::visitCaseNode(CaseNode &node) {
  this->visit(node.getExprNode());
  this->visitEach(node.getArmNodes());
}

void NodePass::visitArmNode(ArmNode &node) {
  this->visitEach(node.getPatternNodes());
  this->visit(node.getActionNode());
}

void NodePass::visitJumpNode(JumpNode &node) { this->visit(node.getExprNode()); }

void NodePass::visitCatchNode(CatchNode &node) {
  this->visit(node.getTypeNode());
  this->visit(node.getBlockNode());
}

void NodePass::visitTryNode(TryNode &node) {
  this->visit(node.getExprNode());
  this->visitEach(node.getCatchNodes());
  this->visit(node.getFinallyNode());
}

void NodePass::visitVarDeclNode(VarDeclNode &node) { this->visit(node.getExprNode()); }

void NodePass::visitAttributeNode(AttributeNode &node) { this->visitEach(node.getValueNodes()); }

void NodePass::visitAssignNode(AssignNode &node) {
  this->visit(node.getLeftNode());
  this->visit(node.getRightNode());
}

void NodePass::visitElementSelfAssignNode(ElementSelfAssignNode &node) {
  this->visit(node.getRecvNode());
  this->visit(node.getIndexNode());
  this->visit(node.getRightNode());
}

void NodePass::visitPrefixAssignNode(PrefixAssignNode &node) {
  this->visitEach(node.getAssignNodes());
  this->visit(node.getExprNode());
}

void NodePass::visitFunctionNode(FunctionNode &node) {
  this->visitEach(node.getParamNodes());
  this->visit(node.getReturnTypeNode());
  this->visit(node.getRecvTypeNode());
  this->visit(node.getBlockNode());
}

void NodePass::visitUserDefinedCmdNode(UserDefinedCmdNode &node) {
  this->visit(node.getReturnTypeNode());
  this->visit(node.getParamNode());
  this->visit(node.getParamTypeNode());
  this->visit(node.getBlockNode());
}

void NodePass::visitFuncListNode(FuncListNode &node) {
  this->visitingDepth--;
  this->visitEach(node.getNodes());
  this->visitingDepth++;
}

void NodePass::visitSourceNode(SourceNode &node) {
  this->visitEach(node.getPathNode().getSegmentNodes());
}

void NodePass::visitSourceListNode(SourceListNode &) {} // unreachable
void NodePass::visitCodeCompNode(CodeCompNode &) {}

void NodePass::visitErrorNode(ErrorNode &node) {
  assert(this->isTopLevel());
  this->visitingDepth--;
  NodePass::visit(node.getOrgNode());
  this->visitingDepth++;
}

void NodePass::visitEmptyNode(EmptyNode &) {}

void NodePass::visit(Node &node) {
  if (node.isUntyped()) {
    return; // ignore Untyped node
  }
  this->visitingDepth++;
  NodeVisitor::visit(node);
  this->visitingDepth--;
}

void NodePass::visitBase(BaseTypeNode &) {}

void NodePass::visitQualified(QualifiedTypeNode &node) {
  this->visit(node.getRecvTypeNode());
  this->visit(node.getNameTypeNode());
}

void NodePass::visitReified(ReifiedTypeNode &node) { this->visitEach(node.getElementTypeNodes()); }

void NodePass::visitFunc(FuncTypeNode &node) {
  this->visitEach(node.getParamTypeNodes());
  this->visit(node.getReturnTypeNode());
}

void NodePass::visitTypeOf(TypeOfNode &node) { this->visit(node.getExprNode()); }

// ##############################
// ##     MultipleNodePass     ##
// ##############################

bool MultipleNodePass::enterModule(const SourcePtr &src, const std::shared_ptr<TypePool> &pool) {
  for (auto &e : this->passes) {
    e->enterModule(src, pool);
  }
  return true;
}

bool MultipleNodePass::exitModule(const std::unique_ptr<Node> &node) {
  for (auto &e : this->passes) {
    e->exitModule(node);
  }
  return true;
}
bool MultipleNodePass::consume(const std::unique_ptr<Node> &node) {
  for (auto &e : this->passes) {
    e->consume(node);
  }
  return true;
}

} // namespace arsh::lsp