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

#ifndef YDSH_NODE_H
#define YDSH_NODE_H

#include <utility>
#include <list>
#include <memory>

#include "../misc/flag_util.hpp"
#include "../misc/noncopyable.h"
#include "../parser/TokenKind.h"
#include "../parser/Lexer.h"
#include "../core/DSObject.h"

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
class NodeDumper;

class Node {
protected:
    unsigned int startPos;
    unsigned int size;

    /**
     * initial value is null.
     */
    DSType *type;

public:
    NON_COPYABLE(Node);

    explicit Node(Token token) :
            Node(token.startPos, token.size) { }

    Node(unsigned int startPos, unsigned int size) :
            startPos(startPos), size(size), type() { }

    virtual ~Node() = default;

    unsigned int getStartPos() const {
        return this->startPos;
    }

    unsigned int getSize() const {
        return this->size;
    }

    void setSize(unsigned int size) {
        this->size = size;
    }

    void updateSize(Token token) {
        this->updateSize(token.startPos, token.size);
    }

    void updateSize(unsigned int startPos, unsigned int size);

    virtual void setType(DSType &type);

    /**
     * must not call it before type checking
     */
    DSType &getType() const {
        return *this->type;
    }

    bool isUntyped() const {
        return this->type == nullptr;
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

    virtual bool isTerminalNode() const;

    virtual void dump(NodeDumper &dumper) const = 0;
    virtual void accept(NodeVisitor &visitor) = 0;
    virtual EvalStatus eval(RuntimeContext &ctx) = 0;
};

// type definition
/**
 * represent for parsed type.
 */
class TypeNode : public Node {
public:
    TypeNode(unsigned int startPos, unsigned int size) : Node(startPos, size) { }

    virtual ~TypeNode() = default;

    virtual void dump(NodeDumper &dumper) const = 0;
    virtual void accept(NodeVisitor &visitor) = 0;

    EvalStatus eval(RuntimeContext &ctx);   // override
};

class BaseTypeNode : public TypeNode {
private:
    std::string typeName;

public:
    BaseTypeNode(Token token, std::string &&typeName) :
            TypeNode(token.startPos, size), typeName(std::move(typeName)) { }

    ~BaseTypeNode() = default;

    const std::string &getTokenText() const {
        return this->typeName;
    }

    void dump(NodeDumper &dumper) const; // override
    void accept(NodeVisitor &visitor); // override
};

/**
 * for reified type and tuple type
 */
class ReifiedTypeNode : public TypeNode {
private:
    BaseTypeNode *templateTypeNode;
    std::vector<TypeNode *> elementTypeNodes;

public:
    explicit ReifiedTypeNode(BaseTypeNode *templateTypeNode) :
            TypeNode(templateTypeNode->getStartPos(), templateTypeNode->getSize()),
            templateTypeNode(templateTypeNode), elementTypeNodes() { }

    ~ReifiedTypeNode();

    void addElementTypeNode(TypeNode *typeNode);

    BaseTypeNode *getTemplate() const {
        return this->templateTypeNode;
    }

    const std::vector<TypeNode *> &getElementTypeNodes() const {
        return this->elementTypeNodes;
    }

    void dump(NodeDumper &dumper) const; // override
    void accept(NodeVisitor &visitor); // override
};

class FuncTypeNode : public TypeNode {
private:
    TypeNode *returnTypeNode;

    /**
     * may be empty vector, if has no parameter
     */
    std::vector<TypeNode *> paramTypeNodes;

public:
    FuncTypeNode(unsigned int startPos, TypeNode *returnTypeNode) :
            TypeNode(startPos, 0),
            returnTypeNode(returnTypeNode), paramTypeNodes() { }

    ~FuncTypeNode();

    void addParamTypeNode(TypeNode *typeNode);

    const std::vector<TypeNode *> &getParamTypeNodes() const {
        return this->paramTypeNodes;
    }

    TypeNode *getReturnTypeNode() const {
        return this->returnTypeNode;
    }

    void dump(NodeDumper &dumper) const; // override
    void accept(NodeVisitor &visitor); // override
};

class DBusIfaceTypeNode : public TypeNode {
private:
    /**
     * must be valid interface name.
     * ex. org.freedesktop.NetworkManager
     */
    std::string name;

public:
    DBusIfaceTypeNode(Token token, std::string &&name) :
            TypeNode(token.startPos, token.size), name(std::move(name)) { }

    ~DBusIfaceTypeNode() = default;

    const std::string &getTokenText() const {
        return this->name;
    }

    void dump(NodeDumper &dumper) const; // override
    void accept(NodeVisitor &visitor); // override
};

/**
 * for multiple return type
 */
class ReturnTypeNode : public TypeNode {
private:
    std::vector<TypeNode *> typeNodes;

public:
    explicit ReturnTypeNode(TypeNode *typeNode);
    ~ReturnTypeNode();

    void addTypeNode(TypeNode *typeNode);

    const std::vector<TypeNode *> &getTypeNodes() const {
        return this->typeNodes;
    }

    bool hasMultiReturn() const {
        return this->typeNodes.size() > 1;
    }

