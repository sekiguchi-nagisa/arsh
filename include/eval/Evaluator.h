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

#ifndef EVAL_EVALUATOR_H_
#define EVAL_EVALUATOR_H_

#include <ast/Node.h>
#include <ast/NodeVisitor.h>


class Evaluator: public NodeVisitor {
public:
    Evaluator();
    virtual ~Evaluator();

    void visitIntValueNode(IntValueNode *node); // override
    void visitFloatValueNode(FloatValueNode *node); // override
    void visitStringValueNode(StringValueNode *node); // override
    void visitStringExprNode(StringExprNode *node); // override
    void visitArrayNode(ArrayNode *node); // override
    void visitMapNode(MapNode *node); // override
    void visitTupleNode(TupleNode *node); // override
    void visitVarNode(VarNode *node); // override
    void visitAccessNode(AccessNode *node); // override
    void visitCastNode(CastNode *node); // override
    void visitInstanceOfNode(InstanceOfNode *node); // override
    void visitBinaryOpNode(BinaryOpNode *node); // override
    void visitArgsNode(ArgsNode *node); // override
    void visitApplyNode(ApplyNode *node); // override
    void visitNewNode(NewNode *node); // override
    void visitCondOpNode(CondOpNode *node); // override
    void visitCmdNode(CmdNode *node); // override
    void visitCmdArgNode(CmdArgNode *node); // override
    void visitSpecialCharNode(SpecialCharNode *node); // override
    void visitPipedCmdNode(PipedCmdNode *node); // override
    void visitCmdContextNode(CmdContextNode *node); // override
    void visitAssertNode(AssertNode *node); // override
    void visitBlockNode(BlockNode *node); // override
    void visitBreakNode(BreakNode *node); // override
    void visitContinueNode(ContinueNode *node); // override
    void visitExportEnvNode(ExportEnvNode *node); // override
    void visitImportEnvNode(ImportEnvNode *node); // override
    void visitForNode(ForNode *node); // override
    void visitWhileNode(WhileNode *node); // override
    void visitDoWhileNode(DoWhileNode *node); // override
    void visitIfNode(IfNode *node); // override
    void visitReturnNode(ReturnNode *node); // override
    void visitThrowNode(ThrowNode *node); // override
    void visitCatchNode(CatchNode *node); // override
    void visitTryNode(TryNode *node); // override
    void visitFinallyNode(FinallyNode *node); // override
    void visitVarDeclNode(VarDeclNode *node); // override
    void visitAssignNode(AssignNode *node); // override
    void visitFunctionNode(FunctionNode *node); // override
    void visitEmptyNode(EmptyNode *node); // override
    void visitDummyNode(DummyNode *node); // override
};

#endif /* EVAL_EVALUATOR_H_ */
