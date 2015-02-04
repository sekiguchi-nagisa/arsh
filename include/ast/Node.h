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

    /**
     * initial value is null.
     */
    DSType *type;

public:
    Node(int lineNum);
    virtual ~Node();

    int getLineNum();
    void setType(DSType *type);

    /**
     * return null, before type checking
     */
    DSType *getType();
    virtual int accept(NodeVisitor *visitor) = 0;
};

// expression definition

class IntValueNode: public Node {
private:
    long value;

public:
    IntValueNode(int lineNum, long value);

    long getValue();
    int accept(NodeVisitor *visitor);	// override
};

class FloatValueNode: public Node {
private:
    double value;

public:
    FloatValueNode(int lineNum, double value);

    double getValue();
    int accept(NodeVisitor *visitor);	// override
};

class BooleanValueNode: public Node {
private:
    bool value;

public:
    BooleanValueNode(int lineNum, bool value);

    bool getValue();
    int accept(NodeVisitor *visitor);	// override
};

class StringValueNode: public Node {	//FIXME:
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

class StringExprNode: public Node {
private:
    std::vector<Node*> nodes;

public:
    StringExprNode(int lineNum);
    ~StringExprNode();

    void addExprNode(Node *node);	//TODO:
    const std::vector<Node*> &getExprNodes();
    int accept(NodeVisitor *visitor);	// override
};

class ArrayNode: public Node {
private:
    std::vector<Node*> nodes;

public:
    ArrayNode(int lineNum);
    ~ArrayNode();

    void addExprNode(Node *node);
    const std::vector<Node*> &getExprNodes();
    int accept(NodeVisitor *visitor);	//override
};

class MapNode: public Node {
private:
    std::vector<Node*> keyNodes;
    std::vector<Node*> valueNodes;

public:
    MapNode(int lineNum);
    ~MapNode();

    void addEntry(Node *keyNode, Node *valueNode);
    const std::vector<Node*> &getKeyNodes();
    const std::vector<Node*> &getValueNodes();
    int accept(NodeVisitor *visitor);	// override
};

class PairNode: public Node {
private:
    Node* leftNode;
    Node* rightNode;

public:
    PairNode(int lineNum, Node *leftNode, Node *rightNode);
    ~PairNode();

    Node *getLeftNode();
    Node *getRightNode();
    int accept(NodeVisitor *visitor);	// override
};

/**
 * base class for VarNode, IndexNode, AccessNode
 */
class AssignableNode: public Node {
protected:
    bool readOnly;

public:
    AssignableNode(int lineNum);
    ~AssignableNode();

    bool isReadOnly();
};

class VarNode: public AssignableNode {
private:
    std::string varName;
    bool global;
    int varIndex;

public:
    VarNode(int lineNum, std::string &&varName);

    const std::string &getVarName();
    void setAttribute(FieldHandle *handle);
    int accept(NodeVisitor *visitor);	// override
    bool isGlobal();
    int getVarIndex();
};

class AccessNode: public AssignableNode {
private:
    Node* recvNode;
    std::string fieldName;
    int fieldIndex;
    int additionalOp;

public:
    AccessNode(Node *recvNode, std::string &&fieldName);
    ~AccessNode();

    Node *getRecvNode();
    void setFieldName(const std::string &fieldName);
    const std::string &getFieldName();
    void setAttribute(FieldHandle *handle);
    int getFieldIndex();
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

class CastNode: public Node {	//TODO: cast op kind
private:
    Node *targetNode;
    TypeToken *targetTypeToken;

public:
    CastNode(int lineNum, Node *targetNode, TypeToken *type);
    ~CastNode();

    Node *getTargetNode();
    TypeToken *getTargetTypeToken();
    int accept(NodeVisitor *visitor);	//override
};

class InstanceOfNode: public Node {
private:
    Node* targetNode;
    TypeToken* targetTypeToken;
    DSType *targetType;
    int opKind;

public:
    InstanceOfNode(int lineNum, Node *targetNode, TypeToken *tyep);
    ~InstanceOfNode();

