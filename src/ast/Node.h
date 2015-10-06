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
#include "../core/DSObject.h"
#include "TypeToken.h"

namespace ydsh {
namespace core {

class FieldHandle;
class MethodHandle;
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
    explicit Node(unsigned int lineNum) :
            lineNum(lineNum), type() { }

    virtual ~Node() = default;

    unsigned int getLineNum() const {
        return this->lineNum;
    }

    virtual void setType(DSType *type);

    /**
     * return null, before type checking
     */
    DSType *getType() const {
        return this->type;
    }

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

    virtual bool isBlockEndNode() const;

    virtual void setSourceName(const char *sourceName);
    virtual const char *getSourceName();

    virtual void dump(Writer &writer) const = 0;
    virtual void accept(NodeVisitor *visitor) = 0;
    virtual EvalStatus eval(RuntimeContext &ctx) = 0;
};

// expression definition

class IntValueNode : public Node {
public:
    enum IntKind{
        BYTE,
        INT16,
        UINT16,
        INT32,
        UINT32,
    };

private:
    IntKind kind;
    int tempValue;

    /**
     * initialized after type check.
     */
    DSValue value;

private:
    IntValueNode(unsigned int lineNum, IntKind kind, int value) :
            Node(lineNum), kind(kind), tempValue(value), value() { }

public:
    IntValueNode(unsigned int lineNum, int value) :
            IntValueNode(lineNum, INT32, value) { }

    static IntValueNode *newByte(unsigned int lineNum, unsigned char value) {
        return new IntValueNode(lineNum, BYTE, (int) value);
    }

    static IntValueNode *newInt16(unsigned int lineNum, short value) {
        return new IntValueNode(lineNum, INT16, (int) value);
    }

    static IntValueNode *newUint16(unsigned int lineNum, unsigned short value) {
        return new IntValueNode(lineNum, UINT16, (int) value);
    }

    static IntValueNode *newInt32(unsigned int lineNum, int value) {
        return new IntValueNode(lineNum, INT32, value);
    }

    static IntValueNode *newUint32(unsigned int lineNum, unsigned int value) {
        return new IntValueNode(lineNum, UINT32, (int) value);
    }

    IntKind getKind() const {
        return this->kind;
    }

    int getTempValue() const {
        return this->tempValue;
    }

    /**
     * before type check, return empty pointer.
     */
    const DSValue &getValue() const {
        return this->value;
    }

