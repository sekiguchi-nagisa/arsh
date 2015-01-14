/*
 * Node.h
 *
 *  Created on: 2014/12/31
 *      Author: skgchxngsxyz-osx
 */

#ifndef AST_NODE_H_
#define AST_NODE_H_

#include <utility>

#include "../core/DSType.h"
#include "../core/CalleeHandle.h"
#include "NodeVisitor.h"
#include "TypeToken.h"

class Node {
protected:
    int lineNum;

public:
    Node(int lineNum);
    virtual ~Node();

    int getLineNum();
    virtual int accept(NodeVisitor *visitor) = 0;
};

class ExprNode: public Node {
protected:
    DSType *type;	// initial value is null

public:
    /**
     * type is null if has no type annotation(ex. IntValueNode, StringValueNode)
     */
    ExprNode(int lineNum);
    virtual ~ExprNode();

    void setType(DSType *type);

    /**
     * return null, before type checking
     */
    DSType *getType();
};

class IntValueNode: public ExprNode {
private:
    long value;

public:
    IntValueNode(int lineNum, long value);

    long getValue();
    int accept(NodeVisitor *visitor);	// override
};

class FloatValueNode: public ExprNode {
private:
    double value;

public:
    FloatValueNode(int lineNum, double value);

    double getValue();
    int accept(NodeVisitor *visitor);	// override
};

class BooleanValueNode: public ExprNode {
private:
    bool value;

public:
    BooleanValueNode(int lineNum, bool value);

    bool getValue();
    int accept(NodeVisitor *visitor);	// override
};

class StringValueNode: public ExprNode {	//FIXME:
private:
    std::string value;

public:
    /**
     * used for CommandNode. lineNum is always 0.
     */
    StringValueNode(std::string &&value);

    StringValueNode(int lineNum, char *value, bool isSingleQuoteStr);

    /**
     * call StringValueNode(lineNum, value, false)
     */
    //StringValueNode(int lineNum, char *value);	//FIXME:
    const std::string &getValue();
    int accept(NodeVisitor *visitor);	// override
};

class StringExprNode: public ExprNode {
private:
    std::vector<std::unique_ptr<ExprNode>> nodes;

public:
    StringExprNode(int lineNum);
    ~StringExprNode();

    void addExprNode(std::unique_ptr<ExprNode> &&node);	//TODO:
    const std::vector<std::unique_ptr<ExprNode>> &getExprNodes();
    int accept(NodeVisitor *visitor);	// override
};

class ArrayNode: public ExprNode {
private:
    std::vector<std::unique_ptr<ExprNode>> nodes;

public:
    ArrayNode(int lineNum);
    ~ArrayNode();

    void addExprNode(std::unique_ptr<ExprNode> &&node);
    const std::vector<std::unique_ptr<ExprNode>> &getExprNodes();
    int accept(NodeVisitor *visitor);	//override
};

class MapNode: public ExprNode {
private:
    std::vector<std::unique_ptr<ExprNode>> keyNodes;
    std::vector<std::unique_ptr<ExprNode>> valueNodes;

public:
    MapNode(int lineNum);
    ~MapNode();

    void addEntry(std::unique_ptr<ExprNode> &&keyNode, std::unique_ptr<ExprNode> &&valueNode);
    const std::vector<std::unique_ptr<ExprNode>> &getkeyNodes();
    const std::vector<std::unique_ptr<ExprNode>> &getValueNodes();
    int accept(NodeVisitor *visitor);	// override
};

class PairNode: public ExprNode {
private:
    std::unique_ptr<ExprNode> leftNode;
    std::unique_ptr<ExprNode> rightNode;

public:
    PairNode(int lineNum, std::unique_ptr<ExprNode> &&leftNode,
            std::unique_ptr<ExprNode> &&rightNode);
    ~PairNode();

    const std::unique_ptr<ExprNode> &getLeftNode();
    const std::unique_ptr<ExprNode> &getRightNode();
    int accept(NodeVisitor *visitor);	// override
};

