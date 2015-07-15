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
#include <memory>

#include "../misc/flag_util.hpp"
#include "../parser/TokenKind.h"
#include "TypeToken.h"

namespace ydsh {
namespace core {

class DSType;
class FieldHandle;
class MethodHandle;
class DSObject;
class RuntimeContext;
enum class EvalStatus : unsigned int;
enum RedirectOP : unsigned int;

}
}

namespace ydsh {
namespace ast {

using namespace ydsh::core;
using namespace ydsh::parser;
using namespace ydsh::misc;

struct NodeVisitor;
class Writer;

class Node {
protected:
    unsigned int lineNum;

    /**
     * initial value is null.
     */
    DSType *type;

public:
    explicit Node(unsigned int lineNum);

    virtual ~Node() = default;

    unsigned int getLineNum() const;

    virtual void setType(DSType *type);

    /**
     * return null, before type checking
     */
    DSType *getType() const;

    /**
     * for CmdContextNode, normally do nothing
     */
    virtual void inStringExprNode();

    /**
     * for CmdContextNode, normally do nothing
     */
    virtual void inCmdArgNode();

    /**
     * for CmdContextNode, normally do nothing
     */
    virtual void inCondition();

    /**
     * for CmdContextNode, normally do nothing
     */
    virtual void inRightHandleSide();

    virtual bool isBlockEndNode();

    virtual void setSourceName(const char *sourceName);
    virtual const char *getSourceName();

    virtual void dump(Writer &writer) const = 0;
    virtual void accept(NodeVisitor *visitor) = 0;
    virtual EvalStatus eval(RuntimeContext &ctx) = 0;
};

// expression definition

class IntValueNode : public Node {
public:
    typedef enum {
        BYTE,
        INT16,
        UINT16,
        INT32,
        UINT32,
    } IntKind;

private:
    IntKind kind;
    int tempValue;

    /**
     * initialized after type check.
     */
    std::shared_ptr<DSObject> value;

private:
    IntValueNode(unsigned int lineNum, IntKind kind, int value);

public:
    IntValueNode(unsigned int lineNum, int value);
    static IntValueNode *newByte(unsigned int lineNum, unsigned char value);
    static IntValueNode *newInt16(unsigned int lineNum, short value);
    static IntValueNode *newUint16(unsigned int lineNum, unsigned short value);
    static IntValueNode *newInt32(unsigned int lineNum, int value);
    static IntValueNode *newUint32(unsigned int lineNum, unsigned int value);

    IntKind getKind();

    /**
     * before type check, return empty pointer.
     */
    std::shared_ptr<DSObject> getValue();

