/*
 * Copyright (C) 2015 Nagisa Sekiguchi
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

#include "NodeVerifier.h"
#include "../misc/unused.h"

namespace ydsh {
namespace parser {

// #######################################
// ##     ToplevelStatementVerifier     ##
// #######################################

void ToplevelStatementVerifier::visit(Node *node) {
    this->currentNodes.push_back(node);
    BaseVisitor::visit(node);
    this->currentNodes.pop_back();
}

void ToplevelStatementVerifier::visitDefault(Node *node) {
    UNUSED(node);   // do nothing
}

void ToplevelStatementVerifier::visitPipedCmdNode(PipedCmdNode *node) {
    for(auto *cmdNode : node->getCmdNodes()) {
        this->visit(cmdNode);
    }
}

void ToplevelStatementVerifier::visitBlockNode(BlockNode *node) {
    for(auto *child : node->getNodeList()) {
        this->visit(child);
    }
}

void ToplevelStatementVerifier::visitForNode(ForNode *node) {
    this->visit(node->getBlockNode());
}

void ToplevelStatementVerifier::visitWhileNode(WhileNode *node) {
    this->visit(node->getBlockNode());
}

void ToplevelStatementVerifier::visitDoWhileNode(DoWhileNode *node) {
    this->visit(node->getBlockNode());
}

void ToplevelStatementVerifier::visitIfNode(IfNode *node) {
    this->visit(node->getThenNode());
    for(auto *child : node->getElifThenNodes()) {
        this->visit(child);
    }
    this->visit(node->getElseNode());
}

void ToplevelStatementVerifier::visitCatchNode(CatchNode *node) {
    this->visit(node->getBlockNode());
}

void ToplevelStatementVerifier::visitTryNode(TryNode *node) {
    this->visit(node->getBlockNode());
    for(auto *child : node->getCatchNodes()) {
        this->visit(child);
    }
    this->visit(node->getFinallyNode());
}

void ToplevelStatementVerifier::visitFunctionNode(FunctionNode *node) {
    this->visit(node->getBlockNode());
}

void ToplevelStatementVerifier::visitUserDefinedCmdNode(UserDefinedCmdNode *node) {
    if(this->currentNodes.size() != 2 || dynamic_cast<RootNode*>(this->currentNodes[0]) == nullptr) {
        E_OutsideToplevel(node);
    }
    this->visit(node->getBlockNode());
}

void ToplevelStatementVerifier::visitRootNode(RootNode *node) {
    for(auto *child : node->getNodeList()) {
        this->visit(child);
    }
}

} // namespace parser
} // namespace ydsh

