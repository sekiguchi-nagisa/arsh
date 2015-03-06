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
#include <core/DSObject.h>
#include <ast/NodeVisitor.h>
#include <ast/TypeToken.h>
#include <parser/Token.h>

class Writer;

class Node {
protected:
    unsigned int lineNum;

    /**
     * initial value is null.
     */
    DSType *type;

public:
    Node(unsigned int lineNum);
    virtual ~Node();

    unsigned int getLineNum() const;
    void setType(DSType *type);

    /**
     * return null, before type checking
     */
    DSType *getType() const;
    virtual void dump(Writer &writer) const = 0;
    virtual int accept(NodeVisitor *visitor) = 0;
};

// expression definition

class IntValueNode: public Node {
private:
    std::shared_ptr<DSObject> value;

public:
    IntValueNode(unsigned int lineNum, int value);

    std::shared_ptr<DSObject> getValue();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

class FloatValueNode: public Node {
private:
    std::shared_ptr<DSObject> value;

public:
    FloatValueNode(unsigned int lineNum, double value);

    std::shared_ptr<DSObject> getValue();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

class StringValueNode: public Node {	//FIXME:
private:
    std::shared_ptr<DSObject> value;

public:
    /**
     * used for CommandNode. lineNum is always 0.
     */
    StringValueNode(std::string &&value);

    StringValueNode(unsigned int lineNum, std::string &&value);

    /**
     * call StringValueNode(lineNum, value, false)
     */
    //StringValueNode(unsigned int lineNum, char *value);	//FIXME:
    std::shared_ptr<DSObject> getValue();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

class StringExprNode: public Node {
private:
    std::vector<Node*> nodes;

public:
    StringExprNode(unsigned int lineNum);
    ~StringExprNode();

    void addExprNode(Node *node);	//TODO:
    const std::vector<Node*> &getExprNodes();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

class ArrayNode: public Node {
private:
    std::vector<Node*> nodes;

public:
    ArrayNode(unsigned int lineNum);
    ~ArrayNode();

    void addExprNode(Node *node);
    void setExprNode(unsigned int index, Node *node);
    const std::vector<Node*> &getExprNodes();
    void dump(Writer &writer) const; // override
    int accept(NodeVisitor *visitor);	//override
};

class MapNode: public Node {
private:
    std::vector<Node*> keyNodes;
    std::vector<Node*> valueNodes;

public:
    MapNode(unsigned int lineNum);
    ~MapNode();

    void addEntry(Node *keyNode, Node *valueNode);
    void setKeyNode(unsigned int index, Node *keyNode);
    const std::vector<Node*> &getKeyNodes();
    void setValueNode(unsigned int index, Node *valueNode);
    const std::vector<Node*> &getValueNodes();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

class TupleNode: public Node {
private:
    std::vector<Node*> nodes;

public:
    TupleNode(unsigned int lineNum, Node *node);
    ~TupleNode();

    void addNode(Node *node);
    const std::vector<Node*> &getNodes();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

/**
 * base class for VarNode, AccessNode
 */
class AssignableNode: public Node {
protected:
    bool readOnly;

    /**
     * if node is VarNode, treat as var index.
     * if node is AccessNode, treat as field index.
     */
    int index;

public:
    AssignableNode(unsigned int lineNum);
    virtual ~AssignableNode();

    bool isReadOnly();
    int getIndex();
};

class VarNode: public AssignableNode {
private:
    std::string varName;
    bool global;

public:
    VarNode(unsigned int lineNum, std::string &&varName);

    const std::string &getVarName();
    void setAttribute(FieldHandle *handle);
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
    bool isGlobal();
    int getVarIndex();
};

class AccessNode: public AssignableNode {
public:
    typedef enum {
        NOP,
        DUP_RECV,
        DUP_RECV_AND_SWAP,
    } AdditionalOp;

private:
    Node* recvNode;
    std::string fieldName;
    AdditionalOp additionalOp;

public:
    AccessNode(Node *recvNode, std::string &&fieldName);
    ~AccessNode();