    void setType(DSType *type); // override
    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class LongValueNode : public Node {
private:
    long tempValue;
    bool unsignedValue;

    std::shared_ptr<DSObject> value;

public:
    LongValueNode(unsigned int lineNum, long value, bool unsignedValue);
    static LongValueNode *newInt64(unsigned int lineNum, long value);
    static LongValueNode *newUint64(unsigned int lineNum, unsigned long value);

    /**
     * before type check, return empty pointer.
     */
    const std::shared_ptr<DSObject> &getValue();

    /**
     * if true, treat as unsigned int 64.
     */
    bool isUnsignedValue();

    void setType(DSType *type); // override
    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class FloatValueNode : public Node {
private:
    double tempValue;

    /**
     * initialized after type check.
     */
    std::shared_ptr<DSObject> value;

public:
    FloatValueNode(unsigned int lineNum, double value);

    /**
     * before type check, return empty pointer.
     */
    std::shared_ptr<DSObject> getValue();

    void setType(DSType *type); // override
    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class StringValueNode : public Node {
protected:
    /**
     * after type checking, is broken.
     */
    std::string tempValue;

    /**
     * initialized after type check.
     */
    std::shared_ptr<DSObject> value;

public:
    /**
     * used for CommandNode. lineNum is always 0.
     */
    explicit StringValueNode(std::string &&value);

    StringValueNode(unsigned int lineNum, std::string &&value);

    virtual ~StringValueNode();

    /**
     * before type check, return empty pointer.
     */
    std::shared_ptr<DSObject> getValue();

    void setType(DSType *type); // override
    void dump(Writer &writer) const;  // override
    virtual void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class ObjectPathNode : public StringValueNode {
public:
    ObjectPathNode(unsigned int lineNum, std::string &&value);
    ~ObjectPathNode() = default;

    void accept(NodeVisitor *visitor); // override
};

class StringExprNode : public Node {
private:
    std::vector<Node *> nodes;

public:
    explicit StringExprNode(unsigned int lineNum);

    ~StringExprNode();

    void addExprNode(Node *node);

    const std::vector<Node *> &getExprNodes();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class ArrayNode : public Node {
private:
    std::vector<Node *> nodes;

public:
    ArrayNode(unsigned int lineNum, Node *node);

    ~ArrayNode();

    void addExprNode(Node *node);

    void setExprNode(unsigned int index, Node *node);

    const std::vector<Node *> &getExprNodes();

    void dump(Writer &writer) const; // override
    void accept(NodeVisitor *visitor);    //override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class MapNode : public Node {
private:
    std::vector<Node *> keyNodes;
    std::vector<Node *> valueNodes;

public:
    MapNode(unsigned int lineNum, Node *keyNode, Node *valueNode);

    ~MapNode();

    void addEntry(Node *keyNode, Node *valueNode);

    void setKeyNode(unsigned int index, Node *keyNode);

    const std::vector<Node *> &getKeyNodes();

    void setValueNode(unsigned int index, Node *valueNode);

    const std::vector<Node *> &getValueNodes();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class TupleNode : public Node {
private:
    /**
     * at least two nodes
     */
    std::vector<Node *> nodes;

public:
    TupleNode(unsigned int lineNum, Node *leftNode, Node *rightNode);

    ~TupleNode();

    void addNode(Node *node);

    const std::vector<Node *> &getNodes();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

/**
 * base class for VarNode, AccessNode
 */
class AssignableNode : public Node {
protected:
    unsigned int index;
    bool readOnly;
    bool global;
    bool env;
    bool interface;

public:
    explicit AssignableNode(unsigned int lineNum);
    virtual ~AssignableNode() = default;

    void setAttribute(FieldHandle *handle);
    bool isReadOnly() const;
    bool isGlobal() const;
    bool isEnv() const;
    bool withinInterface() const;
    unsigned int getIndex();

    virtual void dump(Writer &writer) const;  // override
};

class VarNode : public AssignableNode {
private:
    std::string varName;

public:
    VarNode(unsigned int lineNum, std::string &&varName);
    ~VarNode() = default;

    const std::string &getVarName();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override

    // for ArgsNode
    /**
     * extract varName from varNode.
     * after extracting, delete varNode.
     */
    static std::string extractVarNameAndDelete(VarNode *node);
};

class AccessNode : public AssignableNode {
public:
    typedef enum {
        NOP,
        DUP_RECV,
    } AdditionalOp;

private:
    Node *recvNode;
    std::string fieldName;
    AdditionalOp additionalOp;

public:
    AccessNode(Node *recvNode, std::string &&fieldName);
    ~AccessNode();

    Node *getRecvNode();
    void setFieldName(const std::string &fieldName);
    const std::string &getFieldName();
    void setAdditionalOp(AdditionalOp op);
    AdditionalOp getAdditionalOp();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override

    /**
     * extract recvNode, and fieldName.
     * after extraction, call destructor.
     */
    static std::pair<Node *, std::string> split(AccessNode *accessNode);
};

class CastNode : public Node {
public:
    typedef enum {
        NOP,
        INT_TO_FLOAT,
        FLOAT_TO_INT,
        INT_TO_LONG,
        LONG_TO_INT,
        LONG_TO_FLOAT,
        FLOAT_TO_LONG,
        COPY_INT,
        COPY_LONG,
        TO_STRING,
        CHECK_CAST,
    } CastOp;

private:
    Node *exprNode;

    /**
     * may be tagged pointer
     */
    TypeToken *targetTypeToken;

    CastOp opKind;

public:
    CastNode(Node *exprNode, TypeToken *type, bool dupTypeToken = false);

    ~CastNode();

    Node *getExprNode();

    TypeToken *getTargetTypeToken() const;

    void setOpKind(CastOp opKind);

    CastOp getOpKind();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    //override
    EvalStatus eval(RuntimeContext &ctx); // override

    /**
     * for implicit cast.
     * targetNode must be typed node.
     * type is after casted value type.
     */
    static CastNode *newTypedCastNode(Node *targetNode, DSType *type, CastOp op);
};

class InstanceOfNode : public Node {
public:
    typedef enum {
        ALWAYS_FALSE,
        ALWAYS_TRUE,
        INSTANCEOF,
    } InstanceOfOp;

private:
    Node *targetNode;
    TypeToken *targetTypeToken;
    DSType *targetType;
    InstanceOfOp opKind;

public:
    InstanceOfNode(Node *targetNode, TypeToken *typeToken);

    ~InstanceOfNode();

    Node *getTargetNode();

    TypeToken *getTargetTypeToken();

    void setTargetType(DSType *targetType);

    DSType *getTargetType();

    void setOpKind(InstanceOfOp opKind);

    InstanceOfOp getOpKind();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    //override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class ArgsNode : public Node {
private:
    std::vector<Node *> nodes;

public:
    ArgsNode();

    ~ArgsNode();

    void addArg(Node *argNode);

    void setArg(unsigned int index, Node *argNode);

    const std::vector<Node *> &getNodes();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);   // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

/**
 * super type of NewNode, ApplyNode, MethodCallNode
 */
class CallNode : public Node {
protected:
    ArgsNode *argsNode;

public:
    CallNode(unsigned int lineNum, ArgsNode *argsNode);
    virtual ~CallNode();

    ArgsNode *getArgsNode();
};

/**
 * for function object apply
 */
class ApplyNode : public CallNode {
private:
    Node *exprNode;

public:
    ApplyNode(Node *exprNode, ArgsNode *argsNode);
    ~ApplyNode();

    Node *getExprNode();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class MethodCallNode : public CallNode {
private:
    Node *recvNode;
    std::string methodName;
    MethodHandle *handle;

    flag8_set_t attributeSet;

public:
    MethodCallNode(Node *recvNode, std::string &&methodName);
    MethodCallNode(Node *recvNode, std::string &&methodName, ArgsNode *argsNode);
    ~MethodCallNode();

    void setRecvNode(Node *node);
    Node *getRecvNode();
    void setMethodName(std::string &&methodName);
    const std::string &getMethodName();

    void setAttribute(flag8_t attribute);
    bool hasAttribute(flag8_t attribute);
    void setHandle(MethodHandle *handle);
    MethodHandle *getHandle();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override

    const static flag8_t INDEX = 1 << 0;
    const static flag8_t ICALL = 1 << 1;
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

    ArgsNode *getArgsNode();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);   // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

/**
 * for unary operator call
 */
class UnaryOpNode : public Node {
private:
    TokenKind op;

    /**
     * after call this->createApplyNode(), will be null.
     */
    Node *exprNode;

    /**
     * before call this->createApplyNode(), it is null.
     */
    MethodCallNode *methodCallNode;

public:
    UnaryOpNode(TokenKind op, Node *exprNode);
    ~UnaryOpNode();

    Node *getExprNode();
    void setExprNode(Node *exprNode);

    /**
     * create ApplyNode and set to this->applyNode.
     * exprNode will be null.
     */
    MethodCallNode *createApplyNode();

    /**
     * return null, before call this->createApplyNode().
     */
    MethodCallNode *getApplyNode();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);   // override
    EvalStatus eval(RuntimeContext &ctx); // override
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
    MethodCallNode *methodCallNode;

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
    MethodCallNode *createApplyNode();

    /**
     * return null, before call this->createApplyNode().
     */
    MethodCallNode *getApplyNode();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);   // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

/**
 * represent for '(' expr ')'
 */
class GroupNode : public Node {
private:
    Node *exprNode;

public:
    GroupNode(unsigned int lineNum, Node *exprNode);
    ~GroupNode();

    Node *getExprNode();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);   // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class CondOpNode : public Node {
private:
    Node *leftNode;
    Node *rightNode;

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
    void accept(NodeVisitor *visitor);    //override
    EvalStatus eval(RuntimeContext &ctx); // override
};

/**
 * for command argument
 */
class CmdArgNode : public Node {
private:
    std::vector<Node *> segmentNodes;

public:
    explicit CmdArgNode(Node *segmentNode);

    ~CmdArgNode();

    void addSegmentNode(Node *node);

    const std::vector<Node *> &getSegmentNodes();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);  // override
    EvalStatus eval(RuntimeContext &ctx); // override

    EvalStatus evalImpl(RuntimeContext &ctx);

    /**
     * if true, ignore evaluated empty string.
     */
    bool isIgnorableEmptyString();
};

class RedirNode : public Node {
private:
    RedirectOP op;
    CmdArgNode *targetNode;

public:
    RedirNode(TokenKind kind, CmdArgNode *node);

    ~RedirNode();

    RedirectOP getRedirectOP() {
        return this->op;
    }

    CmdArgNode *getTargetNode() {
        return this->targetNode;
    }

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);  // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class CmdNode : public Node {
private:
    std::string commandName;

    /**
     * may be CmdArgNode, RedirNode
     */
    std::vector<Node *> argNodes;

public:
    CmdNode(unsigned int lineNum, std::string &&commandName);

    ~CmdNode();

    const std::string &getCommandName();

    void addArgNode(CmdArgNode *node);

    const std::vector<Node *> &getArgNodes();

    void addRedirOption(TokenKind kind, CmdArgNode *node);
    void addRedirOption(TokenKind kind);

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    //override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class PipedCmdNode : public Node {    //TODO: background ...etc
private:
    std::vector<CmdNode *> cmdNodes;
    bool asBool;

public:
    explicit PipedCmdNode(CmdNode *node);
    ~PipedCmdNode();

    void addCmdNodes(CmdNode *node);
    const std::vector<CmdNode *> &getCmdNodes();
    void inCondition(); // override
    bool treatAsBool();

    void dump(Writer &writer) const; // override
    void accept(NodeVisitor *visitor);    //override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class CmdContextNode : public Node {
private:
    /**
     * may PipedCmdNode, CondOpNode, CmdNode
     */
    Node *exprNode;

    flag8_set_t attributeSet;

public:
    explicit CmdContextNode(Node *exprNode);
    ~CmdContextNode();

    Node *getExprNode();
    void setAttribute(flag8_t attribute);
    void unsetAttribute(flag8_t attribute);
    bool hasAttribute(flag8_t attribute);

    void inStringExprNode();    // override
    void inCmdArgNode();    // override
    void inCondition(); // override
    void inRightHandleSide();   // override

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    //override
    EvalStatus eval(RuntimeContext &ctx); // override

    const static flag8_t BACKGROUND = 1 << 0;
    const static flag8_t FORK       = 1 << 1;
    const static flag8_t STR_CAP    = 1 << 2;
    const static flag8_t ARRAY_CAP  = 1 << 3;
    const static flag8_t CONDITION  = 1 << 4;
};

// statement definition

class AssertNode : public Node {
private:
    Node *condNode;

public:
    AssertNode(unsigned int lineNum, Node *condNode);

    ~AssertNode();

    Node *getCondNode();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class BlockNode : public Node {
private:
    std::list<Node *> nodeList;

public:
    explicit BlockNode(unsigned int lineNum);

    ~BlockNode();

    void addNode(Node *node);

    void insertNodeToFirst(Node *node);

    const std::list<Node *> &getNodeList();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

/**
 * base class for break, continue, return, throw node
 */
class BlockEndNode : public Node {
public:
    explicit BlockEndNode(unsigned int lineNum);
    virtual ~BlockEndNode() = default;

    bool isBlockEndNode();  // override
};

class BreakNode : public BlockEndNode {
public:
    explicit BreakNode(unsigned int lineNum);
    ~BreakNode() = default;

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class ContinueNode : public BlockEndNode {
public:
    explicit ContinueNode(unsigned int lineNum);
    ~ContinueNode() = default;

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class ExportEnvNode : public Node {
private:
    std::string envName;
    Node *exprNode;
    bool global;
    unsigned int varIndex;

public:
    ExportEnvNode(unsigned int lineNum, std::string &&envName, Node *exprNode);

    ~ExportEnvNode();

    const std::string &getEnvName();

    Node *getExprNode();

    void setAttribute(FieldHandle *handle);

    bool isGlobal();

    unsigned int getVarIndex();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class ImportEnvNode : public Node {
private:
    std::string envName;
    bool global;
    unsigned int varIndex;

public:
    ImportEnvNode(unsigned int lineNum, std::string &&envName);
    ~ImportEnvNode() = default;

    const std::string &getEnvName();

    void setAttribute(FieldHandle *handle);

    bool isGlobal();

    unsigned int getVarIndex();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class TypeAliasNode : public Node {
private:
    std::string alias;
    TypeToken *targetTypeToken;

public:
    TypeAliasNode(unsigned int lineNum, std::string &&alias, TypeToken *targetTypeToken);
    TypeAliasNode(const char *alias, const char *targetTypeName);
    ~TypeAliasNode();

    const std::string &getAlias();
    TypeToken *getTargetTypeToken();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class ForNode : public Node {
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
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class WhileNode : public Node {
private:
    Node *condNode;
    BlockNode *blockNode;

public:
    WhileNode(unsigned int lineNum, Node *condNode, BlockNode *blockNode);

    ~WhileNode();

    Node *getCondNode();

    BlockNode *getBlockNode();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    //override
    EvalStatus eval(RuntimeContext &ctx); // override
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
    void accept(NodeVisitor *visitor);   //override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class IfNode : public Node {
private:
    Node *condNode;
    BlockNode *thenNode;

    std::vector<Node *> elifCondNodes;
    std::vector<BlockNode *> elifThenNodes;

    /**
     * may be null, if has no else block
     */
    BlockNode *elseNode;

public:
    /**
     * elseNode may be null
     */
    IfNode(unsigned int lineNum, Node *condNode, BlockNode *thenNode);

    ~IfNode();

    Node *getCondNode();

    BlockNode *getThenNode();

    void addElifNode(Node *condNode, BlockNode *thenNode);

    const std::vector<Node *> &getElifCondNodes();

    const std::vector<BlockNode *> &getElifThenNodes();

    void addElseNode(BlockNode *elseNode);

    /*
     * return EmptyBlockNode, if elseNode is null.
     */
    BlockNode *getElseNode();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class ReturnNode : public BlockEndNode {
private:
    /**
     * may be null, if has no return value
     */
    Node *exprNode;

public:
    ReturnNode(unsigned int lineNum, Node *exprNode);

    explicit ReturnNode(unsigned int lineNum);

    ~ReturnNode();

    /**
     * return null if has no return value
     */
    Node *getExprNode();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class ThrowNode : public BlockEndNode {
private:
    Node *exprNode;

public:
    ThrowNode(unsigned int lineNum, Node *exprNode);

    ~ThrowNode();

    Node *getExprNode();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class CatchNode : public Node {
private:
    std::string exceptionName;
    TypeToken *typeToken;

    /**
     * may be null, if has no type annotation.
     */
    DSType *exceptionType;

    unsigned int varIndex;

    BlockNode *blockNode;

public:
    CatchNode(unsigned int lineNum, std::string &&exceptionName,
              BlockNode *blockNode);

    CatchNode(unsigned int lineNum, std::string &&exceptionName,
              TypeToken *type, BlockNode *blockNode);

    ~CatchNode();

    const std::string &getExceptionName();

    TypeToken *getTypeToken();

    void setExceptionType(DSType *type);

    /**
     * return null if has no exception type
     */
    DSType *getExceptionType();

    void setAttribute(FieldHandle *handle);

    unsigned int getVarIndex();

    BlockNode *getBlockNode();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class TryNode : public Node {
private:
    BlockNode *blockNode;

    /**
     * may be empty
     */
    std::vector<CatchNode *> catchNodes;

    /**
     * may be EmptyBlockNode
     */
    BlockNode *finallyNode;

public:
    TryNode(unsigned int lineNum, BlockNode *blockNode);

    ~TryNode();

    BlockNode *getBlockNode();

    void addCatchNode(CatchNode *catchNode);

    const std::vector<CatchNode *> &getCatchNodes();

    void addFinallyNode(BlockNode *finallyNode);

    BlockNode *getFinallyNode();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class VarDeclNode : public Node {
private:
    std::string varName;
    bool readOnly;
    bool global;
    unsigned int varIndex;
    Node *initValueNode;

public:
    VarDeclNode(unsigned int lineNum, std::string &&varName, Node *initValueNode, bool readOnly);

    ~VarDeclNode();

    const std::string &getVarName();

    bool isReadOnly();

    void setAttribute(FieldHandle *handle);

    bool isGlobal();

    Node *getInitValueNode();

    unsigned int getVarIndex();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

/**
 * for assignment, self assignment or named parameter
 * assignment is statement.
 * so, after type checking, type is always VoidType
 */
class AssignNode : public Node {
private:
    /**
     * must be VarNode or AccessNode
     */
    Node *leftNode;

    Node *rightNode;
    flag8_set_t attributeSet;

public:
    const static flag8_t SELF_ASSIGN = 1 << 0;
    const static flag8_t FIELD_ASSIGN = 1 << 1;

    AssignNode(Node *leftNode, Node *rightNode, bool selfAssign = false);

    ~AssignNode();

    Node *getLeftNode();

    void setRightNode(Node *rightNode);

    Node *getRightNode();

    void setAttribute(flag8_t flag);

    bool isSelfAssignment();

    bool isFieldAssign();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);   // override
    EvalStatus eval(RuntimeContext &ctx); // override

    /**
     * for ArgsNode
     * split AssignNode to leftNode and rightNode.
     * after splitting, delete AssignNode.
     */
    static std::pair<Node *, Node *> split(AssignNode *node);
};

class ElementSelfAssignNode : public Node {
private:
    Node *recvNode;
    Node *indexNode;

    /**
     * receiver and argument are dummy node
     */
    MethodCallNode *getterNode;

    /**
     * receiver and argument are dummy node
     */
    MethodCallNode *setterNode;

    /**
     * left node is dummy node.
     */
    BinaryOpNode *binaryNode;

public:
    ElementSelfAssignNode(MethodCallNode *leftNode, BinaryOpNode *binaryNode);
    ~ElementSelfAssignNode();

    Node *getRecvNode();
    Node *getIndexNode();
    BinaryOpNode *getBinaryNode();
    MethodCallNode *getGetterNode();
    MethodCallNode *getSetterNode();

    /**
     * add recv type of getterNode and setterNode
     */
    void setRecvType(DSType *type);

    /**
     * add index type of getterNode and setterNode.
     */
    void setIndexType(DSType *type);

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);   // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class FunctionNode : public Node {    //FIXME: named parameter
private:
    std::string funcName;

    /**
     * for parameter definition.
     */
    std::vector<VarNode *> paramNodes;

    /**
     * type token of each parameter
     */
    std::vector<TypeToken *> paramTypeTokens;

    TypeToken *returnTypeToken;

    DSType *returnType;

    BlockNode *blockNode;

    const char *sourceName;

    /**
     * maximum number of local variable in function
     */
    unsigned int maxVarNum;

    /**
     * global variable table index of this function
     */
    unsigned int varIndex;

public:
    FunctionNode(unsigned int lineNum, std::string &&funcName);

    ~FunctionNode();

    const std::string &getFuncName();

    void addParamNode(VarNode *node, TypeToken *paramType);

    const std::vector<VarNode *> &getParamNodes();

    const std::vector<TypeToken *> &getParamTypeTokens();

    void setReturnTypeToken(TypeToken *typeToken);

    TypeToken *getReturnTypeToken();

    void setReturnType(DSType *returnType);

    DSType *getReturnType();

    void setBlockNode(BlockNode *blockNode);

    /**
     * return null before call setBlockNode()
     */
    BlockNode *getBlockNode();

    void setMaxVarNum(unsigned int maxVarNum);

    unsigned int getMaxVarNum();

    void setVarIndex(unsigned int varIndex);

    unsigned int getVarIndex();

    void setSourceName(const char *sourceName); // override
    const char *getSourceName(); // override

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class InterfaceNode : public Node {
private:
    std::string interfaceName;

    std::vector<FunctionNode *> methodDeclNodes;
    std::vector<VarDeclNode *> fieldDeclNodes;
    std::vector<TypeToken *> fieldTypeTokens;

public:
    InterfaceNode(unsigned int lineNum, std::string &&interfaceName);
    ~InterfaceNode();

    const std::string &getInterfaceName();
    void addMethodDeclNode(FunctionNode *methodDeclNode);
    const std::vector<FunctionNode *> &getMethodDeclNodes();

    /**
     * initNode of node is null.
     */
    void addFieldDecl(VarDeclNode *node, TypeToken *typeToken);

    const std::vector<VarDeclNode *> &getFieldDeclNodes();
    const std::vector<TypeToken *> &getFieldTypeTokens();

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

// class ClassNode
// class ConstructorNode

/**
 * define builtin global variable.
 */
class BindVarNode : public Node {
private:
    std::string varName;
    unsigned int varIndex;
    std::shared_ptr<DSObject> value;
    bool updatable;

public:
    BindVarNode(const char *name, const std::shared_ptr<DSObject> &value, bool updatable = false);
    ~BindVarNode() = default;

    const std::string &getVarName();
    void setAttribute(FieldHandle *handle);
    unsigned int getVarIndex();
    const std::shared_ptr<DSObject> &getValue();
    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class EmptyNode : public Node {
public:
    EmptyNode();
    explicit EmptyNode(unsigned int lineNum);
    ~EmptyNode() = default;

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class DummyNode : public Node {
public:
    DummyNode();
    ~DummyNode() = default;

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);   // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

/**
 * Root Node of AST.
 * this class is not inheritance of Node
 */
class RootNode : public Node {    //FIXME:
public:
    const char *sourceName;
    std::list<Node *> nodeList;

private:
    /**
     * max number of local variable.
     */
    unsigned int maxVarNum;

    /**
     * max number of global variable.
     */
    unsigned int maxGVarNum;

public:
    RootNode();
    explicit RootNode(const char *sourceName);

    ~RootNode();

    const char *getSourceName();    // override
    void addNode(Node *node);

    const std::list<Node *> &getNodeList();

    void setMaxVarNum(unsigned int maxVarNum);

    unsigned int getMaxVarNum() const;

    void setMaxGVarNum(unsigned int maxGVarNum);

    unsigned int getMaxGVarNum() const;

    void dump(Writer &writer) const;    // override
    void accept(NodeVisitor *visitor);  // override
    EvalStatus eval(RuntimeContext &ctx);   // override
};

// helper function for node creation

std::string resolveUnaryOpName(TokenKind op);

std::string resolveBinaryOpName(TokenKind op);

TokenKind resolveAssignOp(TokenKind op);

/**
 * create ApplyNode or MethodCallNode.
 */
CallNode *createCallNode(Node *recvNode, ArgsNode *argsNode);

ForNode *createForInNode(unsigned int lineNum, VarNode *varNode, Node *exprNode, BlockNode *blockNode);

Node *createSuffixNode(Node *leftNode, TokenKind op);

Node *createAssignNode(Node *leftNode, TokenKind op, Node *rightNode);

Node *createIndexNode(Node *recvNode, Node *indexNode);

Node *createBinaryOpNode(Node *leftNode, TokenKind op, Node *rightNode);

struct NodeVisitor {
    virtual ~NodeVisitor() = default;

    virtual void visitIntValueNode(IntValueNode *node) = 0;
    virtual void visitLongValueNode(LongValueNode *node) = 0;
    virtual void visitFloatValueNode(FloatValueNode *node) = 0;
    virtual void visitStringValueNode(StringValueNode *node) = 0;
    virtual void visitObjectPathNode(ObjectPathNode *node) = 0;
    virtual void visitStringExprNode(StringExprNode *node) = 0;
    virtual void visitArrayNode(ArrayNode *node) = 0;
    virtual void visitMapNode(MapNode *node) = 0;
    virtual void visitTupleNode(TupleNode *node) = 0;
    virtual void visitVarNode(VarNode *node) = 0;
    virtual void visitAccessNode(AccessNode *node) = 0;
    virtual void visitCastNode(CastNode *node) = 0;
    virtual void visitInstanceOfNode(InstanceOfNode *node) = 0;
    virtual void visitUnaryOpNode(UnaryOpNode *node) = 0;
    virtual void visitBinaryOpNode(BinaryOpNode *node) = 0;
    virtual void visitArgsNode(ArgsNode *node) = 0;
    virtual void visitApplyNode(ApplyNode *node) = 0;
    virtual void visitMethodCallNode(MethodCallNode *node) = 0;
    virtual void visitNewNode(NewNode *node) = 0;
    virtual void visitGroupNode(GroupNode *node) = 0;
    virtual void visitCondOpNode(CondOpNode *node) = 0;
    virtual void visitCmdNode(CmdNode *node) = 0;
    virtual void visitCmdArgNode(CmdArgNode *node) = 0;
    virtual void visitRedirNode(RedirNode *node) = 0;
    virtual void visitPipedCmdNode(PipedCmdNode *node) = 0;
    virtual void visitCmdContextNode(CmdContextNode *node) = 0;
    virtual void visitAssertNode(AssertNode *node) = 0;
    virtual void visitBlockNode(BlockNode *node) = 0;
    virtual void visitBreakNode(BreakNode *node) = 0;
    virtual void visitContinueNode(ContinueNode *node) = 0;
    virtual void visitExportEnvNode(ExportEnvNode *node) = 0;
    virtual void visitImportEnvNode(ImportEnvNode *node) = 0;
    virtual void visitTypeAliasNode(TypeAliasNode *node) = 0;
    virtual void visitForNode(ForNode *node) = 0;
    virtual void visitWhileNode(WhileNode *node) = 0;
    virtual void visitDoWhileNode(DoWhileNode *node) = 0;
    virtual void visitIfNode(IfNode *node) = 0;
    virtual void visitReturnNode(ReturnNode *node) = 0;
    virtual void visitThrowNode(ThrowNode *node) = 0;
    virtual void visitCatchNode(CatchNode *node) = 0;
    virtual void visitTryNode(TryNode *node) = 0;
    virtual void visitVarDeclNode(VarDeclNode *node) = 0;
    virtual void visitAssignNode(AssignNode *node) = 0;
    virtual void visitElementSelfAssignNode(ElementSelfAssignNode *node) = 0;
    virtual void visitFunctionNode(FunctionNode *node) = 0;
    virtual void visitInterfaceNode(InterfaceNode *node) = 0;
    virtual void visitBindVarNode(BindVarNode *node) = 0;
    virtual void visitEmptyNode(EmptyNode *node) = 0;
    virtual void visitDummyNode(DummyNode *node) = 0;
    virtual void visitRootNode(RootNode *node) = 0;
};

} // namespace ast
} // namespace ydsh

#endif /* AST_NODE_H_ */
