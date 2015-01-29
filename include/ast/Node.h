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

#ifndef AST_NODE_H_
#define AST_NODE_H_

#include <utility>
#include <list>

#include <core/DSType.h>
#include <core/FieldHandle.h>
#include <ast/NodeVisitor.h>
#include <ast/TypeToken.h>

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
    std::vector<ExprNode*> nodes;

public:
    StringExprNode(int lineNum);
    ~StringExprNode();

    void addExprNode(ExprNode *node);	//TODO:
    const std::vector<ExprNode*> &getExprNodes();
    int accept(NodeVisitor *visitor);	// override
};

class ArrayNode: public ExprNode {
private:
    std::vector<ExprNode*> nodes;

public:
    ArrayNode(int lineNum);
    ~ArrayNode();

    void addExprNode(ExprNode *node);
    const std::vector<ExprNode*> &getExprNodes();
    int accept(NodeVisitor *visitor);	//override
};

class MapNode: public ExprNode {
private:
    std::vector<ExprNode*> keyNodes;
    std::vector<ExprNode*> valueNodes;

public:
    MapNode(int lineNum);
    ~MapNode();

    void addEntry(ExprNode *keyNode, ExprNode *valueNode);
    const std::vector<ExprNode*> &getKeyNodes();
    const std::vector<ExprNode*> &getValueNodes();
    int accept(NodeVisitor *visitor);	// override
};

class PairNode: public ExprNode {
private:
    ExprNode* leftNode;
    ExprNode* rightNode;

public:
    PairNode(int lineNum, ExprNode *leftNode, ExprNode *rightNode);
    ~PairNode();

    ExprNode *getLeftNode();
    ExprNode *getRightNode();
    int accept(NodeVisitor *visitor);	// override
};

/**
 * base class for VarNode, IndexNode, AccessNode
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
    FieldHandle *handle;

public:
    VarNode(int lineNum, std::string &&varName);

    const std::string &getVarName();
    void setHandle(FieldHandle *handle);
    FieldHandle *getHandle();
    bool isReadOnly();	// override
    int accept(NodeVisitor *visitor);	// override
    bool isGlobal();
    int getVarIndex();
};

class AccessNode: public AssignableNode {
private:
    ExprNode* recvNode;
    std::string fieldName;
    FieldHandle *handle;
    int additionalOp;

public:
    AccessNode(ExprNode *recvNode, std::string &&fieldName);
    ~AccessNode();

    ExprNode *getRecvNode();
    void setFieldName(const std::string &fieldName);
    const std::string &getFieldName();
    void setHandle(FieldHandle *handle);
    FieldHandle *getHandle();
    int getFieldIndex();
    bool isReadOnly();	// override
    void setAdditionalOp(int op);
    int getAdditionnalOp();
    int accept(NodeVisitor *visitor);	// override

    const static int NOP               = 0;
    const static int DUP_RECV          = 1;
    /**
     * dup recv node and after field access, swap oprand
     */
    const static int DUP_RECV_AND_SWAP = 2;
};

class CastNode: public ExprNode {	//TODO: cast op kind
private:
    ExprNode *targetNode;
    TypeToken *targetTypeToken;

public:
    CastNode(int lineNum, ExprNode *targetNode, TypeToken *type);
    ~CastNode();

    ExprNode *getTargetNode();
    TypeToken *getTargetTypeToken();
    int accept(NodeVisitor *visitor);	//override
};

class InstanceOfNode: public ExprNode {
private:
    ExprNode* targetNode;
    TypeToken* targetTypeToken;
    DSType *targetType;
    int opKind;

public:
    InstanceOfNode(int lineNum, ExprNode *targetNode, TypeToken *tyep);
    ~InstanceOfNode();

    ExprNode *getTargetNode();
    TypeToken *getTargetTypeToken();

    /**
     * remove type token, and return removed it.
     */
    TypeToken *removeTargetTypeToken();

    void setTargetType(DSType *targetType);
    DSType *getTargetType();
    void resolveOpKind(int opKind);
    int getOpKind();
    int accept(NodeVisitor *visitor);	//override

    const static int ALWAYS_FALSE = 0;
    const static int INSTANCEOF   = 1;
};

