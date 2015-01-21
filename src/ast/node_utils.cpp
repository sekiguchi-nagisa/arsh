/*
 * node_utils.cpp
 *
 *  Created on: 2015/01/21
 *      Author: skgchxngsxyz-opensuse
 */

#include <stdlib.h>
#include <utility>

#include "node_utils.h"
#include "../core/magic_method.h"

static std::string resolveOpName(int op) {
    //TODO:
    return std::string();
}

static ApplyNode *createApplyNode(int lineNum, ExprNode *recvNode, std::string &&methodName, bool overload) {
    AccessNode *a = new AccessNode(lineNum, recvNode, std::move(methodName));
    return new ApplyNode(a, overload);
}


ExprNode *createBinaryOpNode(ExprNode *leftNode, int op, ExprNode *rightNode) {
    ApplyNode *node = createApplyNode(leftNode->getLineNum(), leftNode, resolveOpName(op), true);
    node->addArgNode(rightNode);
    return node;
}

ExprNode *createUnaryOpNode(int op, ExprNode *rightNode) {
    return createApplyNode(rightNode->getLineNum(), rightNode, resolveOpName(op), false);
}

ForNode *createForInNode(int lineNum, std::string &&initName, ExprNode *exprNode, BlockNode *blockNode) {
    // create for-init
    ApplyNode *apply_reset = createApplyNode(lineNum, exprNode, std::string(RESET), false);
    std::string reset_var_name = std::to_string(rand());
    VarDeclNode *reset_varDecl = new VarDeclNode(lineNum, std::string(reset_var_name), apply_reset, true);

    // create for-cond
    VarNode *reset_var = new VarNode(lineNum, std::string(reset_var_name));
    ApplyNode *apply_hasNext = createApplyNode(lineNum, reset_var, std::string(HAS_NEXT), false);

    // create forIn-init
    reset_var = new VarNode(lineNum, std::string(reset_var_name));
    ApplyNode *apply_next = createApplyNode(lineNum, reset_var, std::string(NEXT), false);
    VarDeclNode *init_var = new VarDeclNode(lineNum, std::move(initName), apply_next, false);

    // insert init to block
    blockNode->insertNodeToFirst(init_var);

    return new ForNode(lineNum, reset_varDecl, apply_hasNext, 0, blockNode);
}

ApplyNode *createConstructorCallNode(int lineNum, TypeToken *targetTypeToken) {
    return new ApplyNode(new NewNode(lineNum, targetTypeToken));
}

