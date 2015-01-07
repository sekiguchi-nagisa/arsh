/*
 * Node.h
 *
 *  Created on: 2014/12/31
 *      Author: skgchxngsxyz-osx
 */

#ifndef AST_NODE_H_
#define AST_NODE_H_

#include "../core/DSType.h"
#include "../core/CalleeHandle.h"
#include "NodeVisitor.h"

class Node {
protected:
	int lineNum;

public:
	Node(int lineNum);
	virtual ~Node();

	int getLineNum();
	virtual int accept(NodeVisitor *visitor) = 0;
};


class ExprNode : public Node {
protected:
	DSType *type;	// initial value is UnresolvedType

public:
	/**
	 * type is null if has no type annotation(ex. IntValueNode, StringValueNode)
	 */
	ExprNode(int lineNum, UnresolvedType *type);
	virtual ~ExprNode();

	void setType(DSType *type);
	DSType *getType();	// UnresolvedType before type checking
};


class IntValueNode : public ExprNode {
private:
	long value;

public:
	IntValueNode(int lineNum, long value);

	long getValue();
	int accept(NodeVisitor *visitor);	// override
};


class FloatValueNode : public ExprNode {
private:
	double value;

public:
	FloatValueNode(int lineNum, double value);

	double getValue();
	int accept(NodeVisitor *visitor);	// override
};


class BooleanValueNode : public ExprNode {
private:
	bool value;

public:
	BooleanValueNode(int lineNum, bool value);

	bool getValue();
	int accept(NodeVisitor *visitor);	// override
};


class StringValueNode : public ExprNode {	//FIXME:
private:
	std::string value;

public:
	/**
	 * used for CommandNode. lineNum is always 0.
	 */
	StringValueNode(std::string value);

	StringValueNode(int lineNum, char *value, bool isSingleQuoteStr);

	/**
	 * call StringValueNode(lineNum, value, false)
	 */
	//StringValueNode(int lineNum, char *value);	//FIXME:

	const std::string &getValue();
	int accept(NodeVisitor *visitor);	// override
};


class StringExprNode : public ExprNode {
private:
	std::vector<ExprNode*> nodes;

public:
	StringExprNode(int lineNum);
	~StringExprNode();

	void addExprNode(ExprNode *node);	//TODO:
	const std::vector<ExprNode*> &getExprNodes();
	int accept(NodeVisitor *visitor);	// override
};


class ArrayNode : public ExprNode {
private:
	std::vector<ExprNode*> nodes;

public:
	ArrayNode(int lineNum);
	~ArrayNode();

	void addExprNode(ExprNode *node);
	const std::vector<ExprNode*> &getExprNodes();
	int accept(NodeVisitor *visitor);	//override
};


class MapNode : public ExprNode {
private:
	std::vector<ExprNode*> keyNodes;
	std::vector<ExprNode*> valueNodes;

public:
	MapNode(int lineNum);
	~MapNode();

	void addEntry(ExprNode *keyNode, ExprNode *valueNode);
	const std::vector<ExprNode*> &getkeyNodes();
	const std::vector<ExprNode*> &getValueNodes();
	int accept(NodeVisitor *visitor);	// override
};


class PairNode : public ExprNode {
private:
	ExprNode *leftNode;
	ExprNode *rightNode;

public:
	PairNode(int lineNum, ExprNode *leftNode, ExprNode *rightNode);
	~PairNode();

	ExprNode *getLeftNode();
	ExprNode *getRightNode();
	int accept(NodeVisitor *visitor);	// override
};


/**
 * base class for SymbolNode, IndexNode, AccessNode
 */
class AssignableNode : public ExprNode {
public:
	AssignableNode(int lineNum);
	~AssignableNode();

	virtual bool isReadOnly() = 0;
};


class SymbolNode : public AssignableNode {	//TODO: symbol entry, rename to VarNode
private:
	std::string varName;
	bool readOnly;

public:
	SymbolNode(int lineNum, std::string varName);

	const std::string &getVarName();
	bool isReadOnly();	// override
	int accept(NodeVisitor *visitor);	// override
};