    void dump(NodeDumper &dumper) const; // override
    void accept(NodeVisitor &visitor); // override
};

class TypeOfNode : public TypeNode {
private:
    Node *exprNode;

public:
    TypeOfNode(unsigned int startPos, Node *exprNode);
    ~TypeOfNode();

    Node *getExprNode() const {
        return this->exprNode;
    }

    void dump(NodeDumper &dumper) const; // override
    void accept(NodeVisitor &visitor); // override
};

TypeNode *newAnyTypeNode();

TypeNode *newVoidTypeNode();



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
    IntValueNode(Token token, IntKind kind, int value) :
            Node(token), kind(kind), tempValue(value), value() { }

public:
    IntValueNode(Token token, int value) :
            IntValueNode(token, INT32, value) { }

    static IntValueNode *newByte(Token token, unsigned char value) {
        return new IntValueNode(token, BYTE, (int) value);
    }

    static IntValueNode *newInt16(Token token, short value) {
        return new IntValueNode(token, INT16, (int) value);
    }

    static IntValueNode *newUint16(Token token, unsigned short value) {
        return new IntValueNode(token, UINT16, (int) value);
    }

    static IntValueNode *newInt32(Token token, int value) {
        return new IntValueNode(token, INT32, value);
    }

    static IntValueNode *newUint32(Token token, unsigned int value) {
        return new IntValueNode(token, UINT32, (int) value);
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

    void setType(DSType &type); // override
    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class LongValueNode : public Node {
private:
    long tempValue;
    bool unsignedValue;

    DSValue value;

public:
    LongValueNode(Token token, long value, bool unsignedValue) :
            Node(token), tempValue(value), unsignedValue(unsignedValue), value() { }

    static LongValueNode *newInt64(Token token, long value) {
        return new LongValueNode(token, value, false);
    }

    static LongValueNode *newUint64(Token token, unsigned long value) {
        return new LongValueNode(token, (long) value, true);
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

    void setType(DSType &type); // override
    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
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
    FloatValueNode(Token token, double value) :
            Node(token), tempValue(value), value() { }

    /**
     * before type check, return empty pointer.
     */
    const DSValue &getValue() const {
        return this->value;
    }

    void setType(DSType &type); // override
    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
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
            Node(0, 0), tempValue(std::move(value)), value() { }

    StringValueNode(Token token, std::string &&value) :
            Node(token), tempValue(std::move(value)), value() { }

    virtual ~StringValueNode() = default;

    /**
     * before type check, return empty pointer.
     */
    const DSValue &getValue() const {
        return this->value;
    }

    void setType(DSType &type); // override
    void dump(NodeDumper &dumper) const;  // override
    virtual void accept(NodeVisitor &visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class ObjectPathNode : public StringValueNode {
public:
    ObjectPathNode(Token token, std::string &&value) :
            StringValueNode(token, std::move(value)) { }

    ~ObjectPathNode() = default;

    void accept(NodeVisitor &visitor); // override
};

class StringExprNode : public Node {
private:
    std::vector<Node *> nodes;

public:
    explicit StringExprNode(unsigned int startPos) : Node(startPos, 1), nodes() { }

    ~StringExprNode();

    void addExprNode(Node *node);

    const std::vector<Node *> &getExprNodes() const {
        return this->nodes;
    }

    void setExprNode(unsigned int index, Node *exprNode) {
        this->nodes[index] = exprNode;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class ArrayNode : public Node {
private:
    std::vector<Node *> nodes;

public:
    ArrayNode(unsigned int startPos, Node *node);

    ~ArrayNode();

    void addExprNode(Node *node);

    const std::vector<Node *> &getExprNodes() const {
        return this->nodes;
    }

    std::vector<Node *> &refExprNodes() {
        return this->nodes;
    }

    void dump(NodeDumper &dumper) const; // override
    void accept(NodeVisitor &visitor);    //override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class MapNode : public Node {
private:
    std::vector<Node *> keyNodes;
    std::vector<Node *> valueNodes;

public:
    MapNode(unsigned int startPos, Node *keyNode, Node *valueNode);

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

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class TupleNode : public Node {
private:
    /**
     * at least two nodes
     */
    std::vector<Node *> nodes;

public:
    TupleNode(unsigned int startPos, Node *leftNode, Node *rightNode);

    ~TupleNode();

    void addNode(Node *node);

    const std::vector<Node *> &getNodes() const {
        return this->nodes;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
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
    explicit AssignableNode(unsigned int startPos, unsigned int size) :
            Node(startPos, size), index(0),
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

    virtual void dump(NodeDumper &dumper) const;  // override
};

class VarNode : public AssignableNode {
private:
    std::string varName;

public:
    VarNode(Token token, std::string &&varName) :
            AssignableNode(token.startPos, token.size), varName(std::move(varName)) { }

    ~VarNode() = default;

    const std::string &getVarName() const {
        return this->varName;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
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
            AssignableNode(recvNode->getStartPos(), recvNode->getSize()),
            recvNode(recvNode), fieldName(std::move(fieldName)), additionalOp(NOP) { }

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

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
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
        TO_VOID,
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
    TypeNode *targetTypeNode;

    CastOp opKind;

public:
    CastNode(Node *exprNode, TypeNode *type, bool dupTypeToken = false);

    ~CastNode();

    Node *getExprNode() const {
        return this->exprNode;
    }

    TypeNode *getTargetTypeNode() const;

    void setOpKind(CastOp opKind) {
        this->opKind = opKind;
    }

    CastOp getOpKind() const {
        return this->opKind;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    //override
    EvalStatus eval(RuntimeContext &ctx); // override

    /**
     * for implicit cast.
     * targetNode must be typed node.
     * type is after casted value type.
     */
    static CastNode *newTypedCastNode(Node *targetNode, DSType &type, CastOp op);
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
    TypeNode *targetTypeNode;
    InstanceOfOp opKind;

public:
    InstanceOfNode(Node *targetNode, TypeNode *typeNode);

    ~InstanceOfNode();

    Node *getTargetNode() const {
        return this->targetNode;
    }

    TypeNode *getTargetTypeNode() const {
        return this->targetTypeNode;
    }

    void setOpKind(InstanceOfOp opKind) {
        this->opKind = opKind;
    }

    InstanceOfOp getOpKind() const {
        return this->opKind;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    //override
    EvalStatus eval(RuntimeContext &ctx); // override
};

/**
 * for function object apply
 */
class ApplyNode : public Node {
private:
    Node *exprNode;
    std::vector<Node *> argNodes;

public:
    ApplyNode(Node *exprNode, std::vector<Node *> &&argNodes);

    ~ApplyNode();

    Node *getExprNode() const {
        return this->exprNode;
    }

    const std::vector<Node *> &getArgNodes() {
        return this->argNodes;
    }

    std::vector<Node *> &refArgNodes() {
        return this->argNodes;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class MethodCallNode : public Node {
private:
    Node *recvNode;
    std::string methodName;
    std::vector<Node *> argNodes;

    MethodHandle *handle;

    flag8_set_t attributeSet;

public:
    MethodCallNode(Node *recvNode, std::string &&methodName) :
            MethodCallNode(recvNode, std::move(methodName), std::vector<Node *>()) { }

    MethodCallNode(Node *recvNode, std::string &&methodName, std::vector<Node *> &&argNodes);

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

    const std::vector<Node *> &getArgNodes() const {
        return this->argNodes;
    }

    std::vector<Node *> &refArgNodes() {
        return this->argNodes;
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

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override

    static constexpr flag8_t INDEX = 1 << 0;
    static constexpr flag8_t ICALL = 1 << 1;
};

/**
 * allocate new DSObject and call constructor.
 */
class NewNode : public Node {
private:
    TypeNode *targetTypeNode;
    std::vector<Node *> argNodes;

public:
    NewNode(unsigned int startPos, TypeNode *targetTypeNode, std::vector<Node *> &&argNodes);

    ~NewNode();

    TypeNode *getTargetTypeNode() const {
        return this->targetTypeNode;
    }

    const std::vector<Node *> &getArgNodes() const {
        return this->argNodes;
    }

    std::vector<Node *> &refArgNodes() {
        return this->argNodes;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);   // override
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
    UnaryOpNode(unsigned int startPos, TokenKind op, Node *exprNode) :
            Node(startPos, 0), op(op), exprNode(exprNode), methodCallNode(0) {
        this->updateSize(exprNode->getStartPos(), exprNode->getSize());
    }

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

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);   // override
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
            Node(leftNode->getStartPos(), 0),
            leftNode(leftNode), rightNode(rightNode), op(op), methodCallNode(0) {
        this->updateSize(rightNode->getStartPos(), rightNode->getSize());
    }

    ~BinaryOpNode();

    Node *getLeftNode() const {
        return this->leftNode;
    }

    Node *&refLeftNode() {
        return this->leftNode;
    }

    Node *getRightNode() const {
        return this->rightNode;
    }

    Node *&refRightNode() {
        return this->rightNode;
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

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);   // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

/**
 * represent for '(' expr ')'
 */
class GroupNode : public Node {
private:
    Node *exprNode;

public:
    GroupNode(unsigned int startPos, Node *exprNode) :
            Node(startPos, 0), exprNode(exprNode) { }

    ~GroupNode();

    Node *getExprNode() const {
        return this->exprNode;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);   // override
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

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    //override
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
            Node(segmentNode->getStartPos(), 0), segmentNodes() {
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

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);  // override
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

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);  // override
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
    TildeNode(Token token, std::string &&value) :
            Node(token), value(std::move(value)) { }

    ~TildeNode() = default;

    const std::string &getValue() {
        return this->value;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);  // override

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
    CmdNode(Token token, std::string &&value) :
            Node(token),
            nameNode(new StringValueNode(token, std::move(value))), argNodes() { }

    explicit CmdNode(TildeNode *nameNode) :
            Node(nameNode->getStartPos(), nameNode->getSize()), nameNode(nameNode), argNodes() { }

    ~CmdNode();

    Node *getNameNode() const {
        return this->nameNode;
    }

    void addArgNode(CmdArgNode *node);

    const std::vector<Node *> &getArgNodes() const {
        return this->argNodes;
    }

    void addRedirOption(TokenKind kind, CmdArgNode *node);
    void addRedirOption(TokenKind kind, Token token);

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    //override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class PipedCmdNode : public Node {    //TODO: background ...etc
private:
    std::vector<Node *> cmdNodes;
    bool asBool;

public:
    explicit PipedCmdNode(Node *node) :
            Node(node->getStartPos(), node->getSize()), cmdNodes(), asBool(false) {
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

    void dump(NodeDumper &dumper) const; // override
    void accept(NodeVisitor &visitor);    //override
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
            Node(exprNode->getStartPos(), exprNode->getSize()),
            exprNode(exprNode), attributeSet(0) {
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

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    //override
    EvalStatus eval(RuntimeContext &ctx); // override

    static constexpr flag8_t BACKGROUND = 1 << 0;
    static constexpr flag8_t FORK       = 1 << 1;
    static constexpr flag8_t STR_CAP    = 1 << 2;
    static constexpr flag8_t ARRAY_CAP  = 1 << 3;
    static constexpr flag8_t CONDITION  = 1 << 4;
};

// statement definition

class AssertNode : public Node {
private:
    Node *condNode;

public:
    AssertNode(unsigned int startPos, Node *condNode) :
            Node(startPos, 0), condNode(condNode) {
        this->condNode->inCondition();
    }

    ~AssertNode();

    Node *getCondNode() const {
        return this->condNode;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class BlockNode : public Node {
private:
    std::list<Node *> nodeList;

public:
    explicit BlockNode(unsigned int startPos) : Node(startPos, 1), nodeList() { }

    ~BlockNode();

    void addNode(Node *node);

    void insertNodeToFirst(Node *node);

    const std::list<Node *> &getNodeList() const {
        return this->nodeList;
    }

    std::list<Node *> &refNodeList() {
        return this->nodeList;
    }

    bool isTerminalNode() const; // overrdie

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

/**
 * base class for break, continue, return, throw node
 */
class BlockEndNode : public Node {
public:
    BlockEndNode(unsigned int startPos, unsigned int size) : Node(startPos, size) { }

    virtual ~BlockEndNode() = default;

    bool isTerminalNode() const { // override
        return true;
    }
};

class BreakNode : public BlockEndNode {
public:
    explicit BreakNode(Token token) :
            BlockEndNode(token.startPos, token.size) { }

    ~BreakNode() = default;

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class ContinueNode : public BlockEndNode {
public:
    explicit ContinueNode(Token token) :
            BlockEndNode(token.startPos, token.size) { }

    ~ContinueNode() = default;

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class ExportEnvNode : public Node {
private:
    std::string envName;
    Node *exprNode;
    bool global;
    unsigned int varIndex;

public:
    ExportEnvNode(unsigned int startPos, std::string &&envName, Node *exprNode) :
            Node(startPos, 0), envName(std::move(envName)), exprNode(exprNode),
            global(false), varIndex(0) {
        this->updateSize(exprNode->getStartPos(), exprNode->getSize());
    }

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

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
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
    ImportEnvNode(unsigned int startPos, std::string &&envName) :
            Node(startPos, 0), envName(std::move(envName)),
            defaultValueNode(nullptr), global(false), varIndex(0) { }

    ~ImportEnvNode();

    const std::string &getEnvName() const {
        return this->envName;
    }

    void setDefaultValueNode(Node *node) {
        this->defaultValueNode = node;
        this->updateSize(this->defaultValueNode->getStartPos(),
                         this->defaultValueNode->getSize());
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

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class TypeAliasNode : public Node {
private:
    std::string alias;
    TypeNode *targetTypeNode;

public:
    TypeAliasNode(unsigned int startPos, std::string &&alias, TypeNode *targetTypeNode) :
            Node(startPos, 0), alias(std::move(alias)), targetTypeNode(targetTypeNode) {
        this->updateSize(targetTypeNode->getStartPos(), targetTypeNode->getSize());
    }

    ~TypeAliasNode();

    const std::string &getAlias() const {
        return this->alias;
    }

    TypeNode *getTargetTypeNode() const {
        return this->targetTypeNode;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
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
    ForNode(unsigned int startPos, Node *initNode, Node *condNode, Node *iterNode, BlockNode *blockNode);

    ~ForNode();

    Node *getInitNode() const {
        return this->initNode;
    }

    Node *&refInitNode() {
        return this->initNode;
    }

    Node *getCondNode() const {
        return this->condNode;
    }

    Node *getIterNode() const {
        return this->iterNode;
    }

    Node * &refIterNode() {
        return this->iterNode;
    }

    BlockNode *getBlockNode() const {
        return this->blockNode;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class WhileNode : public Node {
private:
    Node *condNode;
    BlockNode *blockNode;

public:
    WhileNode(unsigned int startPos, Node *condNode, BlockNode *blockNode) :
            Node(startPos, 0), condNode(condNode), blockNode(blockNode) {
        this->condNode->inCondition();
        this->updateSize(blockNode->getStartPos(), blockNode->getSize());
    }

    ~WhileNode();

    Node *getCondNode() const {
        return this->condNode;
    }

    BlockNode *getBlockNode() const {
        return this->blockNode;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    //override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class DoWhileNode : public Node {
private:
    BlockNode *blockNode;
    Node *condNode;

public:
    DoWhileNode(unsigned int startPos, BlockNode *blockNode, Node *condNode) :
            Node(startPos, 0), blockNode(blockNode), condNode(condNode) {
        this->condNode->inCondition();
    }

    ~DoWhileNode();

    BlockNode *getBlockNode() const {
        return this->blockNode;
    }

    Node *getCondNode() const {
        return this->condNode;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);   //override
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

    bool terminal;

public:
    /**
     * elseNode may be null
     */
    IfNode(unsigned int startPos, Node *condNode, BlockNode *thenNode);

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
        this->updateSize(elseNode->getStartPos(), elseNode->getSize());
    }

    /*
     * return EmptyBlockNode, if elseNode is null.
     */
    BlockNode *getElseNode();

    void setTerminal(bool terminal) {
        this->terminal = terminal;
    }

    bool isTerminalNode() const {   // override
        return this->terminal;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class ReturnNode : public BlockEndNode {
private:
    Node *exprNode;

public:
    ReturnNode(unsigned int startPos, Node *exprNode) :
            BlockEndNode(startPos, 0), exprNode(exprNode) {
        this->updateSize(exprNode->getStartPos(), exprNode->getSize());
    }

    explicit ReturnNode(Token token);

    ~ReturnNode();

    Node *getExprNode() const {
        return this->exprNode;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class ThrowNode : public BlockEndNode {
private:
    Node *exprNode;

public:
    ThrowNode(unsigned int startPos, Node *exprNode) :
            BlockEndNode(startPos, 0), exprNode(exprNode) {
        this->updateSize(exprNode->getStartPos(), exprNode->getSize());
    }

    ~ThrowNode();

    Node *getExprNode() const {
        return this->exprNode;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class CatchNode : public Node {
private:
    std::string exceptionName;
    TypeNode *typeNode;

    unsigned int varIndex;

    BlockNode *blockNode;

public:
    CatchNode(unsigned int startPos, std::string &&exceptionName, BlockNode *blockNode) :
            CatchNode(startPos, std::move(exceptionName), newAnyTypeNode(), blockNode) { }

    CatchNode(unsigned int startPos, std::string &&exceptionName,
              TypeNode *typeNode, BlockNode *blockNode) :
            Node(startPos, 0), exceptionName(std::move(exceptionName)),
            typeNode(typeNode), varIndex(0), blockNode(blockNode) {
        this->updateSize(blockNode->getStartPos(), blockNode->getSize());
    }

    ~CatchNode();

    const std::string &getExceptionName() const {
        return this->exceptionName;
    }

    TypeNode *getTypeNode() const {
        return this->typeNode;
    }

    void setAttribute(FieldHandle *handle);

    unsigned int getVarIndex() const {
        return this->varIndex;
    }

    BlockNode *getBlockNode() const {
        return this->blockNode;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
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

    bool terminal;

public:
    TryNode(unsigned int startPos, BlockNode *blockNode) :
            Node(startPos, 0), blockNode(blockNode), catchNodes(), finallyNode(), terminal(false) {
        this->updateSize(blockNode->getStartPos(), blockNode->getSize());
    }

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

    void setTerminal(bool terminal) {
        this->terminal = terminal;
    }

    bool isTerminalNode() const {   // override
        return this->terminal;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
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
    VarDeclNode(unsigned int startPos, std::string &&varName, Node *initValueNode, bool readOnly);

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

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
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
            Node(leftNode->getStartPos(), 0),
            leftNode(leftNode), rightNode(rightNode), attributeSet(0) {
        if(selfAssign) {
            setFlag(this->attributeSet, SELF_ASSIGN);
        }
        this->updateSize(rightNode->getStartPos(), rightNode->getSize());
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

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);   // override
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
    void setRecvType(DSType &type);

    /**
     * add index type of getterNode and setterNode.
     */
    void setIndexType(DSType &type);

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);   // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class CallableNode : public Node {
protected:
    SourceInfoPtr srcInfoPtr;

public:
    CallableNode(unsigned int startPos, const SourceInfoPtr &srcInfoPtr) :
            Node(startPos, 0), srcInfoPtr(srcInfoPtr) { }

    CallableNode() : Node(0, 0), srcInfoPtr(nullptr) { }

    virtual ~CallableNode() = default;

    const SourceInfoPtr &getSourceInfoPtr() const {
        return this->srcInfoPtr;
    }

    void setSourceInfoPtr(const SourceInfoPtr &srcInfoPtr) {
        this->srcInfoPtr = srcInfoPtr;
    }

    const char *getSourceName() const {
        return this->srcInfoPtr->getSourceName().c_str();
    }
};

class FunctionNode : public CallableNode {
private:
    std::string funcName;

    /**
     * for parameter definition.
     */
    std::vector<VarNode *> paramNodes;

    /**
     * type token of each parameter
     */
    std::vector<TypeNode *> paramTypeNodes;

    TypeNode *returnTypeNode;

    BlockNode *blockNode;

    /**
     * maximum number of local variable in function
     */
    unsigned int maxVarNum;

    /**
     * global variable table index of this function
     */
    unsigned int varIndex;

public:
    FunctionNode(unsigned int startPos, const SourceInfoPtr &srcInfoPtr,
                 std::string &&funcName) :
            CallableNode(startPos, srcInfoPtr), funcName(std::move(funcName)),
            paramNodes(), paramTypeNodes(), returnTypeNode(),
            blockNode(), maxVarNum(0), varIndex(0) { }

    ~FunctionNode();

    const std::string &getFuncName() const {
        return this->funcName;
    }

    void addParamNode(VarNode *node, TypeNode *paramType);

    const std::vector<VarNode *> &getParamNodes() const {
        return this->paramNodes;
    }

    const std::vector<TypeNode *> &getParamTypeNodes() const {
        return this->paramTypeNodes;
    }

    void setReturnTypeToken(TypeNode *typeToken) {
        this->returnTypeNode = typeToken;
        this->updateSize(typeToken->getStartPos(), typeToken->getSize());
    }

    TypeNode *getReturnTypeToken();

    void setBlockNode(BlockNode *blockNode) {
        this->blockNode = blockNode;
        this->updateSize(blockNode->getStartPos(), blockNode->getSize());
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

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class InterfaceNode : public Node {
private:
    std::string interfaceName;

    std::vector<FunctionNode *> methodDeclNodes;
    std::vector<VarDeclNode *> fieldDeclNodes;
    std::vector<TypeNode *> fieldTypeNodes;

public:
    InterfaceNode(unsigned int startPos, std::string &&interfaceName) :
            Node(startPos, 0), interfaceName(std::move(interfaceName)), methodDeclNodes(),
            fieldDeclNodes(), fieldTypeNodes() { }

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
    void addFieldDecl(VarDeclNode *node, TypeNode *typeNode);

    const std::vector<VarDeclNode *> &getFieldDeclNodes() const {
        return this->fieldDeclNodes;
    }

    const std::vector<TypeNode *> &getFieldTypeNodes() const {
        return this->fieldTypeNodes;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class UserDefinedCmdNode : public CallableNode {
private:
    std::string commandName;

    BlockNode *blockNode;

    unsigned int maxVarNum;

public:
    UserDefinedCmdNode(unsigned int startPos, const SourceInfoPtr &srcInfoPtr,
                       std::string &&commandName, BlockNode *blockNode) :
            CallableNode(startPos, srcInfoPtr), commandName(std::move(commandName)),
            blockNode(blockNode), maxVarNum(0) {
        this->updateSize(blockNode->getStartPos(), blockNode->getSize());
    }

    ~UserDefinedCmdNode();

    const std::string &getCommandName() const {
        return this->commandName;
    }

    BlockNode *getBlockNode() const {
        return this->blockNode;
    }

    void setMaxVarNum(unsigned int maxVarNum) {
        this->maxVarNum = maxVarNum;
    }

    unsigned int getMaxVarNum() const {
        return this->maxVarNum;
    }

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

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
            Node(0, 0), varName(std::string(name)), varIndex(0), value(value) { }

    BindVarNode(const char *name, DSValue &&value) :
            Node(0, 0), varName(std::string(name)), varIndex(0), value(std::move(value)) { }

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

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class EmptyNode : public Node {
public:
    EmptyNode() : Node(0, 0) { }
    EmptyNode(Token token) :
            Node(token.startPos, token.size) { }
    ~EmptyNode() = default;

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);    // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

class DummyNode : public Node {
public:
    DummyNode() : Node(0, 0) { }
    ~DummyNode() = default;

    void dump(NodeDumper &dumper) const;  // override
    void accept(NodeVisitor &visitor);   // override
    EvalStatus eval(RuntimeContext &ctx); // override
};

/**
 * Root Node of AST.
 * this class is not inheritance of Node
 */
class RootNode : public CallableNode {    //FIXME:
private:
    std::list<Node *> nodeList;

    /**
     * max number of local variable.
     */
    unsigned int maxVarNum;

    /**
     * max number of global variable.
     */
    unsigned int maxGVarNum;

public:
    RootNode() : CallableNode(), nodeList(), maxVarNum(0), maxGVarNum(0) { }

    ~RootNode();

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

    void dump(NodeDumper &dumper) const;    // override
    void accept(NodeVisitor &visitor);  // override
    EvalStatus eval(RuntimeContext &ctx);   // override
};

// helper function for node creation

const char *resolveUnaryOpName(TokenKind op);

const char *resolveBinaryOpName(TokenKind op);

TokenKind resolveAssignOp(TokenKind op);

/**
 * create ApplyNode or MethodCallNode.
 */
Node *createCallNode(Node *recvNode, std::vector<Node *> &&argNodes);

ForNode *createForInNode(unsigned int startPos, VarNode *varNode, Node *exprNode, BlockNode *blockNode);

Node *createSuffixNode(Node *leftNode, TokenKind op, Token token);

Node *createAssignNode(Node *leftNode, TokenKind op, Node *rightNode);

Node *createIndexNode(Node *recvNode, Node *indexNode);

Node *createBinaryOpNode(Node *leftNode, TokenKind op, Node *rightNode);

struct NodeVisitor {
    virtual ~NodeVisitor() = default;

    virtual void visit(Node &node) { node.accept(*this); }
    virtual void visitBaseTypeNode(BaseTypeNode &node) = 0;
    virtual void visitReifiedTypeNode(ReifiedTypeNode &node) = 0;
    virtual void visitFuncTypeNode(FuncTypeNode &node) = 0;
    virtual void visitDBusIfaceTypeNode(DBusIfaceTypeNode &node) = 0;
    virtual void visitReturnTypeNode(ReturnTypeNode &node) = 0;
    virtual void visitTypeOfNode(TypeOfNode &node) = 0;
    virtual void visitIntValueNode(IntValueNode &node) = 0;
    virtual void visitLongValueNode(LongValueNode &node) = 0;
    virtual void visitFloatValueNode(FloatValueNode &node) = 0;
    virtual void visitStringValueNode(StringValueNode &node) = 0;
    virtual void visitObjectPathNode(ObjectPathNode &node) = 0;
    virtual void visitStringExprNode(StringExprNode &node) = 0;
    virtual void visitArrayNode(ArrayNode &node) = 0;
    virtual void visitMapNode(MapNode &node) = 0;
    virtual void visitTupleNode(TupleNode &node) = 0;
    virtual void visitVarNode(VarNode &node) = 0;
    virtual void visitAccessNode(AccessNode &node) = 0;
    virtual void visitCastNode(CastNode &node) = 0;
    virtual void visitInstanceOfNode(InstanceOfNode &node) = 0;
    virtual void visitUnaryOpNode(UnaryOpNode &node) = 0;
    virtual void visitBinaryOpNode(BinaryOpNode &node) = 0;
    virtual void visitApplyNode(ApplyNode &node) = 0;
    virtual void visitMethodCallNode(MethodCallNode &node) = 0;
    virtual void visitNewNode(NewNode &node) = 0;
    virtual void visitGroupNode(GroupNode &node) = 0;
    virtual void visitCondOpNode(CondOpNode &node) = 0;
    virtual void visitCmdNode(CmdNode &node) = 0;
    virtual void visitCmdArgNode(CmdArgNode &node) = 0;
    virtual void visitRedirNode(RedirNode &node) = 0;
    virtual void visitTildeNode(TildeNode &node) = 0;
    virtual void visitPipedCmdNode(PipedCmdNode &node) = 0;
    virtual void visitCmdContextNode(CmdContextNode &node) = 0;
    virtual void visitAssertNode(AssertNode &node) = 0;
    virtual void visitBlockNode(BlockNode &node) = 0;
    virtual void visitBreakNode(BreakNode &node) = 0;
    virtual void visitContinueNode(ContinueNode &node) = 0;
    virtual void visitExportEnvNode(ExportEnvNode &node) = 0;
    virtual void visitImportEnvNode(ImportEnvNode &node) = 0;
    virtual void visitTypeAliasNode(TypeAliasNode &node) = 0;
    virtual void visitForNode(ForNode &node) = 0;
    virtual void visitWhileNode(WhileNode &node) = 0;
    virtual void visitDoWhileNode(DoWhileNode &node) = 0;
    virtual void visitIfNode(IfNode &node) = 0;
    virtual void visitReturnNode(ReturnNode &node) = 0;
    virtual void visitThrowNode(ThrowNode &node) = 0;
    virtual void visitCatchNode(CatchNode &node) = 0;
    virtual void visitTryNode(TryNode &node) = 0;
    virtual void visitVarDeclNode(VarDeclNode &node) = 0;
    virtual void visitAssignNode(AssignNode &node) = 0;
    virtual void visitElementSelfAssignNode(ElementSelfAssignNode &node) = 0;
    virtual void visitFunctionNode(FunctionNode &node) = 0;
    virtual void visitInterfaceNode(InterfaceNode &node) = 0;
    virtual void visitUserDefinedCmdNode(UserDefinedCmdNode &node) = 0;
    virtual void visitBindVarNode(BindVarNode &node) = 0;
    virtual void visitEmptyNode(EmptyNode &node) = 0;
    virtual void visitDummyNode(DummyNode &node) = 0;
    virtual void visitRootNode(RootNode &node) = 0;
};

struct BaseVisitor : public NodeVisitor {
    virtual ~BaseVisitor() = default;

    virtual void visitDefault(Node &node) = 0;

    virtual void visitBaseTypeNode(BaseTypeNode &node) { this->visitDefault(node); }
    virtual void visitReifiedTypeNode(ReifiedTypeNode &node) { this->visitDefault(node); }
    virtual void visitFuncTypeNode(FuncTypeNode &node) { this->visitDefault(node); }
    virtual void visitDBusIfaceTypeNode(DBusIfaceTypeNode &node) { this->visitDefault(node); }
    virtual void visitReturnTypeNode(ReturnTypeNode &node) { this->visitDefault(node); }
    virtual void visitTypeOfNode(TypeOfNode &node) { this->visitDefault(node); }
    virtual void visitIntValueNode(IntValueNode &node) { this->visitDefault(node); }
    virtual void visitLongValueNode(LongValueNode &node) { this->visitDefault(node); }
    virtual void visitFloatValueNode(FloatValueNode &node) { this->visitDefault(node); }
    virtual void visitStringValueNode(StringValueNode &node) { this->visitDefault(node); }
    virtual void visitObjectPathNode(ObjectPathNode &node) { this->visitDefault(node); }
    virtual void visitStringExprNode(StringExprNode &node) { this->visitDefault(node); }
    virtual void visitArrayNode(ArrayNode &node) { this->visitDefault(node); }
    virtual void visitMapNode(MapNode &node) { this->visitDefault(node); }
    virtual void visitTupleNode(TupleNode &node) { this->visitDefault(node); }
    virtual void visitVarNode(VarNode &node) { this->visitDefault(node); }
    virtual void visitAccessNode(AccessNode &node) { this->visitDefault(node); }
    virtual void visitCastNode(CastNode &node) { this->visitDefault(node); }
    virtual void visitInstanceOfNode(InstanceOfNode &node) { this->visitDefault(node); }
    virtual void visitUnaryOpNode(UnaryOpNode &node) { this->visitDefault(node); }
    virtual void visitBinaryOpNode(BinaryOpNode &node) { this->visitDefault(node); }
    virtual void visitApplyNode(ApplyNode &node) { this->visitDefault(node); }
    virtual void visitMethodCallNode(MethodCallNode &node) { this->visitDefault(node); }
    virtual void visitNewNode(NewNode &node) { this->visitDefault(node); }
    virtual void visitGroupNode(GroupNode &node) { this->visitDefault(node); }
    virtual void visitCondOpNode(CondOpNode &node) { this->visitDefault(node); }
    virtual void visitCmdNode(CmdNode &node) { this->visitDefault(node); }
    virtual void visitCmdArgNode(CmdArgNode &node) { this->visitDefault(node); }
    virtual void visitRedirNode(RedirNode &node) { this->visitDefault(node); }
    virtual void visitTildeNode(TildeNode &node) { this->visitDefault(node); }
    virtual void visitPipedCmdNode(PipedCmdNode &node) { this->visitDefault(node); }
    virtual void visitCmdContextNode(CmdContextNode &node) { this->visitDefault(node); }
    virtual void visitAssertNode(AssertNode &node) { this->visitDefault(node); }
    virtual void visitBlockNode(BlockNode &node) { this->visitDefault(node); }
    virtual void visitBreakNode(BreakNode &node) { this->visitDefault(node); }
    virtual void visitContinueNode(ContinueNode &node) { this->visitDefault(node); }
    virtual void visitExportEnvNode(ExportEnvNode &node) { this->visitDefault(node); }
    virtual void visitImportEnvNode(ImportEnvNode &node) { this->visitDefault(node); }
    virtual void visitTypeAliasNode(TypeAliasNode &node) { this->visitDefault(node); }
    virtual void visitForNode(ForNode &node) { this->visitDefault(node); }
    virtual void visitWhileNode(WhileNode &node) { this->visitDefault(node); }
    virtual void visitDoWhileNode(DoWhileNode &node) { this->visitDefault(node); }
    virtual void visitIfNode(IfNode &node) { this->visitDefault(node); }
    virtual void visitReturnNode(ReturnNode &node) { this->visitDefault(node); }
    virtual void visitThrowNode(ThrowNode &node) { this->visitDefault(node); }
    virtual void visitCatchNode(CatchNode &node) { this->visitDefault(node); }
    virtual void visitTryNode(TryNode &node) { this->visitDefault(node); }
    virtual void visitVarDeclNode(VarDeclNode &node) { this->visitDefault(node); }
    virtual void visitAssignNode(AssignNode &node) { this->visitDefault(node); }
    virtual void visitElementSelfAssignNode(ElementSelfAssignNode &node) { this->visitDefault(node); }
    virtual void visitFunctionNode(FunctionNode &node) { this->visitDefault(node); }
    virtual void visitInterfaceNode(InterfaceNode &node) { this->visitDefault(node); }
    virtual void visitUserDefinedCmdNode(UserDefinedCmdNode &node) { this->visitDefault(node); }
    virtual void visitBindVarNode(BindVarNode &node) { this->visitDefault(node); }
    virtual void visitEmptyNode(EmptyNode &node) { this->visitDefault(node); }
    virtual void visitDummyNode(DummyNode &node) { this->visitDefault(node); }
    virtual void visitRootNode(RootNode &node) { this->visitDefault(node); }
};

} // namespace ast
} // namespace ydsh

#endif //YDSH_NODE_H