/**
 * base class for SymbolNode, IndexNode, AccessNode
 */
class AssignableNode: public ExprNode {
public:
    AssignableNode(int lineNum);
    ~AssignableNode();

    virtual bool isReadOnly() = 0;
};

class VarNode: public AssignableNode {
private:
    std::string varName;
    bool readOnly;

public:
    VarNode(int lineNum, std::string &&varName);

    const std::string &getVarName();
    bool isReadOnly();	// override
    int accept(NodeVisitor *visitor);	// override
};

class IndexNode: public AssignableNode {//TODO: change getter, setter handle class to FieldHandle
private:
    std::unique_ptr<ExprNode> recvNode;
    std::unique_ptr<ExprNode> indexNode;

    /**
     * for getter method ( __GET__ )
     */
    FunctionHandle *getterHandle;	// not call destructor

    /**
     * for setetr method ( __SET__ )
     */
    FunctionHandle *setterHandle;	// not call destructor

public:
    IndexNode(int lineNum, std::unique_ptr<ExprNode> &&recvNode,
            std::unique_ptr<ExprNode> &&indexNode);
    ~IndexNode();

    const std::unique_ptr<ExprNode> &getRecvNode();
    const std::unique_ptr<ExprNode> &getIndexNode();

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

class AccessNode: public AssignableNode {	//TODO: field handle
private:
    std::unique_ptr<ExprNode> recvNode;
    std::string fieldName;
    FieldHandle *handle;

public:
    AccessNode(int lineNum, std::unique_ptr<ExprNode> &&recvNode, std::string &&fieldName);
    ~AccessNode();

    const std::unique_ptr<ExprNode> &getRecvNode();
    const std::string &getFieldName();
    void setFieldHandle(FieldHandle *handle);

    /**
     * return null before call setFieldHandle
     */
    FieldHandle *getFieldHandle();

    bool isReadOnly();	// oevrride
    int accept(NodeVisitor *visitor);	// override
};

class CastNode: public ExprNode {	//TODO: cast op kind
private:
    std::unique_ptr<ExprNode> targetNode;
    std::unique_ptr<TypeToken> targetTypeToken;

public:
    CastNode(int lineNum, std::unique_ptr<ExprNode> &&targetNode,
            std::unique_ptr<TypeToken> &&type);
    ~CastNode();

    const std::unique_ptr<ExprNode> &getTargetNode();
    const std::unique_ptr<TypeToken> &getTargetTypeToken();    //FIXME: use unique_ptr
    int accept(NodeVisitor *visitor);	//override
};

class InstanceOfNode: public ExprNode {	//TODO: instanceof op kind
private:
    std::unique_ptr<ExprNode> targetNode;
    std::unique_ptr<TypeToken> targetTypeToken;

public:
    InstanceOfNode(int lineNum, std::unique_ptr<ExprNode> &&targetNode,
            std::unique_ptr<TypeToken> &&tyep);
    ~InstanceOfNode();

    const std::unique_ptr<ExprNode> &getTargetNode();
    const std::unique_ptr<TypeToken> &getTargetTypeToken();    //FIXME: use unique_ptr
    int accept(NodeVisitor *visitor);	//override
};

//class OperatorCallNode;	//FIXME: duplicated

class ApplyNode: public ExprNode {	//TODO: function handle, named parameter
private:
    std::unique_ptr<ExprNode> recvNode;
    std::vector<std::unique_ptr<ExprNode>> argNodes;

public:
    ApplyNode(int lineNum, std::unique_ptr<ExprNode> &&recvNode);
    ~ApplyNode();

    const std::unique_ptr<ExprNode> &getRecvNode();

    /**
     * for parser
     */
    void addArgNode(std::unique_ptr<ExprNode> &&node);

