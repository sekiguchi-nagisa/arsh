/*
 * node_utils.h
 *
 *  Created on: 2015/01/21
 *      Author: skgchxngsxyz-opensuse
 */

#ifndef SRC_AST_NODE_UTILS_H_
#define SRC_AST_NODE_UTILS_H_

#include "Node.h"

/**
 * helper function for binary op node creation.
 * op is binary op kind.
 */
ExprNode *createBinaryOpNode(ExprNode *leftNode, int op, ExprNode *rightNode);

/**
 * for unary op node creation.
 * op is unary op kind
 */
ExprNode *createUnaryOpNode(int op, ExprNode *rightNode);

ForNode *createForInNode(int lineNum, std::string &&initName, ExprNode *exprNode, BlockNode *blockNode);

ApplyNode *createConstructorCallNode(int lineNum, TypeToken *targetTypeToken);


#endif /* SRC_AST_NODE_UTILS_H_ */