class IndexNode : public AssignableNode {	//TODO: change getter, setter handle class to FieldHandle
private:
	ExprNode *recvNode;
	ExprNode *indexNode;

	/**
	 * for getter method ( __GET__ )
	 */
	FunctionHandle *getterHandle;	// not call destructor

	/**
	 * for setetr method ( __SET__ )
	 */
	FunctionHandle *setterHandle;	// not call destructor

public:
	IndexNode(int lineNum, ExprNode *recvNode, ExprNode *indexNode);
	~IndexNode();

	ExprNode *getRecvNode();
	ExprNode *getIndexNode();

	void setGetterHandle(FunctionHandle *handle);

	/**
	 * return null before call setGetterHandle
	 */
	FunctionHandle *getGetterHandle();

	void setSetterHandle(FunctionHandle *handle);

	/**
	 * return null before call setSetterHandle
	 */
	FunctionHandle *getSetterHandle();

	/**
	 * return always false
	 */
	bool isReadOnly();	// override

	int accept(NodeVisitor *visitor);	//override
};


class AccessNode : public AssignableNode {	//TODO: field handle
private:
	ExprNode *recvNode;
	std::string fieldName;
	FieldHandle *handle;

public:
	AccessNode(int lineNum, ExprNode *recvNode, std::string fieldName);
	~AccessNode();

	ExprNode *getRecvNode();
	const std::string &getFieldName();
	void setFieldHandle(FieldHandle *handle);

	/**
	 * return null before call setFieldHandle
	 */
	FieldHandle *getFieldHandle();

	bool isReadOnly();	// oevrride
	int accept(NodeVisitor *visitor);	// override
};


class CastNode : public ExprNode {	//TODO: cast op kind
private:
	ExprNode *targetNode;

public:
	CastNode(int lineNum, ExprNode *targetNode, UnresolvedType *type);
	~CastNode();

	ExprNode *getTargetNode();
	int accept(NodeVisitor *visitor);	//override
};


class InstanceOfNode : public ExprNode {	//TODO: instanceof op kind
private:
	ExprNode *targetNode;

public:
	InstanceOfNode(int lineNum, ExprNode *targetNode, UnresolvedType *tyep);
	~InstanceOfNode();

	ExprNode *getTargetNode();
	int accept(NodeVisitor *visitor);	//override
};


//class OperatorCallNode;	//FIXME: duplicated

class ApplyNode : public ExprNode {	//TODO: function handle, named parameter
private:
	ExprNode *recvNode;
	std::vector<ExprNode*> argNodes;

public:
	ApplyNode(int lineNum, ExprNode *recvNode);
	~ApplyNode();

	ExprNode *getRecvNode();

	/**
	 * for parser
	 */
	void addArgNode(ExprNode *node);

	const std::vector<ExprNode*> &getArgNodes();
	int accept(NodeVisitor *visitor);	// override
};


class ConstructorCallNode : public ExprNode {	//TODO: named parameter
private:
	std::vector<ExprNode*> argNodes;
	ConstructorHandle *handle;

public:
	ConstructorCallNode(int lineNum, UnresolvedType *type);
	~ConstructorCallNode();

	void addArgNode(ExprNode *node);
	const std::vector<ExprNode*> &getArgNodes();
	void setConstructorHandle(ConstructorHandle *handle);

	/**
	 * return null before call setConstructorHandle()
	 */
	ConstructorHandle *getConstructorHandle();

	int accept(NodeVisitor *visitor);	//override
};


class CondOpNode : public ExprNode {
private:
	ExprNode *leftNode;
	ExprNode *rightNode;

	/**
	 * if true, conditional and. otherwise, conditional or
	 */
	bool andOp;

public:
	CondOpNode(int lineNum, ExprNode *leftNode, ExprNode *rightNode, bool isAndOp);
	~CondOpNode();

	ExprNode *getLeftNode();
	ExprNode *getRightNode();
	bool isAndOp();
	int accept(NodeVisitor *visitor);	//override
};


class ProcessNode : public ExprNode {	//FIXME: redirect option, trace
private:
	std::string commandName;
	std::vector<ExprNode*> argNodes;

public:
	ProcessNode(int lineNum, std::string commandName);
	~ProcessNode();