    const std::vector<std::unique_ptr<ExprNode>> &getArgNodes();
    int accept(NodeVisitor *visitor);	// override
};

class ConstructorCallNode: public ExprNode {	//TODO: named parameter
private:
    std::unique_ptr<TypeToken> targetType;
    std::vector<std::unique_ptr<ExprNode>> argNodes;
    ConstructorHandle *handle;

public:
    ConstructorCallNode(int lineNum, std::unique_ptr<TypeToken> &&type);
    ~ConstructorCallNode();

    const std::unique_ptr<TypeToken> &getTargetType();
    void addArgNode(std::unique_ptr<ExprNode> &&node);
    const std::vector<std::unique_ptr<ExprNode>> &getArgNodes();
    void setConstructorHandle(ConstructorHandle *handle);

    /**
     * return null before call setConstructorHandle()
     */
    ConstructorHandle *getConstructorHandle();

    int accept(NodeVisitor *visitor);	//override
};

class CondOpNode: public ExprNode {
private:
    std::unique_ptr<ExprNode> leftNode;
    std::unique_ptr<ExprNode> rightNode;

    /**
     * if true, conditional and. otherwise, conditional or
     */
    bool andOp;

public:
    CondOpNode(int lineNum, std::unique_ptr<ExprNode> &&leftNode,
            std::unique_ptr<ExprNode> &&rightNode, bool isAndOp);
    ~CondOpNode();

    const std::unique_ptr<ExprNode> &getLeftNode();
    const std::unique_ptr<ExprNode> &getRightNode();
    bool isAndOp();
    int accept(NodeVisitor *visitor);	//override
};

class ProcessNode: public ExprNode {	//FIXME: redirect option, trace
private:
    std::string commandName;
    std::vector<std::unique_ptr<ExprNode>> argNodes;

public:
    ProcessNode(int lineNum, std::string &&commandName);
    ~ProcessNode();

    const std::string &getCommandName();
    void addArgNode(std::unique_ptr<ExprNode> &&node);
    const std::vector<std::unique_ptr<ExprNode>> &getArgNodes();
    int accept(NodeVisitor *visitor);	//override
};

/**
 * for command(process) argument
 */
class ProcArgNode: public ExprNode {	//TODO: escape sequence
private:
    std::vector<std::unique_ptr<ExprNode>> segmentNodes;

public:
    ProcArgNode(int lineNum);
    ~ProcArgNode();

    void addSegmentNode(std::unique_ptr<ExprNode> &&node);
    const std::vector<std::unique_ptr<ExprNode>> &getSegmentNodes();
    int accept(NodeVisitor *visitor);	// override
};

class SpecialCharNode: public ExprNode {	//FIXME:
public:
    SpecialCharNode(int lineNum);
    ~SpecialCharNode();

    int accept(NodeVisitor *visitor);	//override
};

class TaskNode: public ExprNode {	//TODO: background ...etc
private:
    std::vector<std::unique_ptr<ProcessNode>> procNodes;
    bool background;

public:
    TaskNode();
    ~TaskNode();

    void addProcNodes(std::unique_ptr<ProcessNode> &&node);
    const std::vector<std::unique_ptr<ProcessNode>> &getProcNodes();
    bool isBackground();
    int accept(NodeVisitor *visitor);	//override
};

class InnerTaskNode: public ExprNode {	//FIXME:
private:
    std::unique_ptr<ExprNode> exprNode;

public:
    InnerTaskNode(std::unique_ptr<ExprNode> &&exprNode);
    ~InnerTaskNode();

    const std::unique_ptr<ExprNode> &getExprNode();
    int accept(NodeVisitor *visitor);	//override
};

class AssertNode: public Node {	//FIXME: callee
private:
    std::unique_ptr<ExprNode> exprNode;

public:
    AssertNode(int lineNum, std::unique_ptr<ExprNode> &&exprNode);
    ~AssertNode();

    const std::unique_ptr<ExprNode> &getExprNode();
    int accept(NodeVisitor *visitor);	// override
};

class BlockNode: public Node {
private:
    std::vector<std::unique_ptr<Node>> nodes;

public:
    BlockNode();
    virtual ~BlockNode();