/**
 * for unary or binary operator call.
 * operator function must be method.
 */
class OperatorCallNode : public ExprNode {
private:
    /**
     * two or one argument
     */
    std::vector<ExprNode*> argNodes;
    int op;
    FunctionHandle *handle;

public:
    OperatorCallNode(ExprNode *leftNode, int op, ExprNode *rightNode);
    OperatorCallNode(int op, ExprNode *rightNode);
    ~OperatorCallNode();

    const std::vector<ExprNode*> getArgNodes();
    int getOp();
    void setHandle(FunctionHandle *handle);
    FunctionHandle *getHandle();
    int accept(NodeVisitor *visitor);   // override
};

class ArgsNode : public ExprNode {
private:
    std::vector<std::pair<std::string, ExprNode*>> argPairs;

    /**
     * may be null, if not has named parameter.
     * size is equivalent to argsPair.size()
     */
    unsigned int *paramIndexMap;

    /**
     * size of all parameter of callee.
     * may be not equivalent to argPairs.size() if has default parameter.
     */
    unsigned int paramSize;

public:
    ArgsNode(std::string &&paramName, ExprNode* argNode);
    ArgsNode(ExprNode *argNode);
    ~ArgsNode();

    void addArgPair(std::string &&paramName, ExprNode *argNode);

    /**
     * equivalent to this->addArgPair("", argNode)
     */
    void addArg(ExprNode *argNode);

    void initIndexMap();
    void addParamIndex(unsigned int index, unsigned int value);
    unsigned int *getParamIndexMap();
    void setParamSize(unsigned int size);
    unsigned int getParamSize();
    const std::vector<std::pair<std::string, ExprNode*>> &getArgPairs();
    int accept(NodeVisitor *visitor);   // override
};

class ApplyNode: public ExprNode {	//TODO: function handle, named parameter
protected:
    ExprNode* recvNode;
    std::vector<ExprNode*> argNodes;

    /**
     * if true, treat as function call
     */
    bool asFuncCall;

public:
    ApplyNode(ExprNode *recvNode);
    virtual ~ApplyNode();

    ExprNode *getRecvNode();

    /**
     * for parser
     */
    void addArgNode(ExprNode *node);

    const std::vector<ExprNode*> &getArgNodes();
    void setFuncCall(bool asFuncCall);
    bool isFuncCall();
    int accept(NodeVisitor *visitor);	// override
};

class IndexNode : public ApplyNode {
public:
    IndexNode(ExprNode *recvNode, ExprNode *indexNode);
    ~IndexNode();

    ExprNode *getIndexNode();

    /**
     * return this.
     * convert to set index
     */
    ApplyNode *treatAsAssignment(ExprNode *rightNode);
};

/**
 * allocate new DSObject and call constructor.
 */
class NewNode : public ExprNode {
private:
    TypeToken *targetTypeToken;
    std::vector<ExprNode*> argNodes;

public:
    NewNode(int lineNum, TypeToken *targetTypeToken);
    ~NewNode();

    TypeToken *getTargetTypeToken();

    /**
     * remove type token and return removed type token.
     */
    TypeToken *removeTargetTypeToken();

    void addArgNode(ExprNode *argNode);
    const std::vector<ExprNode*> &getArgNodes();

    int accept(NodeVisitor *visitor);   // override
};

class CondOpNode: public ExprNode {
private:
    ExprNode* leftNode;
    ExprNode* rightNode;

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

class ProcessNode: public ExprNode {	//FIXME: redirect option, trace
private:
    std::string commandName;
    std::vector<ProcArgNode*> argNodes;
    std::vector<std::pair<int, ExprNode*>> redirOptions;

public:
    ProcessNode(int lineNum, std::string &&commandName);
    ~ProcessNode();

    const std::string &getCommandName();
    void addArgNode(ProcArgNode *node);
    const std::vector<ProcArgNode*> &getArgNodes();
    void addRedirOption(std::pair<int, ExprNode*> &&optionPair);
    const std::vector<std::pair<int, ExprNode*>> &getRedirOptions();
    int accept(NodeVisitor *visitor);	//override
};

/**
 * for command(process) argument
 */
class ProcArgNode: public ExprNode {	//TODO: escape sequence
private:
    std::vector<ExprNode*> segmentNodes;

public:
    ProcArgNode(int lineNum);
    ~ProcArgNode();