	const std::string &getCommandName();
	void addArgNode(ExprNode *node);
	const std::vector<ExprNode*> &getArgNodes();
	int accept(NodeVisitor *visitor);	//override
};

/**
 * for command(process) argument
 */
class ArgumentNode : public ExprNode {	//TODO: escape sequence
private:
	std::vector<ExprNode*> segmentNodes;

public:
	ArgumentNode(int lineNum);
	~ArgumentNode();

	void addSegmentNode(ExprNode *node);
	const std::vector<ExprNode*> &getSegmentNodes();
	int accept(NodeVisitor *visitor);	// override
};


class SpecialCharNode : public ExprNode {	//FIXME:
public:
	SpecialCharNode(int lineNum);
	~SpecialCharNode();

	int accept(NodeVisitor *visitor);	//override
};


class TaskNode : public ExprNode {	//TODO: background ...etc
private:
	std::vector<ProcessNode*> procNodes;
	bool background;

public:
	TaskNode();
	~TaskNode();

	void addProcNodes(ProcessNode* node);
	const std::vector<ProcessNode*> &getProcNodes();
	bool isBackground();
	int accept(NodeVisitor *visitor);	//override
};


class InnerTaskNode : public ExprNode {	//FIXME:
private:
	ExprNode *exprNode;

public:
	InnerTaskNode(ExprNode *exprNode);
	~InnerTaskNode();

	ExprNode *getExprNode();
	int accept(NodeVisitor *visitor);	//override
};


class AssertNode : public Node {	//FIXME: callee
private:
	ExprNode *exprNode;

public:
	AssertNode(int lineNum, ExprNode *exprNode);
	~AssertNode();

	ExprNode *getExprNode();
	int accept(NodeVisitor *visitor);	// override
};


class BlockNode : public Node {
private:
	std::vector<Node*> nodes;

public:
	BlockNode();
	~BlockNode();

	void addNode(Node *node);
	const std::vector<Node*> &getNodes();
	int accept(NodeVisitor *visitor);	// override
};


/**
 * base class for break, continue, return, throw node
 */
class BlockEndNode : public Node {
public:
	BlockEndNode(int lineNum);
};


class BreakNode : public BlockEndNode {
public:
	BreakNode(int lineNum);
	int accept(NodeVisitor *visitor);	// override
};


class ContinueNode : public BlockEndNode {
public:
	ContinueNode(int lineNum);
	int accept(NodeVisitor *visitor);	// override
};


class ExportEnvNode : public Node {	//TODO: callee
private:
	std::string envName;
	ExprNode *exprNode;

public:
	ExportEnvNode(int lineNum, std::string envName, ExprNode *exprNode);
	~ExportEnvNode();

	const std::string &getEnvName();
	ExprNode *getExprNode();
	int accept(NodeVisitor *visitor);	// override
};


class ImportEnvNode : public Node {	//TODO: callee
private:
	std::string envName;

public:
	ImportEnvNode(int lineNum, std::string envName);

	const std::string getEnvName();
	int accept(NodeVisitor *visitor);	// override
};


/**
 * base class for ForNode, ForInNode, WhileNode
 */
class LoopNode : public Node {
public:
	LoopNode(int lineNum);
};


class ForNode : public LoopNode {
private:
	/**
	 * may be empty node
	 */
	Node *initNode;

	/**
	 * may be empty node
	 */
	Node *condNode;

	/**
	 * may be empty node
	 */
	Node *iterNode;

	BlockNode *blockNode;

public:
	ForNode(int lineNum, Node *initNode, Node *condNode, Node *iterNode, BlockNode *blockNode);
	~ForNode();

	Node *getInitNode();
	Node *getCondNode();
	Node *getIterNode();
	BlockNode *getBlockNode();
	int accept(NodeVisitor *visitor);	// override
};


class ForInNode : public LoopNode {	//FIXME: callee handle, initName
private:
	std::string initName;
	ExprNode *exprNode;
	BlockNode *blockNode;