    Node *getTargetNode();
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
class OperatorCallNode : public Node {
private:
    /**
     * two or one argument
     */
    std::vector<Node*> argNodes;
    int op;
    FunctionHandle *handle;

public:
    OperatorCallNode(Node *leftNode, int op, Node *rightNode);
    OperatorCallNode(int op, Node *rightNode);
    ~OperatorCallNode();

    const std::vector<Node*> getArgNodes();
    int getOp();
    void setHandle(FunctionHandle *handle);
    FunctionHandle *getHandle();
    int accept(NodeVisitor *visitor);   // override
};

class ArgsNode : public Node {
private:
    std::vector<std::pair<std::string, Node*>> argPairs;

    /**
     * may be null, if not has named parameter.
     * size is equivalent to argsPair.size().
     * key is order of arg, value is parameter index.
     */
    unsigned int *paramIndexMap;

    /**
     * size of all parameter of callee.
     * may be not equivalent to argPairs.size() if has default parameter.
     */
    unsigned int paramSize;

public:
    ArgsNode(int lineNum);
    ArgsNode(std::string &&paramName, Node* argNode);
    ArgsNode(Node *argNode);
    ~ArgsNode();

    void addArgPair(std::string &&paramName, Node *argNode);

    /**
     * equivalent to this->addArgPair("", argNode)
     */
    void addArg(Node *argNode);

    void initIndexMap();
    void addParamIndex(unsigned int index, unsigned int value);
    unsigned int *getParamIndexMap();
    void setParamSize(unsigned int size);
    unsigned int getParamSize();
    const std::vector<std::pair<std::string, Node*>> &getArgPairs();
    int accept(NodeVisitor *visitor);   // override
};

class ApplyNode: public Node {	//TODO: function handle, named parameter
protected:
    Node *recvNode;
    ArgsNode *argsNode;

    /**
     * if true, treat as function call
     */
    bool asFuncCall;

public:
    ApplyNode(Node *recvNode, ArgsNode *argsNode);
    virtual ~ApplyNode();

    Node *getRecvNode();
    ArgsNode *getArgsNode();
    void setFuncCall(bool asFuncCall);
    bool isFuncCall();
    int accept(NodeVisitor *visitor);	// override
};

class IndexNode : public ApplyNode {
public:
    IndexNode(Node *recvNode, Node *indexNode);
    ~IndexNode();

    Node *getIndexNode();

    /**
     * return this.
     * convert to set index
     */
    ApplyNode *treatAsAssignment(Node *rightNode);
};

/**
 * allocate new DSObject and call constructor.
 */
class NewNode : public Node {
private:
    TypeToken *targetTypeToken;
    ArgsNode *argsNode;

public:
    NewNode(int lineNum, TypeToken *targetTypeToken, ArgsNode *argsNode);
    ~NewNode();

    TypeToken *getTargetTypeToken();

    /**
     * remove type token and return removed type token.
     */
    TypeToken *removeTargetTypeToken();

    ArgsNode *getArgsNode();
    int accept(NodeVisitor *visitor);   // override
};

class CondOpNode: public Node {
private:
    Node* leftNode;
    Node* rightNode;

    /**
     * if true, conditional and. otherwise, conditional or
     */
    bool andOp;

public:
    CondOpNode(int lineNum, Node *leftNode, Node *rightNode, bool isAndOp);
    ~CondOpNode();

    Node *getLeftNode();
    Node *getRightNode();
    bool isAndOp();
    int accept(NodeVisitor *visitor);	//override
};

class ProcessNode: public Node {	//FIXME: redirect option, trace
private:
    std::string commandName;
    std::vector<ProcArgNode*> argNodes;
    std::vector<std::pair<int, Node*>> redirOptions;

public:
    ProcessNode(int lineNum, std::string &&commandName);
    ~ProcessNode();