    Node *getRecvNode();
    void setFieldName(const std::string &fieldName);
    const std::string &getFieldName();
    void setAttribute(FieldHandle *handle);
    int getFieldIndex();
    void setAdditionalOp(AdditionalOp op);
    AdditionalOp getAdditionnalOp();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

class CastNode: public Node {
public:
    typedef enum {
        NOP,
        INT_TO_FLOAT,
        FLOAT_TO_INT,
        TO_STRING,
        CHECK_CAST,
    } CastOp;

private:
    Node *exprNode;
    TypeToken *targetTypeToken;
    CastOp opKind;

    /**
     * for string cast
     */
    int fieldIndex;

public:
    CastNode(Node *exprNode, TypeToken *type);
    ~CastNode();

    Node *getExprNode();
    TypeToken *getTargetTypeToken();

    /**
     * remove type token, and return removed it.
     */
    TypeToken *removeTargetTypeToken();

    void setOpKind(CastOp opKind);
    CastOp getOpKind();
    void setFieldIndex(int index);
    int getFieldIndex();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	//override
};

class InstanceOfNode: public Node {
public:
    typedef enum {
        ALWAYS_FALSE,
        INSTANCEOF,
    } InstanceOfOp;

private:
    Node* targetNode;
    TypeToken* targetTypeToken;
    DSType *targetType;
    InstanceOfOp opKind;

public:
    InstanceOfNode(Node *targetNode, TypeToken *tyep);
    ~InstanceOfNode();

    Node *getTargetNode();
    TypeToken *getTargetTypeToken();

    /**
     * remove type token, and return removed it.
     */
    TypeToken *removeTargetTypeToken();

    void setTargetType(DSType *targetType);
    DSType *getTargetType();
    void setOpKind(InstanceOfOp opKind);
    InstanceOfOp getOpKind();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	//override
};

/**
 * binary operator call.
 */
class BinaryOpNode : public Node {
private:
    /**
     * after call this->createApplyNode(), will be null.
     */
    Node *leftNode;

    /**
     * after call this->createApplyNode(), will be null.
     */
    Node *rightNode;

    TokenKind op;

    /**
     * before call this->createApplyNode(), it is null.
     */
    ApplyNode *applyNode;

public:
    BinaryOpNode(Node *leftNode, TokenKind op, Node *rightNode);
    ~BinaryOpNode();

    Node *getLeftNode();
    void setLeftNode(Node *leftNode);
    Node *getRightNode();
    void setRightNode(Node *rightNode);

    /**
     * create ApplyNode and set to this->applyNode.
     * leftNode and rightNode will be null.
     */
    ApplyNode *creatApplyNode();

    /**
     * return null, before call this->createApplyNode().
     */
    ApplyNode *getApplyNode();

    void dump(Writer &writer) const;  // override
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
    ArgsNode(unsigned int lineNum);
    ArgsNode(std::string &&paramName, Node* argNode);
    ArgsNode(Node *argNode);
    ~ArgsNode();

    void addArgPair(std::string &&paramName, Node *argNode);

    /**
     * equivalent to this->addArgPair("", argNode)
     */
    void addArg(Node *argNode);

    void setArg(unsigned int index, Node *argNode);
    void initIndexMap();
    void addParamIndex(unsigned int index, unsigned int value);
    unsigned int *getParamIndexMap();
    void setParamSize(unsigned int size);
    unsigned int getParamSize();
    const std::vector<std::pair<std::string, Node*>> &getArgPairs();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);   // override
};

class ApplyNode: public Node {
private:
    Node *recvNode;
    ArgsNode *argsNode;

    unsigned char attributeSet;

public:
    ApplyNode(Node *recvNode, ArgsNode *argsNode);
    ~ApplyNode();

    Node *getRecvNode();
    ArgsNode *getArgsNode();
    void setAttribute(unsigned char attribute);
    void unsetAttribute(unsigned char attribute);
    bool hasAttribute(unsigned char attribute);
    void setFuncCall(bool asFuncCall);
    bool isFuncCall();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override