	FunctionHandle *resetHandle;	// handle for __RESET__
	FunctionHandle *nextHandle;		// handle for __NEXT__
	FunctionHandle *hasNextHandle;	// handle for __HAS_NEXT__

public:
	ForInNode(int lineNum, std::string initName, ExprNode *exprNode, BlockNode *blockNode);
	~ForInNode();

	const std::string &getInitName();
	ExprNode *getExprNode();
	BlockNode *getBlockNode();
	void setIteratorHandle(FunctionHandle *resetHandle, FunctionHandle *nextHandle, FunctionHandle *hasNextHandle);

	/**
	 * return null before call setIteratorHandle()
	 */
	FunctionHandle *getResetHandle();

	/**
	 * return null before call setIteratorHandle()
	 */
	FunctionHandle *getNextHandle();

	/**
	 * return null before call setIteratorHandle()
	 */
	FunctionHandle *getHasNextHandle();

	int accept(NodeVisitor *visitor);	// override
};


class WhileNode : public LoopNode {
private:
	ExprNode *condNode;
	BlockNode *blockNode;

	/**
	 * if true, this node represent for do-while
	 */
	bool asDoWhile;

public:
	WhileNode(int lineNum, ExprNode *condNode, BlockNode *blockNode, bool asDoWhile);
	~WhileNode();

	ExprNode *getCondNode();
	BlockNode *getBlockNode();
	bool isDoWhile();
	int accept(NodeVisitor *visitor);	//override
};


class IfNode : public Node {
private:
	ExprNode *condNode;
	BlockNode *thenNode;

	/**
	 * may be EmptyBlockNode
	 */
	BlockNode *elseNode;

public:
	IfNode(int lineNum, ExprNode *condNode, BlockNode *thenNode, BlockNode *elseNode);
	~IfNode();

	ExprNode *getCondNode();
	BlockNode *getThenNode();
	BlockNode *getElseNode();
	int accept(NodeVisitor *visitor);	// override
};


class ReturnNode : public BlockEndNode {
private:
	/**
	 * may be null, if has no return value
	 */
	ExprNode *exprNode;

public:
	ReturnNode(int lineNum, ExprNode *exprNode);
	ReturnNode(int lineNum);
	~ReturnNode();

	/**
	 * return null if has no return value
	 */
	ExprNode *getExprNode();
	int accept(NodeVisitor *visitor);	// override
};


class ThrowNode : public BlockEndNode {
private:
	ExprNode *exprNode;

public:
	ThrowNode(int lineNum, ExprNode *exprNode);
	~ThrowNode();

	ExprNode *getExprNode();
	int accept(NodeVisitor *visitor);	// override
};


class CatchNode : public Node {	//TODO: exception name
private:
	std::string exceptionName;

	/**
	 * may be null, if has no type annotation.
	 */
	DSType *exceptionType;

	BlockNode *blockNode;

public:
	/**
	 * if type is null, has no type annotation
	 */
	CatchNode(int lineNum, std::string exceptionName, UnresolvedType *type, BlockNode *blockNode);
	~CatchNode();

	const std::string &getExceptionName();
	void setExceptionType(DSType *type);

	/**
	 * return null if has no exception type
	 */
	DSType *getExceptionType();

	BlockNode *getBlockNode();
	int accept(NodeVisitor *visitor);	// override
};

class TryNode : public Node {	//TODO: finallyNode
private:
	BlockNode *blockNode;

	/**
	 * may be empty
	 */
	std::vector<CatchNode*> catchNodes;

	/**
	 * may be EmptyNdoe
	 */
	Node *finallyNode;

public:
	TryNode(int lineNum, BlockNode *blockNode);
	~TryNode();

	void addCatchNode(CatchNode *catchNode);
	const std::vector<CatchNode*> getCatchNodes();
	void addFinallyNode(Node *finallyNode);
	Node *getFinallyNode();
	int accept(NodeVisitor *visitor);	// override
};


class FinallyNode : public Node {
private:
	BlockNode *blockNode;

public:
	FinallyNode(int lineNum, BlockNode *block);
	~FinallyNode();

	BlockNode *getBlockNode();
	int accept(NodeVisitor *visitor);	// override
};


#endif /* AST_NODE_H_ */