    const std::string &getCommandName();
    void addArgNode(ProcArgNode *node);
    const std::vector<ProcArgNode*> &getArgNodes();
    void addRedirOption(std::pair<int, Node*> &&optionPair);
    const std::vector<std::pair<int, Node*>> &getRedirOptions();
    int accept(NodeVisitor *visitor);	//override
};

/**
 * for command(process) argument
 */
class ProcArgNode: public Node {	//TODO: escape sequence
private:
    std::vector<Node*> segmentNodes;

public:
    ProcArgNode(int lineNum);
    ~ProcArgNode();

    void addSegmentNode(Node *node);
    const std::vector<Node*> &getSegmentNodes();
    int accept(NodeVisitor *visitor);	// override
};

class SpecialCharNode: public Node {	//FIXME:
public:
    SpecialCharNode(int lineNum);
    ~SpecialCharNode();

    int accept(NodeVisitor *visitor);	//override
};

class TaskNode: public Node {	//TODO: background ...etc
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

class InnerTaskNode: public Node {	//FIXME:
private:
    Node* exprNode;

public:
    InnerTaskNode(Node *exprNode);
    ~InnerTaskNode();

    Node *getExprNode();
    int accept(NodeVisitor *visitor);	//override
};

// statement definition

class AssertNode: public Node {
private:
    Node *exprNode;

public:
    AssertNode(int lineNum, Node *exprNode);
    ~AssertNode();

    Node *getExprNode();
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
    Node* exprNode;

public:
    ExportEnvNode(int lineNum, std::string &&envName, Node *exprNode);
    ~ExportEnvNode();

    const std::string &getEnvName();
    Node *getExprNode();
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
    Node *condNode;
    BlockNode *blockNode;

    /**
     * if true, this node represent for do-while
     */
    bool asDoWhile;

public:
    WhileNode(int lineNum, Node *condNode, BlockNode *blockNode, bool asDoWhile);
    ~WhileNode();

    Node *getCondNode();
    BlockNode *getBlockNode();
    bool isDoWhile();
    int accept(NodeVisitor *visitor);	//override
};

class IfNode: public Node {
private:
    Node *condNode;
    BlockNode *thenNode;

    /**
     * may be null, if has no else block
     */
    BlockNode *elseNode;

public:
    /**
     * elseNode may be null
     */
    IfNode(int lineNum, Node *condNode, BlockNode *thenNode, BlockNode *elseNode);
    ~IfNode();

    Node *getCondNode();
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
    Node* exprNode;

public:
    ReturnNode(int lineNum, Node *exprNode);
    ReturnNode(int lineNum);
    ~ReturnNode();

    /**
     * return null if has no return value
     */
    Node *getExprNode();
    int accept(NodeVisitor *visitor);	// override
};

class ThrowNode: public BlockEndNode {
private:
    Node* exprNode;

public:
    ThrowNode(int lineNum, Node *exprNode);
    ~ThrowNode();

    Node *getExprNode();
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
    Node* initValueNode;

public:
    VarDeclNode(int lineNum, std::string &&varName, Node *initValueNode, bool readOnly);
    ~VarDeclNode();

    const std::string &getVarName();
    bool isReadOnly();
    void setGlobal(bool global);
    bool isGlobal();
    Node *getInitValueNode();
    int accept(NodeVisitor *visitor);	// override
};

/**
 * for assignment or named parameter
 * assignment is statement.
 * so, after type checking, type is always VoidType
 */
class AssignNode: public Node {
private:
    /**
     * must be VarNode or AccessNode
     */
    Node* leftNode;

    Node* rightNode;

public:
    AssignNode(Node *leftNode, Node *rightNode);
    ~AssignNode();

    Node *getLeftNode();
    Node *getRightNode();
    int accept(NodeVisitor *visitor);   // override
};

/**
 * for field self assignment.
 */
class FieldSelfAssignNode : public Node {
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

class EmptyNode: public Node {
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