    const static unsigned char FUNC_CALL = 1 << 0;
    const static unsigned char INDEX     = 1 << 1;
};

/**
 * allocate new DSObject and call constructor.
 */
class NewNode : public Node {
private:
    TypeToken *targetTypeToken;
    ArgsNode *argsNode;

public:
    NewNode(unsigned int lineNum, TypeToken *targetTypeToken, ArgsNode *argsNode);
    ~NewNode();

    TypeToken *getTargetTypeToken();

    /**
     * remove type token and return removed type token.
     */
    TypeToken *removeTargetTypeToken();

    ArgsNode *getArgsNode();
    void dump(Writer &writer) const;  // override
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
    CondOpNode(Node *leftNode, Node *rightNode, bool isAndOp);
    ~CondOpNode();

    Node *getLeftNode();
    Node *getRightNode();
    bool isAndOp();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	//override
};

class ProcessNode: public Node {	//FIXME: redirect option
private:
    std::string commandName;
    std::vector<ProcArgNode*> argNodes;
    std::vector<std::pair<int, Node*>> redirOptions;

public:
    ProcessNode(unsigned int lineNum, std::string &&commandName);
    ~ProcessNode();

    const std::string &getCommandName();
    void addArgNode(ProcArgNode *node);
    const std::vector<ProcArgNode*> &getArgNodes();
    void addRedirOption(std::pair<int, Node*> &&optionPair);
    const std::vector<std::pair<int, Node*>> &getRedirOptions();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	//override
};

/**
 * for command(process) argument
 */
class ProcArgNode: public Node {	//TODO: escape sequence
private:
    std::vector<Node*> segmentNodes;

public:
    ProcArgNode(unsigned int lineNum);
    ~ProcArgNode();

    void addSegmentNode(Node *node);
    const std::vector<Node*> &getSegmentNodes();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

class SpecialCharNode: public Node {	//FIXME:
public:
    SpecialCharNode(unsigned int lineNum);
    ~SpecialCharNode();

    void dump(Writer &writer) const;  // override
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
    void dump(Writer &writer) const; // override
    int accept(NodeVisitor *visitor);	//override
};

class InnerTaskNode: public Node {	//FIXME:
private:
    Node* exprNode;

public:
    InnerTaskNode(Node *exprNode);
    ~InnerTaskNode();

    Node *getExprNode();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	//override
};

// statement definition

class AssertNode: public Node {
private:
    Node *exprNode;

public:
    AssertNode(unsigned int lineNum, Node *exprNode);
    ~AssertNode();

    Node *getExprNode();
    void dump(Writer &writer) const;  // override
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
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

/**
 * base class for break, continue, return, throw node
 */
class BlockEndNode: public Node {
public:
    BlockEndNode(unsigned int lineNum);
};

class BreakNode: public BlockEndNode {
public:
    BreakNode(unsigned int lineNum);

    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

class ContinueNode: public BlockEndNode {
public:
    ContinueNode(unsigned int lineNum);

    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

class ExportEnvNode: public Node {
private:
    std::string envName;
    Node* exprNode;

public:
    ExportEnvNode(unsigned int lineNum, std::string &&envName, Node *exprNode);
    ~ExportEnvNode();

    const std::string &getEnvName();
    Node *getExprNode();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

class ImportEnvNode: public Node {
private:
    std::string envName;
    bool global;
    int varIndex;

public:
    ImportEnvNode(unsigned int lineNum, std::string &&envName);

    const std::string &getEnvName();
    void setAttribute(FieldHandle *handle);
    bool isGlobal();
    int getVarIndex();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

class ForNode: public Node {
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
    ForNode(unsigned int lineNum, Node *initNode, Node *condNode, Node *iterNode, BlockNode *blockNode);
    ~ForNode();

    Node *getInitNode();
    Node *getCondNode();
    Node *getIterNode();
    BlockNode *getBlockNode();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

class WhileNode: public Node {
private:
    Node *condNode;
    BlockNode *blockNode;

public:
    WhileNode(unsigned int lineNum, Node *condNode, BlockNode *blockNode);
    ~WhileNode();

