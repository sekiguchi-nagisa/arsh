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
class StringValueNode;
class StringExprNode;
class ArrayNode;
class MapNode;
class TupleNode;
class VarNode;
class AccessNode;
class CastNode;
class InstanceOfNode;
class BinaryOpNode;
class ArgsNode;
class ApplyNode;
class NewNode;
class CondOpNode;
class CmdNode;
class CmdArgNode;
class SpecialCharNode;
class PipedCmdNode;
class CmdContextNode;

class AssertNode;
class BlockNode;
class BreakNode;
class ContinueNode;
class ExportEnvNode;
class ImportEnvNode;
class ForNode;
class WhileNode;
class DoWhileNode;
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
class DummyNode;

class NodeVisitor {
public:
    NodeVisitor();
    virtual ~NodeVisitor();

    virtual void visitIntValueNode(IntValueNode *node) = 0;
    virtual void visitFloatValueNode(FloatValueNode *node) = 0;
    virtual void visitStringValueNode(StringValueNode *node) = 0;
    virtual void visitStringExprNode(StringExprNode *node) = 0;
    virtual void visitArrayNode(ArrayNode *node) = 0;
    virtual void visitMapNode(MapNode *node) = 0;
    virtual void visitTupleNode(TupleNode *node) = 0;
    virtual void visitVarNode(VarNode *node) = 0;
    virtual void visitAccessNode(AccessNode *node) = 0;
    virtual void visitCastNode(CastNode *node) = 0;
    virtual void visitInstanceOfNode(InstanceOfNode *node) = 0;
    virtual void visitBinaryOpNode(BinaryOpNode *node) = 0;
    virtual void visitArgsNode(ArgsNode *node) = 0;
    virtual void visitApplyNode(ApplyNode *node) = 0;
    virtual void visitNewNode(NewNode *node) = 0;
    virtual void visitCondOpNode(CondOpNode *node) = 0;
    virtual void visitCmdNode(CmdNode *node) = 0;
    virtual void visitCmdArgNode(CmdArgNode *node) = 0;
    virtual void visitSpecialCharNode(SpecialCharNode *node) = 0;
    virtual void visitPipedCmdNode(PipedCmdNode *node) = 0;
    virtual void visitCmdContextNode(CmdContextNode *node) = 0;
    virtual void visitAssertNode(AssertNode *node) = 0;
    virtual void visitBlockNode(BlockNode *node) = 0;
    virtual void visitBreakNode(BreakNode *node) = 0;
    virtual void visitContinueNode(ContinueNode *node) = 0;
    virtual void visitExportEnvNode(ExportEnvNode *node) = 0;
    virtual void visitImportEnvNode(ImportEnvNode *node) = 0;
    virtual void visitForNode(ForNode *node) = 0;
    virtual void visitWhileNode(WhileNode *node) = 0;
    virtual void visitDoWhileNode(DoWhileNode *node) = 0;
    virtual void visitIfNode(IfNode *node) = 0;
    virtual void visitReturnNode(ReturnNode *node) = 0;
    virtual void visitThrowNode(ThrowNode *node) = 0;
    virtual void visitCatchNode(CatchNode *node) = 0;
    virtual void visitTryNode(TryNode *node) = 0;
    virtual void visitFinallyNode(FinallyNode *node) = 0;
    virtual void visitVarDeclNode(VarDeclNode *node) = 0;
    virtual void visitAssignNode(AssignNode *node) = 0;
    virtual void visitFunctionNode(FunctionNode *node) = 0;
    virtual void visitEmptyNode(EmptyNode *node) = 0;
    virtual void visitDummyNode(DummyNode *node) = 0;
};

#endif /* AST_NODEVISITOR_H_ */