    void addSegmentNode(ExprNode *node);
    const std::vector<ExprNode*> &getSegmentNodes();
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
    std::vector<ProcessNode*> procNodes;
    bool background;

public:
    TaskNode();
    ~TaskNode();

    void addProcNodes(ProcessNode *node);
    const std::vector<ProcessNode*> &getProcNodes();
    bool isBackground();
    int accept(NodeVisitor *visitor);	//override
};

class InnerTaskNode: public ExprNode {	//FIXME:
private:
    ExprNode* exprNode;

public:
    InnerTaskNode(ExprNode *exprNode);
    ~InnerTaskNode();

    ExprNode *getExprNode();
    int accept(NodeVisitor *visitor);	//override
};

class AssertNode: public Node {
private:
    ExprNode *exprNode;

public:
    AssertNode(int lineNum, ExprNode *exprNode);
    ~AssertNode();

    ExprNode *getExprNode();
    int accept(NodeVisitor *visitor);	// override
};

class BlockNode: public Node {
private:
    std::list<Node*> nodeList;

public:
    BlockNode();
    ~BlockNode();

    void addNode(Node *node);
    void insertNodeToFirst(Node *node);
    const std::list<Node*> &getNodeList();
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

class ExportEnvNode: public Node {
private:
    std::string envName;
    ExprNode* exprNode;

public:
    ExportEnvNode(int lineNum, std::string &&envName, ExprNode *exprNode);
    ~ExportEnvNode();

    const std::string &getEnvName();
    ExprNode *getExprNode();
    int accept(NodeVisitor *visitor);	// override
};

class ImportEnvNode: public Node {
private:
    std::string envName;

public:
    ImportEnvNode(int lineNum, std::string &&envName);

    const std::string &getEnvName();
    int accept(NodeVisitor *visitor);	// override
};

/**
 * base class for ForNode, WhileNode
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
    /**
     * initNode may be null.
     * condNode may be null.
     * iterNode may be null.
     */
    ForNode(int lineNum, Node *initNode, Node *condNode, Node *iterNode, BlockNode *blockNode);
    ~ForNode();

    Node *getInitNode();
    Node *getCondNode();
    Node *getIterNode();
    BlockNode *getBlockNode();
    int accept(NodeVisitor *visitor);	// override
};

class WhileNode: public LoopNode {
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

class IfNode: public Node {
private:
    ExprNode *condNode;
    BlockNode *thenNode;

    /**
     * may be null, if has no else block
     */
    BlockNode *elseNode;

public:
    /**
     * elseNode may be null
     */
    IfNode(int lineNum, ExprNode *condNode, BlockNode *thenNode, BlockNode *elseNode);
    ~IfNode();

    ExprNode *getCondNode();
    BlockNode *getThenNode();

    /*
     * return EmptyBlockNode, if elseNode is null.
     */
    BlockNode *getElseNode();

    int accept(NodeVisitor *visitor);	// override
};

class ReturnNode: public BlockEndNode {
private:
    /**
     * may be null, if has no return value
     */
    ExprNode* exprNode;

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

class ThrowNode: public BlockEndNode {
private:
    ExprNode* exprNode;

public:
    ThrowNode(int lineNum, ExprNode *exprNode);
    ~ThrowNode();

    ExprNode *getExprNode();
    int accept(NodeVisitor *visitor);	// override
};

class CatchNode: public Node {	//TODO: exception name
private:
    std::string exceptionName;
    TypeToken *typeToken;

    /**
     * may be null, if has no type annotation.
     */
    DSType *exceptionType;

    BlockNode *blockNode;

public:
    /**
     * if type is null, has no type annotation
     */
    CatchNode(int lineNum, std::string &&exceptionName, TypeToken *type, BlockNode *blockNode);
    ~CatchNode();

    const std::string &getExceptionName();
    TypeToken *getTypeToken();

    /**
     * get type token and set 0.
     */
    TypeToken *removeTypeToken();

    void setExceptionType(DSType *type);

    /**
     * return null if has no exception type
     */
    DSType *getExceptionType();

