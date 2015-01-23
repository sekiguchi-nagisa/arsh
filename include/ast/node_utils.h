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

#ifndef SRC_AST_NODE_UTILS_H_
#define SRC_AST_NODE_UTILS_H_

#include <ast/Node.h>

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
