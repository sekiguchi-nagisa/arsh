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

#include <ast/node_utils.h>
#include <core/magic_method.h>

#include <stdlib.h>
#include <utility>

std::string resolveOpName(int op) {
    //TODO:
    return std::string();
}

static ApplyNode *createApplyNode(ExprNode *recvNode, std::string &&methodName) {
    AccessNode *a = new AccessNode(recvNode, std::move(methodName));
    return new ApplyNode(a, new ArgsNode(a->getLineNum()));
}

ForNode *createForInNode(int lineNum, std::string &&initName, ExprNode *exprNode, BlockNode *blockNode) {
    // create for-init
    ApplyNode *apply_reset = createApplyNode(exprNode, std::string(RESET));
    std::string reset_var_name = std::to_string(rand());
    VarDeclNode *reset_varDecl = new VarDeclNode(lineNum, std::string(reset_var_name), apply_reset, true);

    // create for-cond
    VarNode *reset_var = new VarNode(lineNum, std::string(reset_var_name));
    ApplyNode *apply_hasNext = createApplyNode(reset_var, std::string(HAS_NEXT));

    // create forIn-init
    reset_var = new VarNode(lineNum, std::string(reset_var_name));
    ApplyNode *apply_next = createApplyNode(reset_var, std::string(NEXT));
    VarDeclNode *init_var = new VarDeclNode(lineNum, std::move(initName), apply_next, false);

    // insert init to block
    blockNode->insertNodeToFirst(init_var);

    return new ForNode(lineNum, reset_varDecl, apply_hasNext, 0, blockNode);
}

std::string resolveAssignOpName(int op) {
    return std::string();   //FIXME:
}

ExprNode *createSuffixNode(ExprNode *leftNode, int op) {
    return createAssignNode(leftNode, op, new IntValueNode(leftNode->getLineNum(), 1));
}

ExprNode *createAssignNode(ExprNode *leftNode, int op, ExprNode *rightNode) {   //TODO: element self assign
    /*
     * basic assignment
     */
    if(op == 0) {
        // assign to element(actually call SET)
        IndexNode *indexNode = dynamic_cast<IndexNode*>(leftNode);
        if(indexNode != 0) {
            return indexNode->treatAsAssignment(rightNode);
        }
        // assign to variable or field
        return new AssignNode(leftNode, rightNode);
    }

    /**
     * self assignment
     */
    std::string opName = resolveAssignOpName(op);
    // assign to field
    AccessNode *accessNode = dynamic_cast<AccessNode*>(leftNode);
    if(accessNode != 0) {
        ApplyNode *applyNode = createApplyNode(accessNode, std::move(opName));
        return new FieldSelfAssignNode(applyNode);
    }
    // assign to variable.
    OperatorCallNode *opNode = new OperatorCallNode(leftNode, op, rightNode);
//    ApplyNode *applyNode = createApplyNode(leftNode, std::move(opName));
//    applyNode->addArgNode(rightNode);
//    return new AssignNode(leftNode, applyNode);
    return 0;   //FIXME:
}