    Node *getCondNode();
    BlockNode *getBlockNode();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	//override
};

class DoWhileNode : public Node {
private:
    BlockNode *blockNode;
    Node *condNode;

public:
    DoWhileNode(unsigned int lineNum, BlockNode *blockNode, Node *condNode);
    ~DoWhileNode();

    BlockNode *getBlockNode();
    Node *getCondNode();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);   //override
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
    IfNode(unsigned int lineNum, Node *condNode, BlockNode *thenNode, BlockNode *elseNode);
    ~IfNode();

    Node *getCondNode();
    BlockNode *getThenNode();

    /*
     * return EmptyBlockNode, if elseNode is null.
     */
    BlockNode *getElseNode();

    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

class ReturnNode: public BlockEndNode {
private:
    /**
     * may be null, if has no return value
     */
    Node* exprNode;

public:
    ReturnNode(unsigned int lineNum, Node *exprNode);
    ReturnNode(unsigned int lineNum);
    ~ReturnNode();

    /**
     * return null if has no return value
     */
    Node *getExprNode();

    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

class ThrowNode: public BlockEndNode {
private:
    Node* exprNode;

public:
    ThrowNode(unsigned int lineNum, Node *exprNode);
    ~ThrowNode();

    Node *getExprNode();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

class CatchNode: public Node {
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
    CatchNode(unsigned int lineNum, std::string &&exceptionName, TypeToken *type, BlockNode *blockNode);
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
    void dump(Writer &writer) const;  // override
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
    TryNode(unsigned int lineNum, BlockNode *blockNode);
    ~TryNode();

    BlockNode *getBlockNode();
    void addCatchNode(CatchNode *catchNode);
    const std::vector<CatchNode*> &getCatchNodes();
    void addFinallyNode(Node *finallyNode);
    Node *getFinallyNode();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

class FinallyNode: public Node {
private:
    BlockNode* blockNode;

public:
    FinallyNode(unsigned int lineNum, BlockNode *block);
    ~FinallyNode();

    BlockNode *getBlockNode();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

class VarDeclNode: public Node {
private:
    std::string varName;
    bool readOnly;
    bool global;
    int varIndex;
    Node* initValueNode;

public:
    VarDeclNode(unsigned int lineNum, std::string &&varName, Node *initValueNode, bool readOnly);
    ~VarDeclNode();

    const std::string &getVarName();
    bool isReadOnly();
    void setAttribute(FieldHandle *handle);
    bool isGlobal();
    Node *getInitValueNode();
    int getVarIndex();
    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

/**
 * for assignment, self assignment or named parameter
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

    /**
     * if true, treat as self assignment.
     */
    bool selfAssign;

public:
    AssignNode(Node *leftNode, Node *rightNode, bool selfAssign = false);
    ~AssignNode();

    Node *getLeftNode();
    void setRightNode(Node *rightNode);
    Node *getRightNode();
    bool isSelfAssignment();
    void dump(Writer &writer) const;  // override
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
    FunctionNode(unsigned int lineNum, std::string &&funcName);
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

    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

// class ClassNode
// class ConstructorNode

class EmptyNode: public Node {
public:
    EmptyNode();

    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);	// override
};

class DummyNode: public Node {
public:
    DummyNode();

    void dump(Writer &writer) const;  // override
    int accept(NodeVisitor *visitor);   // override
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
    const std::list<Node*> &getNodeList() const;
};

// helper function for node creation

std::string resolveUnaryOpName(TokenKind op);
std::string resolveBinaryOpName(TokenKind op);
TokenKind resolveAssignOp(TokenKind op);

ApplyNode *createApplyNode(Node *recvNode, std::string &&methodName);

ForNode *createForInNode(unsigned int lineNum, std::string &&initName, Node *exprNode, BlockNode *blockNode);

Node *createSuffixNode(Node *leftNode, TokenKind op);

Node *createAssignNode(Node *leftNode, TokenKind op, Node *rightNode);

Node *createIndexNode(Node *recvNode, Node *indexNode);

Node *createUnaryOpNode(TokenKind op, Node *recvNode);

#endif /* AST_NODE_H_ */
