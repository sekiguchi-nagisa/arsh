/*
 * Evaluator.h
 *
 *  Created on: 2015/01/07
 *      Author: skgchxngsxyz-osx
 */

#ifndef EVAL_EVALUATOR_H_
#define EVAL_EVALUATOR_H_

#include "../core/DSType.h"
#include "../core/CalleeHandle.h"
#include "../ast/Node.h"
#include "../ast/NodeVisitor.h"

class Evaluator : public NodeVisitor {
public:
	Evaluator();
	virtual ~Evaluator();

	int visitIntValueNode       (IntValueNode        *node); // override
	int visitFloatValueNode     (FloatValueNode      *node); // override
	int visitBooleanValueNode   (BooleanValueNode    *node); // override
	int visitStringValueNode    (StringValueNode     *node); // override
	int visitStringExprNode     (StringExprNode      *node); // override
	int visitArrayNode          (ArrayNode           *node); // override
	int visitMapNode            (MapNode             *node); // override
	int visitPairNode           (PairNode            *node); // override
	int visitSymbolNode         (SymbolNode          *node); // override
	int visitIndexNode          (IndexNode           *node); // override
	int visitAccessNode         (AccessNode          *node); // override
	int visitCastNode           (CastNode            *node); // override
	int visitInstanceOfNode     (InstanceOfNode      *node); // override
	int visitApplyNode          (ApplyNode           *node); // override
	int visitConstructorCallNode(ConstructorCallNode *node); // override
	int visitCondOpNode         (CondOpNode          *node); // override
	int visitProcessNode        (ProcessNode         *node); // override
	int visitProcArgNode        (ProcArgNode         *node); // override
	int visitSpecialCharNode    (SpecialCharNode     *node); // override
	int visitTaskNode           (TaskNode            *node); // override
	int visitInnerTaskNode      (InnerTaskNode       *node); // override
	int visitAssertNode         (AssertNode          *node); // override
	int visitBlockNode          (BlockNode           *node); // override
	int visitBreakNode          (BreakNode           *node); // override
	int visitContinueNode       (ContinueNode        *node); // override
	int visitExportEnvNode      (ExportEnvNode       *node); // override
	int visitImportEnvNode      (ImportEnvNode       *node); // override
	int visitForNode            (ForNode             *node); // override
	int visitForInNode          (ForInNode           *node); // override
	int visitWhileNode          (WhileNode           *node); // override
	int visitIfNode             (IfNode              *node); // override
	int visitReturnNode         (ReturnNode          *node); // override
	int visitThrowNode          (ThrowNode           *node); // override
	int visitCatchNode          (CatchNode           *node); // override
	int visitTryNode            (TryNode             *node); // override
	int visitFinallyNode        (FinallyNode         *node); // override
	int visitVarDeclNode        (VarDeclNode         *node); // override
	int visitAssignNode         (AssignNode          *node); // override
	int visitFunctionNode       (FunctionNode        *node); // override
	int visitEmptyNode          (EmptyNode           *node); // override
};

#endif /* EVAL_EVALUATOR_H_ */