    virtual void addNode(std::unique_ptr<Node> &&node);
    virtual const std::vector<std::unique_ptr<Node>> &getNodes();
    int accept(NodeVisitor *visitor);	// override
};

/**
 * base class for break, continue, return, throw node
 */
class BlockEndNode: public Node {
public:
    BlockEndNode(int lineNum);
};

class BreakNode: public BlockEndNode {
public:
    BreakNode(int lineNum);
    int accept(NodeVisitor *visitor);	// override
};

class ContinueNode: public BlockEndNode {
public:
    ContinueNode(int lineNum);
    int accept(NodeVisitor *visitor);	// override
};

class ExportEnvNode: public Node {	//TODO: callee
private:
    std::string envName;
    std::unique_ptr<ExprNode> exprNode;

public:
    ExportEnvNode(int lineNum, std::string &&envName, std::unique_ptr<ExprNode> &&exprNode);
    ~ExportEnvNode();

    const std::string &getEnvName();
    const std::unique_ptr<ExprNode> &getExprNode();
    int accept(NodeVisitor *visitor);	// override
};

class ImportEnvNode: public Node {	//TODO: callee
private:
    std::string envName;

public:
    ImportEnvNode(int lineNum, std::string &&envName);

    const std::string &getEnvName();
    int accept(NodeVisitor *visitor);	// override
};

/**
 * base class for ForNode, ForInNode, WhileNode
 */
class LoopNode: public Node {
public:
    LoopNode(int lineNum);
};

class ForNode: public LoopNode {
private:
    /**
     * may be empty node
     */
    std::unique_ptr<Node> initNode;

    /**
     * may be empty node
     */
    std::unique_ptr<Node> condNode;

    /**
     * may be empty node
     */
    std::unique_ptr<Node> iterNode;

    std::unique_ptr<BlockNode> blockNode;

public:
    ForNode(int lineNum, std::unique_ptr<Node> &&initNode, std::unique_ptr<Node> &&condNode,
            std::unique_ptr<Node> &&iterNode, std::unique_ptr<BlockNode> &&blockNode);
    ~ForNode();

    const std::unique_ptr<Node> &getInitNode();
    const std::unique_ptr<Node> &getCondNode();
    const std::unique_ptr<Node> &getIterNode();
    const std::unique_ptr<BlockNode> &getBlockNode();
    int accept(NodeVisitor *visitor);	// override
};

class ForInNode: public LoopNode {	//FIXME: callee handle, initName
private:
    std::string initName;
    std::unique_ptr<ExprNode> exprNode;
    std::unique_ptr<BlockNode> blockNode;

    FunctionHandle *resetHandle;	// handle for __RESET__
    FunctionHandle *nextHandle;		// handle for __NEXT__
    FunctionHandle *hasNextHandle;	// handle for __HAS_NEXT__

public:
    ForInNode(int lineNum, std::string &&initName, std::unique_ptr<ExprNode> &&exprNode,
            std::unique_ptr<BlockNode> &&blockNode);
    ~ForInNode();

    const std::string &getInitName();
    const std::unique_ptr<ExprNode> &getExprNode();
    const std::unique_ptr<BlockNode> &getBlockNode();
    void setIteratorHandle(FunctionHandle *resetHandle, FunctionHandle *nextHandle,
            FunctionHandle *hasNextHandle);

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

class WhileNode: public LoopNode {
private:
    std::unique_ptr<ExprNode> condNode;
    std::unique_ptr<BlockNode> blockNode;

    /**
     * if true, this node represent for do-while
     */
    bool asDoWhile;

public:
    WhileNode(int lineNum, std::unique_ptr<ExprNode> &&condNode,
            std::unique_ptr<BlockNode> &&blockNode, bool asDoWhile);
    ~WhileNode();