    void setType(DSType *type); // override
    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class LongValueNode : public Node {
private:
    long tempValue;
    bool unsignedValue;

    DSValue value;

public:
    LongValueNode(unsigned int lineNum, long value, bool unsignedValue) :
            Node(lineNum), tempValue(value), unsignedValue(unsignedValue), value() { }

    static LongValueNode *newInt64(unsigned int lineNum, long value) {
        return new LongValueNode(lineNum, value, false);
    }

    static LongValueNode *newUint64(unsigned int lineNum, unsigned long value) {
        return new LongValueNode(lineNum, (long) value, true);
    }

    /**
     * before type check, return empty pointer.
     */
    const DSValue &getValue() const {
        return this->value;
    }

    /**
     * if true, treat as unsigned int 64.
     */
    bool isUnsignedValue() const {
        return this->unsignedValue;
    }

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
    DSValue value;

public:
    FloatValueNode(unsigned int lineNum, double value) :
            Node(lineNum), tempValue(value), value() { }

    /**
     * before type check, return empty pointer.
     */
    const DSValue &getValue() const {
        return this->value;
    }

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
    DSValue value;

public:
    /**
     * used for CommandNode. lineNum is always 0.
     */
    explicit StringValueNode(std::string &&value) :
            StringValueNode(0, std::move(value)) { }

    StringValueNode(unsigned int lineNum, std::string &&value) :
            Node(lineNum), tempValue(std::move(value)), value() { }

    virtual ~StringValueNode() = default;

    /**
     * before type check, return empty pointer.
     */
    const DSValue &getValue() const {
        return this->value;
    }

    void setType(DSType *type); // override
    void dump(Writer &writer) const;  // override
    virtual void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class ObjectPathNode : public StringValueNode {
public:
    ObjectPathNode(unsigned int lineNum, std::string &&value) :
            StringValueNode(lineNum, std::move(value)) { }

    ~ObjectPathNode() = default;

    void accept(NodeVisitor *visitor); // override
};

class StringExprNode : public Node {
private:
    std::vector<Node *> nodes;

public:
    explicit StringExprNode(unsigned int lineNum) : Node(lineNum), nodes() { }

    ~StringExprNode();

    void addExprNode(Node *node);

    const std::vector<Node *> &getExprNodes() const {
        return this->nodes;
    }

    void setExprNode(unsigned int index, Node *exprNode) {
        this->nodes[index] = exprNode;
    }

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

    const std::vector<Node *> &getExprNodes() const {
        return this->nodes;
    }

    std::vector<Node *> &refExprNodes() {
        return this->nodes;
    }

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

    const std::vector<Node *> &getKeyNodes() const {
        return this->keyNodes;
    }

    std::vector<Node *> &refKeyNodes() {
        return this->keyNodes;
    }

    const std::vector<Node *> &getValueNodes() const {
        return this->valueNodes;
    }

    std::vector<Node *> &refValueNodes() {
        return this->valueNodes;
    }

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

    const std::vector<Node *> &getNodes() const {
        return this->nodes;
    }

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
    explicit AssignableNode(unsigned int lineNum) :
            Node(lineNum), index(0),
            readOnly(false), global(false), env(false), interface(false) { }

    virtual ~AssignableNode() = default;

    void setAttribute(FieldHandle *handle);

    bool isReadOnly() const {
        return this->readOnly;
    }

    bool isGlobal() const {
        return this->global;
    }

    bool isEnv() const {
        return this->env;
    }

    bool withinInterface() const {
        return this->interface;
    }

    unsigned int getIndex() const {
        return this->index;
    }

    virtual void dump(Writer &writer) const;  // override
};

class VarNode : public AssignableNode {
private:
    std::string varName;

public:
    VarNode(unsigned int lineNum, std::string &&varName) :
            AssignableNode(lineNum), varName(std::move(varName)) { }

    ~VarNode() = default;

    const std::string &getVarName() const {
        return this->varName;
    }

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override

    // for ArgsNode
    /**
     * extract varName from varNode.
     * after extracting, delete varNode.
     */
    static std::string extractVarNameAndDelete(VarNode *node) {
        std::string name(std::move(node->varName));
        delete node;
        return name;
    }
};

class AccessNode : public AssignableNode {
public:
    enum AdditionalOp {
        NOP,
        DUP_RECV,
    };

private:
    Node *recvNode;
    std::string fieldName;
    AdditionalOp additionalOp;

public:
    AccessNode(Node *recvNode, std::string &&fieldName) :
            AssignableNode(recvNode->getLineNum()), recvNode(recvNode),
            fieldName(std::move(fieldName)), additionalOp(NOP) { }

    ~AccessNode();

    Node *getRecvNode() const {
        return this->recvNode;
    }

    void setFieldName(const std::string &fieldName) {
        this->fieldName = fieldName;
    }

    const std::string &getFieldName() const {
        return this->fieldName;
    }

    void setAdditionalOp(AdditionalOp op) {
        this->additionalOp = op;
    }

    AdditionalOp getAdditionalOp() const {
        return this->additionalOp;
    }

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
    enum CastOp {
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
    };

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

    Node *getExprNode() const {
        return this->exprNode;
    }

    TypeToken *getTargetTypeToken() const;

    void setOpKind(CastOp opKind) {
        this->opKind = opKind;
    }

    CastOp getOpKind() const {
        return this->opKind;
    }

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
    enum InstanceOfOp {
        ALWAYS_FALSE,
        ALWAYS_TRUE,
        INSTANCEOF,
    };

private:
    Node *targetNode;
    TypeToken *targetTypeToken;
    DSType *targetType;
    InstanceOfOp opKind;

public:
    InstanceOfNode(Node *targetNode, TypeToken *typeToken) :
            Node(targetNode->getLineNum()), targetNode(targetNode), targetTypeToken(typeToken),
            targetType(0), opKind(ALWAYS_FALSE) { }

    ~InstanceOfNode();

    Node *getTargetNode() const {
        return this->targetNode;
    }

    TypeToken *getTargetTypeToken() const {
        return this->targetTypeToken;
    }

    void setTargetType(DSType *targetType) {
        this->targetType = targetType;
    }

    DSType *getTargetType() const {
        return this->targetType;
    }

    void setOpKind(InstanceOfOp opKind) {
        this->opKind = opKind;
    }

    InstanceOfOp getOpKind() const {
        return this->opKind;
    }

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    //override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class ArgsNode : public Node {
private:
    std::vector<Node *> nodes;

public:
    ArgsNode() : Node(0), nodes() { }

    ~ArgsNode();

    void addArg(Node *argNode);

    const std::vector<Node *> &getNodes() const {
        return this->nodes;
    }

    std::vector<Node *> &refNodes() {
        return this->nodes;
    }

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
    CallNode(unsigned int lineNum, ArgsNode *argsNode) :
            Node(lineNum), argsNode(argsNode) { }

    virtual ~CallNode();

    ArgsNode *getArgsNode() const {
        return this->argsNode;
    }
};

/**
 * for function object apply
 */
class ApplyNode : public CallNode {
private:
    Node *exprNode;

public:
    ApplyNode(Node *exprNode, ArgsNode *argsNode) :
            CallNode(exprNode->getLineNum(), argsNode), exprNode(exprNode) { }

    ~ApplyNode();

    Node *getExprNode() const {
        return this->exprNode;
    }

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
    MethodCallNode(Node *recvNode, std::string &&methodName) :
            MethodCallNode(recvNode, std::move(methodName), new ArgsNode()) { }

    MethodCallNode(Node *recvNode, std::string &&methodName, ArgsNode *argsNode) :
            CallNode(recvNode->getLineNum(), argsNode),
            recvNode(recvNode), methodName(std::move(methodName)), handle(), attributeSet() { }

    ~MethodCallNode();

    void setRecvNode(Node *node) {
        this->recvNode = node;
    }

    Node *getRecvNode() const {
        return this->recvNode;
    }

    void setMethodName(std::string &&methodName) {
        this->methodName = std::move(methodName);
    }

    const std::string &getMethodName() const {
        return this->methodName;
    }

    void setAttribute(flag8_t attribute) {
        setFlag(this->attributeSet, attribute);
    }

    bool hasAttribute(flag8_t attribute) const {
        return hasFlag(this->attributeSet, attribute);
    }

    void setHandle(MethodHandle *handle) {
        this->handle = handle;
    }

    MethodHandle *getHandle() const {
        return this->handle;
    }

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
    NewNode(unsigned int lineNum, TypeToken *targetTypeToken, ArgsNode *argsNode) :
            Node(lineNum), targetTypeToken(targetTypeToken), argsNode(argsNode) { }

    ~NewNode();

    TypeToken *getTargetTypeToken() const {
        return this->targetTypeToken;
    }

    ArgsNode *getArgsNode() const {
        return this->argsNode;
    }

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
    UnaryOpNode(TokenKind op, Node *exprNode) :
            Node(exprNode->getLineNum()), op(op), exprNode(exprNode), methodCallNode(0) { }

    ~UnaryOpNode();

    Node *getExprNode() const {
        return this->exprNode;
    }

    void setExprNode(Node *exprNode) {
        this->exprNode = exprNode;
    }

    /**
     * create ApplyNode and set to this->applyNode.
     * exprNode will be null.
     */
    MethodCallNode *createApplyNode();

    /**
     * return null, before call this->createApplyNode().
     */
    MethodCallNode *getApplyNode() const {
        return this->methodCallNode;
    }

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
    BinaryOpNode(Node *leftNode, TokenKind op, Node *rightNode) :
            Node(leftNode->getLineNum()),
            leftNode(leftNode), rightNode(rightNode), op(op), methodCallNode(0) { }

    ~BinaryOpNode();

    Node *getLeftNode() const {
        return this->leftNode;
    }

    void setLeftNode(Node *leftNode) {
        this->leftNode = leftNode;
    }

    Node *getRightNode() const {
        return this->rightNode;
    }

    void setRightNode(Node *rightNode) {
        this->rightNode = rightNode;
    }

    TokenKind getOp() const {
        return this->op;
    }

    /**
     * create ApplyNode and set to this->applyNode.
     * leftNode and rightNode will be null.
     */
    MethodCallNode *createApplyNode();

    /**
     * return null, before call this->createApplyNode().
     */
    MethodCallNode *getApplyNode() const {
        return this->methodCallNode;
    }

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
    GroupNode(unsigned int lineNum, Node *exprNode) :
            Node(lineNum), exprNode(exprNode) { }

    ~GroupNode();

    Node *getExprNode() const {
        return this->exprNode;
    }

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

    Node *getLeftNode() const {
        return this->leftNode;
    }

    Node *getRightNode() const {
        return this->rightNode;
    }

    bool isAndOp() const {
        return this->andOp;
    }

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
    explicit CmdArgNode(Node *segmentNode) :
            Node(segmentNode->getLineNum()), segmentNodes() {
        this->addSegmentNode(segmentNode);
    }

    ~CmdArgNode();

    void addSegmentNode(Node *node);

    const std::vector<Node *> &getSegmentNodes() const {
        return this->segmentNodes;
    }
    
    void setSegmentNode(unsigned int index, Node *segmentNode) {
        this->segmentNodes[index] = segmentNode;
    }

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

/**
 * for tilde expansion
 * TildeNode is always first element of CmdArgNode.
 */
class TildeNode : public Node {
private:
    /**
     * starts with '/'
     */
    std::string value;

public:
    TildeNode(unsigned int lineNum, std::string &&value) :
            Node(lineNum), value(std::move(value)) { }

    ~TildeNode() = default;

    const std::string &getValue() {
        return this->value;
    }

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);  // override

    /**
     * if isLastSegment is true, segment nodes size is 1.
     */
    std::string expand(bool isLastSegment = true);

    EvalStatus eval(RuntimeContext &ctx); // override
};

class CmdNode : public Node {
private:
    /**
     * msy be TildeNode or StringValueNode.
     */
    Node *nameNode;

    /**
     * may be CmdArgNode, RedirNode
     */
    std::vector<Node *> argNodes;

public:
    CmdNode(unsigned int lineNum, std::string &&value) :
            Node(lineNum),
            nameNode(new StringValueNode(lineNum, std::move(value))), argNodes() { }

    explicit CmdNode(TildeNode *nameNode) :
            Node(nameNode->getLineNum()), nameNode(nameNode), argNodes() { }

    ~CmdNode();

    Node *getNameNode() const {
        return this->nameNode;
    }

    void addArgNode(CmdArgNode *node);

    const std::vector<Node *> &getArgNodes() const {
        return this->argNodes;
    }

    void addRedirOption(TokenKind kind, CmdArgNode *node);
    void addRedirOption(TokenKind kind);

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    //override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class PipedCmdNode : public Node {    //TODO: background ...etc
private:
    std::vector<Node *> cmdNodes;
    bool asBool;

public:
    explicit PipedCmdNode(Node *node) :
            Node(node->getLineNum()), cmdNodes(), asBool(false) {
        this->cmdNodes.push_back(node);
    }

    ~PipedCmdNode();

    void addCmdNodes(Node *node);

    const std::vector<Node *> &getCmdNodes() const {
        return this->cmdNodes;
    }

    void inCondition(); // override

    bool treatAsBool() const {
        return this->asBool;
    }

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
    explicit CmdContextNode(Node *exprNode) :
            Node(exprNode->getLineNum()), exprNode(exprNode), attributeSet(0) {
        if(dynamic_cast<CondOpNode *>(exprNode) != 0) {
            this->setAttribute(CONDITION);
        }
    }

    ~CmdContextNode();

    Node *getExprNode() const {
        return this->exprNode;
    }

    void setAttribute(flag8_t attribute) {
        setFlag(this->attributeSet, attribute);
    }

    void unsetAttribute(flag8_t attribute) {
        unsetFlag(this->attributeSet, attribute);
    }

    bool hasAttribute(flag8_t attribute) const {
        return hasFlag(this->attributeSet, attribute);
    }

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
    AssertNode(unsigned int lineNum, Node *condNode) : Node(lineNum), condNode(condNode) {
        this->condNode->inCondition();
    }

    ~AssertNode();

    Node *getCondNode() const {
        return this->condNode;
    }

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class BlockNode : public Node {
private:
    std::list<Node *> nodeList;

public:
    explicit BlockNode(unsigned int lineNum) : Node(lineNum), nodeList() { }

    ~BlockNode();

    void addNode(Node *node);

    void insertNodeToFirst(Node *node);

    const std::list<Node *> &getNodeList() const {
        return this->nodeList;
    }

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

/**
 * base class for break, continue, return, throw node
 */
class BlockEndNode : public Node {
public:
    explicit BlockEndNode(unsigned int lineNum) : Node(lineNum) { }

    virtual ~BlockEndNode() = default;

    bool isBlockEndNode() const { // override
        return true;
    }
};

class BreakNode : public BlockEndNode {
public:
    explicit BreakNode(unsigned int lineNum) : BlockEndNode(lineNum) { }

    ~BreakNode() = default;

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class ContinueNode : public BlockEndNode {
public:
    explicit ContinueNode(unsigned int lineNum) : BlockEndNode(lineNum) { }

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
    ExportEnvNode(unsigned int lineNum, std::string &&envName, Node *exprNode) :
            Node(lineNum), envName(std::move(envName)), exprNode(exprNode),
            global(false), varIndex(0) { }

    ~ExportEnvNode();

    const std::string &getEnvName() const {
        return this->envName;
    }

    Node *getExprNode() const {
        return this->exprNode;
    }

    void setAttribute(FieldHandle *handle);

    bool isGlobal() const {
        return this->global;
    }

    unsigned int getVarIndex() const {
        return this->varIndex;
    }

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class ImportEnvNode : public Node {
private:
    std::string envName;

    /**
     * may be null if has no default value.
     */
    Node *defaultValueNode;

    bool global;
    unsigned int varIndex;

public:
    /**
     * defaultValueNode may be null
     */
    ImportEnvNode(unsigned int lineNum, std::string &&envName, Node *defaultValueNode) :
            Node(lineNum), envName(std::move(envName)),
            defaultValueNode(defaultValueNode), global(false), varIndex(0) { }

    ~ImportEnvNode();

    const std::string &getEnvName() const {
        return this->envName;
    }

    /**
     * may be null
     */
    Node *getDefaultValueNode() const {
        return this->defaultValueNode;
    }

    void setAttribute(FieldHandle *handle);

    bool isGlobal() const {
        return this->global;
    }

    unsigned int getVarIndex() const {
        return this->varIndex;
    }

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class TypeAliasNode : public Node {
private:
    std::string alias;
    TypeToken *targetTypeToken;

public:
    TypeAliasNode(unsigned int lineNum, std::string &&alias, TypeToken *targetTypeToken) :
            Node(lineNum), alias(std::move(alias)), targetTypeToken(targetTypeToken) { }

    TypeAliasNode(const char *alias, const char *targetTypeName) :
            Node(0), alias(std::string(alias)),
            targetTypeToken(new ClassTypeToken(0, std::string(targetTypeName))) { }

    ~TypeAliasNode();

    const std::string &getAlias() const {
        return this->alias;
    }

    TypeToken *getTargetTypeToken() const {
        return this->targetTypeToken;
    }

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

    Node *getInitNode() const {
        return this->initNode;
    }

    Node *getCondNode() const {
        return this->condNode;
    }

    Node *getIterNode() const {
        return this->iterNode;
    }

    BlockNode *getBlockNode() const {
        return this->blockNode;
    }

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class WhileNode : public Node {
private:
    Node *condNode;
    BlockNode *blockNode;

public:
    WhileNode(unsigned int lineNum, Node *condNode, BlockNode *blockNode) :
            Node(lineNum), condNode(condNode), blockNode(blockNode) {
        this->condNode->inCondition();
    }

    ~WhileNode();

    Node *getCondNode() const {
        return this->condNode;
    }

    BlockNode *getBlockNode() const {
        return this->blockNode;
    }

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    //override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class DoWhileNode : public Node {
private:
    BlockNode *blockNode;
    Node *condNode;

public:
    DoWhileNode(unsigned int lineNum, BlockNode *blockNode, Node *condNode) :
            Node(lineNum), blockNode(blockNode), condNode(condNode) {
        this->condNode->inCondition();
    }

    ~DoWhileNode();

    BlockNode *getBlockNode() const {
        return this->blockNode;
    }

    Node *getCondNode() const {
        return this->condNode;
    }

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

    Node *getCondNode() const {
        return this->condNode;
    }

    BlockNode *getThenNode() const {
        return this->thenNode;
    }

    void addElifNode(Node *condNode, BlockNode *thenNode);

    const std::vector<Node *> &getElifCondNodes() const {
        return this->elifCondNodes;
    }

    const std::vector<BlockNode *> &getElifThenNodes() const {
        return this->elifThenNodes;
    }

    void addElseNode(BlockNode *elseNode) {
        this->elseNode = elseNode;
    }

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
    Node *exprNode;

public:
    ReturnNode(unsigned int lineNum, Node *exprNode) :
            BlockEndNode(lineNum), exprNode(exprNode) { }

    explicit ReturnNode(unsigned int lineNum);

    ~ReturnNode();

    Node *getExprNode() const {
        return this->exprNode;
    }

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class ThrowNode : public BlockEndNode {
private:
    Node *exprNode;

public:
    ThrowNode(unsigned int lineNum, Node *exprNode) :
            BlockEndNode(lineNum), exprNode(exprNode) { }

    ~ThrowNode();

    Node *getExprNode() const {
        return this->exprNode;
    }

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
    CatchNode(unsigned int lineNum, std::string &&exceptionName, BlockNode *blockNode) :
            CatchNode(lineNum, std::move(exceptionName), newAnyTypeToken(lineNum), blockNode) { }

    CatchNode(unsigned int lineNum, std::string &&exceptionName,
              TypeToken *type, BlockNode *blockNode) :
            Node(lineNum), exceptionName(std::move(exceptionName)),
            typeToken(type), exceptionType(nullptr), varIndex(0), blockNode(blockNode) { }

    ~CatchNode();

    const std::string &getExceptionName() const {
        return this->exceptionName;
    }

    TypeToken *getTypeToken() const {
        return this->typeToken;
    }

    void setExceptionType(DSType *type) {
        this->exceptionType = type;
    }

    /**
     * return null if has no exception type
     */
    DSType *getExceptionType() const {
        return this->exceptionType;
    }

    void setAttribute(FieldHandle *handle);

    unsigned int getVarIndex() const {
        return this->varIndex;
    }

    BlockNode *getBlockNode() const {
        return this->blockNode;
    }

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
    TryNode(unsigned int lineNum, BlockNode *blockNode) :
            Node(lineNum), blockNode(blockNode), catchNodes(), finallyNode() { }

    ~TryNode();

    BlockNode *getBlockNode() const {
        return this->blockNode;
    }

    void addCatchNode(CatchNode *catchNode);

    const std::vector<CatchNode *> &getCatchNodes() const {
        return this->catchNodes;
    }

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

    const std::string &getVarName() const {
        return this->varName;
    }

    bool isReadOnly() const {
        return this->readOnly;
    }

    void setAttribute(FieldHandle *handle);

    bool isGlobal() const {
        return this->global;
    }

    Node *getInitValueNode() const {
        return this->initValueNode;
    }

    unsigned int getVarIndex() const {
        return this->varIndex;
    }

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
    const static flag8_t SELF_ASSIGN  = 1 << 0;
    const static flag8_t FIELD_ASSIGN = 1 << 1;

    AssignNode(Node *leftNode, Node *rightNode, bool selfAssign = false) :
            Node(leftNode->getLineNum()),
            leftNode(leftNode), rightNode(rightNode), attributeSet(0) {
        if(selfAssign) {
            setFlag(this->attributeSet, SELF_ASSIGN);
        }
    }

    ~AssignNode();

    Node *getLeftNode() const {
        return this->leftNode;
    }

    Node *getRightNode() const {
        return this->rightNode;
    }

    Node * &refRightNode() {
        return this->rightNode;
    }

    void setAttribute(flag8_t flag) {
        setFlag(this->attributeSet, flag);
    }

    bool isSelfAssignment() const {
        return hasFlag(this->attributeSet, SELF_ASSIGN);
    }

    bool isFieldAssign() const {
        return hasFlag(this->attributeSet, FIELD_ASSIGN);
    }

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

    Node *getRecvNode() const {
        return this->recvNode;
    }

    Node *getIndexNode() const {
        return this->indexNode;
    }

    BinaryOpNode *getBinaryNode() const {
        return this->binaryNode;
    }

    MethodCallNode *getGetterNode() const {
        return this->getterNode;
    }

    MethodCallNode *getSetterNode() const {
        return this->setterNode;
    }

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
    FunctionNode(unsigned int lineNum, std::string &&funcName) :
            Node(lineNum), funcName(std::move(funcName)),
            paramNodes(), paramTypeTokens(), returnTypeToken(), returnType(0),
            blockNode(), maxVarNum(0), varIndex(0) { }

    ~FunctionNode();

    const std::string &getFuncName() const {
        return this->funcName;
    }

    void addParamNode(VarNode *node, TypeToken *paramType);

    const std::vector<VarNode *> &getParamNodes() const {
        return this->paramNodes;
    }

    const std::vector<TypeToken *> &getParamTypeTokens() const {
        return this->paramTypeTokens;
    }

    void setReturnTypeToken(TypeToken *typeToken) {
        this->returnTypeToken = typeToken;
    }

    TypeToken *getReturnTypeToken();

    void setReturnType(DSType *returnType) {
        this->returnType = returnType;
    }

    DSType *getReturnType() const {
        return this->returnType;
    }

    void setBlockNode(BlockNode *blockNode) {
        this->blockNode = blockNode;
    }

    /**
     * return null before call setBlockNode()
     */
    BlockNode *getBlockNode() const {
        return this->blockNode;
    }

    void setMaxVarNum(unsigned int maxVarNum) {
        this->maxVarNum = maxVarNum;
    }

    unsigned int getMaxVarNum() const {
        return this->maxVarNum;
    }

    void setVarIndex(unsigned int varIndex) {
        this->varIndex = varIndex;
    }

    unsigned int getVarIndex() const {
        return this->varIndex;
    }

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
    InterfaceNode(unsigned int lineNum, std::string &&interfaceName) :
            Node(lineNum), interfaceName(std::move(interfaceName)), methodDeclNodes(),
            fieldDeclNodes(), fieldTypeTokens() { }

    ~InterfaceNode();

    const std::string &getInterfaceName() const {
        return this->interfaceName;
    }

    void addMethodDeclNode(FunctionNode *methodDeclNode);

    const std::vector<FunctionNode *> &getMethodDeclNodes() const {
        return this->methodDeclNodes;
    }

    /**
     * initNode of node is null.
     */
    void addFieldDecl(VarDeclNode *node, TypeToken *typeToken);

    const std::vector<VarDeclNode *> &getFieldDeclNodes() const {
        return this->fieldDeclNodes;
    }

    const std::vector<TypeToken *> &getFieldTypeTokens() const {
        return this->fieldTypeTokens;
    }

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class UserDefinedCmdNode : public Node {
private:
    std::string commandName;

    BlockNode *blockNode;

    const char *sourceName;

public:
    UserDefinedCmdNode(unsigned int lineNum, std::string &&commandName, BlockNode *blockNode) :
            Node(lineNum), commandName(std::move(commandName)), blockNode(blockNode), sourceName(0) { }

    ~UserDefinedCmdNode();

    const std::string &getCommandName() const {
        return this->commandName;
    }

    BlockNode *getBlockNode() const {
        return this->blockNode;
    }

    void setSourceName(const char *sourceName); // override
    const char *getSourceName(); // override

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
    DSValue value;

public:
    BindVarNode(const char *name, const DSValue &value) :
            Node(0), varName(std::string(name)), varIndex(0), value(value) { }

    ~BindVarNode() = default;

    const std::string &getVarName() const {
        return this->varName;
    }

    void setAttribute(FieldHandle *handle);

    unsigned int getVarIndex() const {
        return this->varIndex;
    }

    const DSValue &getValue() const {
        return this->value;
    }

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class EmptyNode : public Node {
public:
    EmptyNode() : EmptyNode(0) { }
    explicit EmptyNode(unsigned int lineNum) : Node(lineNum) { }
    ~EmptyNode() = default;

    void dump(Writer &writer) const;  // override
    void accept(NodeVisitor *visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class DummyNode : public Node {
public:
    DummyNode() : Node(0) { }
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
    explicit RootNode(const char *sourceName) :
            Node(0), sourceName(sourceName), nodeList(), maxVarNum(0), maxGVarNum(0) { }

    RootNode() : RootNode("") { }

    ~RootNode();

    const char *getSourceName();    // override
    void addNode(Node *node);

    const std::list<Node *> &getNodeList() const {
        return this->nodeList;
    }

    void setMaxVarNum(unsigned int maxVarNum) {
        this->maxVarNum = maxVarNum;
    }

    unsigned int getMaxVarNum() const {
        return this->maxVarNum;
    }

    void setMaxGVarNum(unsigned int maxGVarNum) {
        this->maxGVarNum = maxGVarNum;
    }

    unsigned int getMaxGVarNum() const {
        return this->maxGVarNum;
    }

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

    void visit(Node *node) { node->accept(this); }
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
    virtual void visitTildeNode(TildeNode *node) = 0;
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
    virtual void visitUserDefinedCmdNode(UserDefinedCmdNode *node) = 0;
    virtual void visitBindVarNode(BindVarNode *node) = 0;
    virtual void visitEmptyNode(EmptyNode *node) = 0;
    virtual void visitDummyNode(DummyNode *node) = 0;
    virtual void visitRootNode(RootNode *node) = 0;
};

struct BaseVisitor : public NodeVisitor {
    virtual ~BaseVisitor() = default;

    virtual void visitDefault(Node *node) = 0;

    virtual void visitIntValueNode(IntValueNode *node) { this->visitDefault(node); }
    virtual void visitLongValueNode(LongValueNode *node) { this->visitDefault(node); }
    virtual void visitFloatValueNode(FloatValueNode *node) { this->visitDefault(node); }
    virtual void visitStringValueNode(StringValueNode *node) { this->visitDefault(node); }
    virtual void visitObjectPathNode(ObjectPathNode *node) { this->visitDefault(node); }
    virtual void visitStringExprNode(StringExprNode *node) { this->visitDefault(node); }
    virtual void visitArrayNode(ArrayNode *node) { this->visitDefault(node); }
    virtual void visitMapNode(MapNode *node) { this->visitDefault(node); }
    virtual void visitTupleNode(TupleNode *node) { this->visitDefault(node); }
    virtual void visitVarNode(VarNode *node) { this->visitDefault(node); }
    virtual void visitAccessNode(AccessNode *node) { this->visitDefault(node); }
    virtual void visitCastNode(CastNode *node) { this->visitDefault(node); }
    virtual void visitInstanceOfNode(InstanceOfNode *node) { this->visitDefault(node); }
    virtual void visitUnaryOpNode(UnaryOpNode *node) { this->visitDefault(node); }
    virtual void visitBinaryOpNode(BinaryOpNode *node) { this->visitDefault(node); }
    virtual void visitArgsNode(ArgsNode *node) { this->visitDefault(node); }
    virtual void visitApplyNode(ApplyNode *node) { this->visitDefault(node); }
    virtual void visitMethodCallNode(MethodCallNode *node) { this->visitDefault(node); }
    virtual void visitNewNode(NewNode *node) { this->visitDefault(node); }
    virtual void visitGroupNode(GroupNode *node) { this->visitDefault(node); }
    virtual void visitCondOpNode(CondOpNode *node) { this->visitDefault(node); }
    virtual void visitCmdNode(CmdNode *node) { this->visitDefault(node); }
    virtual void visitCmdArgNode(CmdArgNode *node) { this->visitDefault(node); }
    virtual void visitRedirNode(RedirNode *node) { this->visitDefault(node); }
    virtual void visitTildeNode(TildeNode *node) { this->visitDefault(node); }
    virtual void visitPipedCmdNode(PipedCmdNode *node) { this->visitDefault(node); }
    virtual void visitCmdContextNode(CmdContextNode *node) { this->visitDefault(node); }
    virtual void visitAssertNode(AssertNode *node) { this->visitDefault(node); }
    virtual void visitBlockNode(BlockNode *node) { this->visitDefault(node); }
    virtual void visitBreakNode(BreakNode *node) { this->visitDefault(node); }
    virtual void visitContinueNode(ContinueNode *node) { this->visitDefault(node); }
    virtual void visitExportEnvNode(ExportEnvNode *node) { this->visitDefault(node); }
    virtual void visitImportEnvNode(ImportEnvNode *node) { this->visitDefault(node); }
    virtual void visitTypeAliasNode(TypeAliasNode *node) { this->visitDefault(node); }
    virtual void visitForNode(ForNode *node) { this->visitDefault(node); }
    virtual void visitWhileNode(WhileNode *node) { this->visitDefault(node); }
    virtual void visitDoWhileNode(DoWhileNode *node) { this->visitDefault(node); }
    virtual void visitIfNode(IfNode *node) { this->visitDefault(node); }
    virtual void visitReturnNode(ReturnNode *node) { this->visitDefault(node); }
    virtual void visitThrowNode(ThrowNode *node) { this->visitDefault(node); }
    virtual void visitCatchNode(CatchNode *node) { this->visitDefault(node); }
    virtual void visitTryNode(TryNode *node) { this->visitDefault(node); }
    virtual void visitVarDeclNode(VarDeclNode *node) { this->visitDefault(node); }
    virtual void visitAssignNode(AssignNode *node) { this->visitDefault(node); }
    virtual void visitElementSelfAssignNode(ElementSelfAssignNode *node) { this->visitDefault(node); }
    virtual void visitFunctionNode(FunctionNode *node) { this->visitDefault(node); }
    virtual void visitInterfaceNode(InterfaceNode *node) { this->visitDefault(node); }
    virtual void visitUserDefinedCmdNode(UserDefinedCmdNode *node) { this->visitDefault(node); }
    virtual void visitBindVarNode(BindVarNode *node) { this->visitDefault(node); }
    virtual void visitEmptyNode(EmptyNode *node) { this->visitDefault(node); }
    virtual void visitDummyNode(DummyNode *node) { this->visitDefault(node); }
    virtual void visitRootNode(RootNode *node) { this->visitDefault(node); }
};

} // namespace ast
} // namespace ydsh

#endif /* AST_NODE_H_ */
