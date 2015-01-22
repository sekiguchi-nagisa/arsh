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

#ifndef AST_NODEVISITOR_H_
#define AST_NODEVISITOR_H_

class Node;
class IntValueNode;
class FloatValueNode;
class BooleanValueNode;
class StringValueNode;
class StringExprNode;
class ArrayNode;
class MapNode;
class PairNode;
class VarNode;
class IndexNode;
class AccessNode;
class CastNode;
class InstanceOfNode;
class ApplyNode;
class NewNode;
class CondOpNode;
class ProcessNode;
class ProcArgNode;
class SpecialCharNode;
class TaskNode;
class InnerTaskNode;

class AssertNode;
class BlockNode;
class BreakNode;
class ContinueNode;
class ExportEnvNode;
class ImportEnvNode;
class ForNode;
class WhileNode;
class IfNode;
class ReturnNode;
class ThrowNode;
class CatchNode;
class TryNode;
class FinallyNode;
class VarDeclNode;
class AssignNode;
class FunctionNode;
class EmptyNode;

class NodeVisitor {
public:
    NodeVisitor();
    virtual ~NodeVisitor();

    virtual int visitIntValueNode(IntValueNode *node) = 0;
    virtual int visitFloatValueNode(FloatValueNode *node) = 0;
    virtual int visitBooleanValueNode(BooleanValueNode *node) = 0;
    virtual int visitStringValueNode(StringValueNode *node) = 0;
    virtual int visitStringExprNode(StringExprNode *node) = 0;
    virtual int visitArrayNode(ArrayNode *node) = 0;
    virtual int visitMapNode(MapNode *node) = 0;
    virtual int visitPairNode(PairNode *node) = 0;
    virtual int visitVarNode(VarNode *node) = 0;
    virtual int visitIndexNode(IndexNode *node) = 0;
    virtual int visitAccessNode(AccessNode *node) = 0;
    virtual int visitCastNode(CastNode *node) = 0;
    virtual int visitInstanceOfNode(InstanceOfNode *node) = 0;
    virtual int visitApplyNode(ApplyNode *node) = 0;
    virtual int visitNewNode(NewNode *node) = 0;
    virtual int visitCondOpNode(CondOpNode *node) = 0;
    virtual int visitProcessNode(ProcessNode *node) = 0;
    virtual int visitProcArgNode(ProcArgNode *node) = 0;
    virtual int visitSpecialCharNode(SpecialCharNode *node) = 0;
    virtual int visitTaskNode(TaskNode *node) = 0;
    virtual int visitInnerTaskNode(InnerTaskNode *node) = 0;
    virtual int visitAssertNode(AssertNode *node) = 0;
    virtual int visitBlockNode(BlockNode *node) = 0;
    virtual int visitBreakNode(BreakNode *node) = 0;
    virtual int visitContinueNode(ContinueNode *node) = 0;
    virtual int visitExportEnvNode(ExportEnvNode *node) = 0;
    virtual int visitImportEnvNode(ImportEnvNode *node) = 0;
    virtual int visitForNode(ForNode *node) = 0;
    virtual int visitWhileNode(WhileNode *node) = 0;
    virtual int visitIfNode(IfNode *node) = 0;
    virtual int visitReturnNode(ReturnNode *node) = 0;
    virtual int visitThrowNode(ThrowNode *node) = 0;
    virtual int visitCatchNode(CatchNode *node) = 0;
    virtual int visitTryNode(TryNode *node) = 0;
    virtual int visitFinallyNode(FinallyNode *node) = 0;
    virtual int visitVarDeclNode(VarDeclNode *node) = 0;
    virtual int visitAssignNode(AssignNode *node) = 0;
    virtual int visitFunctionNode(FunctionNode *node) = 0;
    virtual int visitEmptyNode(EmptyNode *node) = 0;
};

#endif /* AST_NODEVISITOR_H_ */