    const std::unique_ptr<ExprNode> &getCondNode();
    const std::unique_ptr<BlockNode> &getBlockNode();
    bool isDoWhile();
    int accept(NodeVisitor *visitor);	//override
};

class IfNode: public Node {
private:
    std::unique_ptr<ExprNode> condNode;
    std::unique_ptr<BlockNode> thenNode;

    /**
     * may be null, if has no else block
     */
    std::unique_ptr<BlockNode> elseNode;

public:
    /**
     * elseNode may be null
     */
    IfNode(int lineNum, std::unique_ptr<ExprNode> &&condNode, std::unique_ptr<BlockNode> &&thenNode,
            std::unique_ptr<BlockNode> &&elseNode);
    ~IfNode();

    const std::unique_ptr<ExprNode> &getCondNode();
    const std::unique_ptr<BlockNode> &getThenNode();

    /*
     * return EmptyBlockNode, if elseNode is null.
     */
    const std::unique_ptr<BlockNode> &getElseNode();

    int accept(NodeVisitor *visitor);	// override
};

class ReturnNode: public BlockEndNode {
private:
    /**
     * may be null, if has no return value
     */
    std::unique_ptr<ExprNode> exprNode;

public:
    ReturnNode(int lineNum, std::unique_ptr<ExprNode> &&exprNode);
    ReturnNode(int lineNum);
    ~ReturnNode();

    /**
     * return null if has no return value
     */
    const std::unique_ptr<ExprNode> &getExprNode();
    int accept(NodeVisitor *visitor);	// override
};

class ThrowNode: public BlockEndNode {
private:
    std::unique_ptr<ExprNode> exprNode;

public:
    ThrowNode(int lineNum, std::unique_ptr<ExprNode> &&exprNode);
    ~ThrowNode();

    const std::unique_ptr<ExprNode> &getExprNode();
    int accept(NodeVisitor *visitor);	// override
};

class CatchNode: public Node {	//TODO: exception name
private:
    std::string exceptionName;
    std::unique_ptr<TypeToken> typeToken;

    /**
     * may be null, if has no type annotation.
     */
    DSType *exceptionType;

    std::unique_ptr<BlockNode> blockNode;

public:
    /**
     * if type is null, has no type annotation
     */
    CatchNode(int lineNum, std::string &&exceptionName, std::unique_ptr<TypeToken> &&type,
            std::unique_ptr<BlockNode> &&blockNode);
    ~CatchNode();

    const std::string &getExceptionName();
    const std::unique_ptr<TypeToken> &getTypeToken();
    void setExceptionType(DSType *type);

    /**
     * return null if has no exception type
     */
    DSType *getExceptionType();

    const std::unique_ptr<BlockNode> &getBlockNode();
    int accept(NodeVisitor *visitor);	// override
};

class TryNode: public Node {	//TODO: finallyNode
private:
    std::unique_ptr<BlockNode> blockNode;

    /**
     * may be empty
     */
    std::vector<std::unique_ptr<CatchNode>> catchNodes;

    /**
     * may be EmptyNdoe
     */
    std::unique_ptr<Node> finallyNode;

public:
    TryNode(int lineNum, std::unique_ptr<BlockNode> &&blockNode);
    ~TryNode();

    void addCatchNode(std::unique_ptr<CatchNode> &&catchNode);
    const std::vector<std::unique_ptr<CatchNode>> &getCatchNodes();
    void addFinallyNode(std::unique_ptr<Node> &&finallyNode);
    const std::unique_ptr<Node> &getFinallyNode();
    int accept(NodeVisitor *visitor);	// override
};

class FinallyNode: public Node {
private:
    std::unique_ptr<BlockNode> blockNode;

public:
    FinallyNode(int lineNum, std::unique_ptr<BlockNode> &&block);
    ~FinallyNode();

    const std::unique_ptr<BlockNode> &getBlockNode();
    int accept(NodeVisitor *visitor);	// override
};

class VarDeclNode: public Node {
private:
    std::string varName;
    bool readOnly;
    bool global;
    std::unique_ptr<ExprNode> initValueNode;

public:
    VarDeclNode(int lineNum, std::string &&varName, std::unique_ptr<ExprNode> &&initValueNode,
            bool readOnly);
    ~VarDeclNode();