    BlockNode *getBlockNode();
    int accept(NodeVisitor *visitor);	// override
};

class TryNode: public Node {	//TODO: finallyNode
private:
    BlockNode* blockNode;

    /**
     * may be empty
     */
    std::vector<CatchNode*> catchNodes;

    /**
     * may be EmptyNdoe
     */
    Node* finallyNode;

public:
    TryNode(int lineNum, BlockNode *blockNode);
    ~TryNode();

    BlockNode *getBlockNode();
    void addCatchNode(CatchNode *catchNode);
    const std::vector<CatchNode*> &getCatchNodes();
    void addFinallyNode(Node *finallyNode);
    Node *getFinallyNode();
    int accept(NodeVisitor *visitor);	// override
};

class FinallyNode: public Node {
private:
    BlockNode* blockNode;

public:
    FinallyNode(int lineNum, BlockNode *block);
    ~FinallyNode();

    BlockNode *getBlockNode();
    int accept(NodeVisitor *visitor);	// override
};

class VarDeclNode: public Node {
private:
    std::string varName;
    bool readOnly;
    bool global;
    ExprNode* initValueNode;

public:
    VarDeclNode(int lineNum, std::string &&varName, ExprNode *initValueNode, bool readOnly);
    ~VarDeclNode();

    const std::string &getVarName();
    bool isReadOnly();
    void setGlobal(bool global);
    bool isGlobal();
    ExprNode *getInitValueNode();
    int accept(NodeVisitor *visitor);	// override
};

/**
 * for assignment or named parameter
 * assignment is statement, but base class is ExprNode(due to parser).
 * so, after type checking, type is always VoidType
 */
class AssignNode: public ExprNode {
private:
    /**
     * must be VarNode or AccessNode
     */
    ExprNode* leftNode;

    ExprNode* rightNode;

public:
    AssignNode(ExprNode *leftNode, ExprNode *rightNode);
    ~AssignNode();

    ExprNode *getLeftNode();
    ExprNode *getRightNode();
    int accept(NodeVisitor *visitor);   // override
};

/**
 * for field self assignment.
 * field self assignment is statement, but class is ExprNode(due to parser).
 */
class FieldSelfAssignNode : public ExprNode {
private:
    /**
     * applyNode->getRecvNode must be AccessNode.
     */
    ApplyNode *applyNode;

public:
    FieldSelfAssignNode(ApplyNode *applyNode);
    ~FieldSelfAssignNode();

    ApplyNode *getApplyNode();
    int accept(NodeVisitor *visitor);   // override
};

class FunctionNode: public Node {	//FIXME
private:
    std::string funcName;

    /**
     * for parameter definition.
     */
    std::vector<VarNode*> paramNodes;

    /**
     * unresolved type of each parameter
     */
    std::vector<TypeToken*> paramTypeTokens;

    TypeToken *returnTypeToken;

    /**
     * may be null, if VoidType
     */
    DSType *returnType;

    BlockNode *blockNode;

public:
    FunctionNode(int lineNum, std::string &&funcName);
    ~FunctionNode();

    const std::string &getFuncName();
    void addParamNode(VarNode *node, TypeToken *paramType);
    const std::vector<VarNode*> &getParamNodes();

    /**
     * get unresolved types
     */
    const std::vector<TypeToken*> &getParamTypeTokens();

    void setReturnTypeToken(TypeToken *typeToken);
    TypeToken *getReturnTypeToken();
    void setReturnType(DSType *returnType);

    /**
     * return null, if has no return type.
     */
    DSType *getReturnType();

    void setBlockNode(BlockNode *blockNode);

    /**
     * return null before call setBlockNode()
     */
    BlockNode *getBlockNode();

    int accept(NodeVisitor *visitor);	// override
};

// class ClassNode
// class ConstructorNode

class EmptyNode: public ExprNode {
public:
    EmptyNode();

    int accept(NodeVisitor *visitor);	// override
};

/**
 * Root Node of AST.
 * this class is not inheritance of Node
 */
class RootNode {	//FIXME:
private:
    std::list<Node*> nodeList;

public:
    RootNode();
    ~RootNode();

    void addNode(Node *node);
    const std::list<Node*> &getNodeList();
};

#endif /* AST_NODE_H_ */
