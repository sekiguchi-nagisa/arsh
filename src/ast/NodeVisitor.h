/*
 * NodeVisitor.h
 *
 *  Created on: 2014/12/31
 *      Author: skgchxngsxyz-osx
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
class SymbolNode;
class IndexNode;
class AccessNode;
class CastNode;
class InstanceOfNode;
class ApplyNode;
class ConstructorCallNode;
class CondOpNode;
class ProcessNode;
class ArgumentNode;
class SpecialCharNode;
class TaskNode;
class InnerTaskNode;


class NodeVisitor {
public:
	NodeVisitor();
	virtual ~NodeVisitor();

	virtual int visitIntValueNode       (IntValueNode        *node) = 0;
	virtual int visitFloatValueNode     (FloatValueNode      *node) = 0;
	virtual int visitBooleanValueNode   (BooleanValueNode    *node) = 0;
	virtual int visitStringValueNode    (StringValueNode     *node) = 0;
	virtual int visitStringExprNode     (StringExprNode      *node) = 0;
	virtual int visitArrayNode          (ArrayNode           *node) = 0;
	virtual int visitMapNode            (MapNode             *node) = 0;
	virtual int visitPairNode           (PairNode            *node) = 0;
	virtual int visitSymbolNode         (SymbolNode          *node) = 0;
	virtual int visitIndexNode          (IndexNode           *node) = 0;
	virtual int visitAccessNode         (AccessNode          *node) = 0;
	virtual int visitCastNode           (CastNode            *node) = 0;
	virtual int visitInstanceOfNode     (InstanceOfNode      *node) = 0;
	virtual int visitApplyNode          (ApplyNode           *node) = 0;
	virtual int visitConstructorCallNode(ConstructorCallNode *node) = 0;
	virtual int visitCondOpNode         (CondOpNode          *node) = 0;
	virtual int visitProcessNode        (ProcessNode         *node) = 0;
	virtual int visitArgumentNode       (ArgumentNode        *node) = 0;
	virtual int visitSpecialCharNode    (SpecialCharNode     *node) = 0;
	virtual int visitTaskNode           (TaskNode            *node) = 0;
	virtual int visitInnerTaskNode      (InnerTaskNode       *node) = 0;
};

#endif /* AST_NODEVISITOR_H_ */