    const std::string &getVarName();
    bool isReadOnly();
    void setGlobal(bool global);
    bool isGlobal();
    const std::unique_ptr<ExprNode> &getInitValueNode();
    int accept(NodeVisitor *visitor);	// override
};

/**
 * for assignment or named parameter
 * assignment is statement, but base class is ExprNode(due to parser).
 * so, after type checking, type is always VoidType
 */
class AssignNode: public ExprNode {	//TODO: assign op, handle
private:
    std::unique_ptr<ExprNode> leftNode;
    std::unique_ptr<ExprNode> rightNode;

    /**
     * if assign op is '=', it is null
     */
    FunctionHandle *handle;	//FIXME

public:
    AssignNode(int lineNum, std::unique_ptr<ExprNode> &&leftNode,
            std::unique_ptr<ExprNode> &&rightNode);
    ~AssignNode();

    const std::unique_ptr<ExprNode> &getLeftNode();

    /**
     * for type checker
     */
    void setRightNode(std::unique_ptr<ExprNode> &&rightNode);
    const std::unique_ptr<ExprNode> &getRightNode();
    void setHandle(FunctionHandle *handle);

    /**
     * return null, before call setHandle()
     */
    FunctionHandle *getHandle();
    int accept(NodeVisitor *visitor);
};

class FunctionNode: public Node {	//FIXME
private:
    std::string funcName;

    /**
     * for parameter definition.
     */
    std::vector<std::unique_ptr<VarNode>> paramNodes;

    /**
     * unresolved type of each parameter
     */
    std::vector<std::unique_ptr<TypeToken>> paramTypeTokens;

    std::unique_ptr<TypeToken> returnTypeToken;

    /**
     * may be null, if VoidType
     */
    DSType *returnType;

    std::unique_ptr<BlockNode> blockNode;

public:
    FunctionNode(int lineNum, std::string &&funcName);
    ~FunctionNode();

    const std::string &getFuncName();
    void addParamNode(std::unique_ptr<VarNode> &&node, std::unique_ptr<TypeToken> &&paramType);
    const std::vector<std::unique_ptr<VarNode>> &getParamNodes();

    /**
     * get unresolved types
     */
    const std::vector<std::unique_ptr<TypeToken>> &getParamTypeTokens();

    void setReturnTypeToken(std::unique_ptr<TypeToken> &&typeToken);
    const std::unique_ptr<TypeToken> &getReturnTypeToken();
    void setReturnType(DSType *returnType);

    /**
     * return null, if has no return type.
     */
    DSType *getReturnType();

    void setBlockNode(std::unique_ptr<BlockNode> &&blockNode);

    /**
     * return null before call setBlockNode()
     */
    const std::unique_ptr<BlockNode> &getBlockNode();

    int accept(NodeVisitor *visitor);	// override
};

// class ClassNode
// class ConstructorNode

class EmptyNode: public ExprNode {	//TODO: EmptyBlockNode
public:
    EmptyNode();

    int accept(NodeVisitor *visitor);	// override
};

class EmptyBlockNode: public BlockNode {
private:
    EmptyBlockNode();

public:
    ~EmptyBlockNode();

    /**
     * do nothing. do not call it
     */
    void addNode(std::unique_ptr<Node> &&node);	// override

    int accept(NodeVisitor *visitor);	// override

    static std::unique_ptr<EmptyBlockNode> emptyBlockNode;
};

/**
 * Root Node of AST.
 * this class is not inheritance of Node
 */
class RootNode {	//FIXME:
private:
    std::vector<std::unique_ptr<Node>> nodes;

public:
    RootNode();
    ~RootNode();

    void addNode(std::unique_ptr<Node> &&node);
    const std::vector<std::unique_ptr<Node>> &getNodes();
};

#endif /* AST_NODE_H_ */
