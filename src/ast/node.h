/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#ifndef YDSH_AST_NODE_H
#define YDSH_AST_NODE_H

#include <utility>
#include <list>
#include <memory>

#include "../misc/flag_util.hpp"
#include "../misc/noncopyable.h"
#include "../parser/token_kind.h"
#include "../parser/lexer.h"
#include "../core/object.h"

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
    Token token;

    /**
     * initial value is null.
     */
    DSType *type;

public:
    NON_COPYABLE(Node);

    explicit Node(Token token) :
            token(token), type() { }

    virtual ~Node() = default;

    Token getToken() const {
        return this->token;
    }

    unsigned int getStartPos() const {
        return this->token.pos;
    }

    unsigned int getSize() const {
        return this->token.size;
    }

    void updateToken(Token token);

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
    explicit TypeNode(Token token) : Node(token) { }

    virtual ~TypeNode() = default;

    virtual void dump(NodeDumper &dumper) const override = 0;
    virtual void accept(NodeVisitor &visitor) override = 0;

    EvalStatus eval(RuntimeContext &ctx) override;
};

class BaseTypeNode : public TypeNode {
private:
    std::string typeName;

public:
    BaseTypeNode(Token token, std::string &&typeName) :
            TypeNode(token), typeName(std::move(typeName)) { }

    ~BaseTypeNode() = default;

    const std::string &getTokenText() const {
        return this->typeName;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
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
            TypeNode(templateTypeNode->getToken()),
            templateTypeNode(templateTypeNode), elementTypeNodes() { }

    ~ReifiedTypeNode();

    void addElementTypeNode(TypeNode *typeNode);

    BaseTypeNode *getTemplate() const {
        return this->templateTypeNode;
    }

    const std::vector<TypeNode *> &getElementTypeNodes() const {
        return this->elementTypeNodes;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
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
            TypeNode({startPos, 0}),
            returnTypeNode(returnTypeNode), paramTypeNodes() { }

    ~FuncTypeNode();

    void addParamTypeNode(TypeNode *typeNode);

    const std::vector<TypeNode *> &getParamTypeNodes() const {
        return this->paramTypeNodes;
    }

    TypeNode *getReturnTypeNode() const {
        return this->returnTypeNode;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
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
            TypeNode(token), name(std::move(name)) { }

    ~DBusIfaceTypeNode() = default;

    const std::string &getTokenText() const {
        return this->name;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
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

    IntValueNode(Token token, IntKind kind, int value) :
            Node(token), kind(kind), tempValue(value), value() { }

public:
    static IntValueNode *newByte(Token token, unsigned char value) {
        return new IntValueNode(token, BYTE, value);
    }

    static IntValueNode *newInt16(Token token, short value) {
        return new IntValueNode(token, INT16, value);
    }

    static IntValueNode *newUint16(Token token, unsigned short value) {
        return new IntValueNode(token, UINT16, value);
    }

    static IntValueNode *newInt32(Token token, int value) {
        return new IntValueNode(token, INT32, value);
    }

    static IntValueNode *newUint32(Token token, unsigned int value) {
        return new IntValueNode(token, UINT32, value);
    }

    ~IntValueNode() = default;

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

    void setType(DSType &type) override;
    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

class LongValueNode : public Node {
private:
    long tempValue;
    bool unsignedValue;

    DSValue value;

    LongValueNode(Token token, long value, bool unsignedValue) :
            Node(token), tempValue(value), unsignedValue(unsignedValue), value() { }

public:
    static LongValueNode *newInt64(Token token, long value) {
        return new LongValueNode(token, value, false);
    }

    static LongValueNode *newUint64(Token token, unsigned long value) {
        return new LongValueNode(token, value, true);
    }

    ~LongValueNode() = default;

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

    void setType(DSType &type) override;
    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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

    void setType(DSType &type) override;
    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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
            Node({0, 0}), tempValue(std::move(value)), value() { }

    StringValueNode(Token token, std::string &&value) :
            Node(token), tempValue(std::move(value)), value() { }

    virtual ~StringValueNode() = default;

    /**
     * before type check, return empty pointer.
     */
    const DSValue &getValue() const {
        return this->value;
    }

    void setType(DSType &type) override;
    void dump(NodeDumper &dumper) const override;
    virtual void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

class ObjectPathNode : public StringValueNode {
public:
    ObjectPathNode(Token token, std::string &&value) :
            StringValueNode(token, std::move(value)) { }

    ~ObjectPathNode() = default;

    void accept(NodeVisitor &visitor) override;
};

class StringExprNode : public Node {
private:
    std::vector<Node *> nodes;

public:
    explicit StringExprNode(unsigned int startPos) : Node({startPos, 1}), nodes() { }

    ~StringExprNode();

    void addExprNode(Node *node);

    const std::vector<Node *> &getExprNodes() const {
        return this->nodes;
    }

    void setExprNode(unsigned int index, Node *exprNode) {
        this->nodes[index] = exprNode;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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
    explicit AssignableNode(Token token) :
            Node(token), index(0),
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

    virtual void dump(NodeDumper &dumper) const override;
};

class VarNode : public AssignableNode {
private:
    std::string varName;

public:
    VarNode(Token token, std::string &&varName) :
            AssignableNode(token), varName(std::move(varName)) { }

    ~VarNode() = default;

    const std::string &getVarName() const {
        return this->varName;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;

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
            AssignableNode(recvNode->getToken()),
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;

    /**
     * extract recvNode, and fieldName.
     * after extraction, call destructor.
     */
    static std::pair<Node *, std::string> split(AccessNode *accessNode);
};

class CastNode : public Node {
public:
    enum CastOp {
        NO_CAST,
        TO_VOID,
        NUM_CAST,
        TO_STRING,
        CHECK_CAST,
    };

    /**
     * number cast op
     */
    enum NumberCastOp : unsigned short {
        NOP        = 0,
        COPY_INT   = 1 << 0,
        TO_B       = 1 << 1,
        TO_U16     = 1 << 2,
        TO_I16     = 1 << 3,
        NEW_LONG   = 1 << 4,
        COPY_LONG  = 1 << 5,
        I_NEW_LONG = 1 << 6,
        NEW_INT    = 1 << 7,
        U32_TO_D   = 1 << 8,
        I32_TO_D   = 1 << 9,
        U64_TO_D   = 1 << 10,
        I64_TO_D   = 1 << 11,
        D_TO_U32   = 1 << 12,
        D_TO_I32   = 1 << 13,
        D_TO_U64   = 1 << 14,
        D_TO_I64   = 1 << 15,
    };

private:
    Node *exprNode;

    /**
     * may be tagged pointer
     */
    TypeNode *targetTypeNode;

    CastOp opKind;

    /**
     * additional op for number cast.
     */
    unsigned short numCastOp;

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

    unsigned short getNumberCastOp() const {
        return this->numCastOp;
    }

    /**
     * this node must be typed.
     * not allow void cast.
     */
    bool resolveCastOp(TypePool &pool);

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;

    /**
     * for implicit cast.
     * targetNode must be typed node.
     * type is after casted value type.
     */
    static CastNode *newTypedCastNode(TypePool &pool, Node *targetNode, DSType &type);
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;

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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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
            Node({startPos, 0}), op(op), exprNode(exprNode), methodCallNode(0) {
        this->updateToken(exprNode->getToken());
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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
            Node(leftNode->getToken()),
            leftNode(leftNode), rightNode(rightNode), op(op), methodCallNode(0) {
        this->updateToken(rightNode->getToken());
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

class TernaryNode : public Node {
private:
    Node *condNode;
    Node *leftNode;
    Node *rightNode;

public:
    TernaryNode(Node *condNode, Node *leftNode, Node *rightNode) :
            Node(condNode->getToken()), condNode(condNode),
            leftNode(leftNode), rightNode(rightNode) {
        this->updateToken(this->rightNode->getToken());
    }

    ~TernaryNode();

    Node *getCondNode() {
        return this->condNode;
    }

    Node *getLeftNode() {
        return this->leftNode;
    }

    Node *& refLeftNode() {
        return this->leftNode;
    }

    Node *getRightNode() {
        return this->rightNode;
    }

    Node *& refRightNode() {
        return this->rightNode;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

/**
 * for command argument
 */
class CmdArgNode : public Node {
private:
    std::vector<Node *> segmentNodes;

public:
    explicit CmdArgNode(Node *segmentNode) :
            Node(segmentNode->getToken()), segmentNodes() {
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;

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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;

    /**
     * if isLastSegment is true, segment nodes size is 1.
     */
    std::string expand(bool isLastSegment = true);

    EvalStatus eval(RuntimeContext &ctx) override;
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
            Node(nameNode->getToken()), nameNode(nameNode), argNodes() { }

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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

class PipedCmdNode : public Node {
private:
    std::vector<Node *> cmdNodes;

public:
    explicit PipedCmdNode(Node *node) :
            Node(node->getToken()), cmdNodes(1) {
        this->cmdNodes[0] = node;
    }

    ~PipedCmdNode();

    void addCmdNodes(Node *node);

    const std::vector<Node *> &getCmdNodes() const {
        return this->cmdNodes;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

/**
 * for command substitution (ex. "$(echo)", $(echo) )
 */
class SubstitutionNode : public Node {
private:
    Node *exprNode;

    /**
     * if true, this node is within string expr node
     */
    bool strExpr;

public:
    explicit SubstitutionNode(Node *exprNode) :
            Node(exprNode->getToken()), exprNode(exprNode), strExpr(false) { }

    ~SubstitutionNode();

    Node *getExprNode() const {
        return this->exprNode;
    }

    void setStrExpr(bool strExpr) {
        this->strExpr = strExpr;
    }

    bool isStrExpr() const {
        return this->strExpr;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

// statement definition

class AssertNode : public Node {
private:
    Node *condNode;

public:
    AssertNode(unsigned int startPos, Node *condNode) :
            Node({startPos, 0}), condNode(condNode) { }

    ~AssertNode();

    Node *getCondNode() const {
        return this->condNode;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

class BlockNode : public Node {
private:
    std::list<Node *> nodeList;

public:
    explicit BlockNode(unsigned int startPos) : Node({startPos, 1}), nodeList() { }

    ~BlockNode();

    void addNode(Node *node);

    void insertNodeToFirst(Node *node);

    const std::list<Node *> &getNodeList() const {
        return this->nodeList;
    }

    std::list<Node *> &refNodeList() {
        return this->nodeList;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

/**
 * base class for break, continue, return, throw node
 */
class BlockEndNode : public Node {
public:
    explicit BlockEndNode(Token token) : Node(token) { }

    virtual ~BlockEndNode() = default;
};

class BreakNode : public BlockEndNode {
public:
    explicit BreakNode(Token token) : BlockEndNode(token) { }

    ~BreakNode() = default;

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

class ContinueNode : public BlockEndNode {
public:
    explicit ContinueNode(Token token) : BlockEndNode(token) { }

    ~ContinueNode() = default;

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

class ExportEnvNode : public Node {
private:
    std::string envName;
    Node *exprNode;
    bool global;
    unsigned int varIndex;

public:
    ExportEnvNode(unsigned int startPos, std::string &&envName, Node *exprNode) :
            Node({startPos, 0}), envName(std::move(envName)), exprNode(exprNode),
            global(false), varIndex(0) {
        this->updateToken(exprNode->getToken());
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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
            Node({startPos, 0}), envName(std::move(envName)),
            defaultValueNode(nullptr), global(false), varIndex(0) { }

    ~ImportEnvNode();

    const std::string &getEnvName() const {
        return this->envName;
    }

    void setDefaultValueNode(Node *node) {
        this->defaultValueNode = node;
        this->updateToken(this->defaultValueNode->getToken());
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

class TypeAliasNode : public Node {
private:
    std::string alias;
    TypeNode *targetTypeNode;

public:
    TypeAliasNode(unsigned int startPos, std::string &&alias, TypeNode *targetTypeNode) :
            Node({startPos, 0}), alias(std::move(alias)), targetTypeNode(targetTypeNode) {
        this->updateToken(targetTypeNode->getToken());
    }

    ~TypeAliasNode();

    const std::string &getAlias() const {
        return this->alias;
    }

    TypeNode *getTargetTypeNode() const {
        return this->targetTypeNode;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

class WhileNode : public Node {
private:
    Node *condNode;
    BlockNode *blockNode;

public:
    WhileNode(unsigned int startPos, Node *condNode, BlockNode *blockNode) :
            Node({startPos, 0}), condNode(condNode), blockNode(blockNode) {
        this->updateToken(blockNode->getToken());
    }

    ~WhileNode();

    Node *getCondNode() const {
        return this->condNode;
    }

    BlockNode *getBlockNode() const {
        return this->blockNode;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

class DoWhileNode : public Node {
private:
    BlockNode *blockNode;
    Node *condNode;

public:
    DoWhileNode(unsigned int startPos, BlockNode *blockNode, Node *condNode) :
            Node({startPos, 0}), blockNode(blockNode), condNode(condNode) { }

    ~DoWhileNode();

    BlockNode *getBlockNode() const {
        return this->blockNode;
    }

    Node *getCondNode() const {
        return this->condNode;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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
        this->updateToken(elseNode->getToken());
    }

    /*
     * return EmptyBlockNode, if elseNode is null.
     */
    BlockNode *getElseNode();

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

class ReturnNode : public BlockEndNode {
private:
    Node *exprNode;

public:
    ReturnNode(unsigned int startPos, Node *exprNode) :
            BlockEndNode({startPos, 0}), exprNode(exprNode) {
        this->updateToken(exprNode->getToken());
    }

    explicit ReturnNode(Token token);

    ~ReturnNode();

    Node *getExprNode() const {
        return this->exprNode;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

class ThrowNode : public BlockEndNode {
private:
    Node *exprNode;

public:
    ThrowNode(unsigned int startPos, Node *exprNode) :
            BlockEndNode({startPos, 0}), exprNode(exprNode) {
        this->updateToken(exprNode->getToken());
    }

    ~ThrowNode();

    Node *getExprNode() const {
        return this->exprNode;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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
            Node({startPos, 0}), exceptionName(std::move(exceptionName)),
            typeNode(typeNode), varIndex(0), blockNode(blockNode) {
        this->updateToken(blockNode->getToken());
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

class TryNode : public Node {
private:
    BlockNode *blockNode;

    /**
     * may be empty
     */
    std::vector<CatchNode *> catchNodes;

    /**
     * may be null
     */
    BlockNode *finallyNode;

public:
    TryNode(unsigned int startPos, BlockNode *blockNode) :
            Node({startPos, 0}), blockNode(blockNode), catchNodes(), finallyNode() {
        this->updateToken(blockNode->getToken());
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

    /**
     * if has no finally block, return null
     */
    BlockNode *getFinallyNode() {
        return this->finallyNode;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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
            Node(leftNode->getToken()),
            leftNode(leftNode), rightNode(rightNode), attributeSet(0) {
        if(selfAssign) {
            setFlag(this->attributeSet, SELF_ASSIGN);
        }
        this->updateToken(rightNode->getToken());
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;

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
     * before type checking, rightNode is BinaryOpNode.
     * after type checking, rightNode may be CastNode.
     * if rightNode is BinaryOpNode, left node is dummy node.
     */
    Node *rightNode;

public:
    ElementSelfAssignNode(MethodCallNode *leftNode, BinaryOpNode *binaryNode);
    ~ElementSelfAssignNode();

    Node *getRecvNode() const {
        return this->recvNode;
    }

    Node *getIndexNode() const {
        return this->indexNode;
    }

    /**
     * may be BinaryOpNode or CastNode
     */
    Node *getRightNode() const {
        return this->rightNode;
    }

    Node * &refRightNode() {
        return this->rightNode;
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

class CallableNode : public Node {
protected:
    SourceInfoPtr srcInfoPtr;

public:
    CallableNode(unsigned int startPos, const SourceInfoPtr &srcInfoPtr) :
            Node({startPos, 0}), srcInfoPtr(srcInfoPtr) { }

    CallableNode() : Node({0, 0}), srcInfoPtr(nullptr) { }

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
        this->updateToken(typeToken->getToken());
    }

    TypeNode *getReturnTypeToken();

    void setBlockNode(BlockNode *blockNode) {
        this->blockNode = blockNode;
        this->updateToken(blockNode->getToken());
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

class InterfaceNode : public Node {
private:
    std::string interfaceName;

    std::vector<FunctionNode *> methodDeclNodes;
    std::vector<VarDeclNode *> fieldDeclNodes;
    std::vector<TypeNode *> fieldTypeNodes;

public:
    InterfaceNode(unsigned int startPos, std::string &&interfaceName) :
            Node({startPos, 0}), interfaceName(std::move(interfaceName)), methodDeclNodes(),
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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
        this->updateToken(blockNode->getToken());
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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
            Node({0, 0}), varName(std::string(name)), varIndex(0), value(value) { }

    BindVarNode(const char *name, DSValue &&value) :
            Node({0, 0}), varName(std::string(name)), varIndex(0), value(std::move(value)) { }

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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

class EmptyNode : public Node {
public:
    EmptyNode() : Node({0, 0}) { }
    explicit EmptyNode(Token token) : Node(token) { }
    ~EmptyNode() = default;

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

class DummyNode : public Node {
public:
    DummyNode() : Node({0, 0}) { }
    ~DummyNode() = default;

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
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

    std::list<Node *> &refNodeList() {
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

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
    EvalStatus eval(RuntimeContext &ctx) override;
};

// helper function for node creation

const char *resolveUnaryOpName(TokenKind op);

const char *resolveBinaryOpName(TokenKind op);

TokenKind resolveAssignOp(TokenKind op);

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
    virtual void visitCondOpNode(CondOpNode &node) = 0;
    virtual void visitTernaryNode(TernaryNode &node) = 0;
    virtual void visitCmdNode(CmdNode &node) = 0;
    virtual void visitCmdArgNode(CmdArgNode &node) = 0;
    virtual void visitRedirNode(RedirNode &node) = 0;
    virtual void visitTildeNode(TildeNode &node) = 0;
    virtual void visitPipedCmdNode(PipedCmdNode &node) = 0;
    virtual void visitSubstitutionNode(SubstitutionNode &node) = 0;
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

    virtual void visitBaseTypeNode(BaseTypeNode &node) override { this->visitDefault(node); }
    virtual void visitReifiedTypeNode(ReifiedTypeNode &node) override { this->visitDefault(node); }
    virtual void visitFuncTypeNode(FuncTypeNode &node) override { this->visitDefault(node); }
    virtual void visitDBusIfaceTypeNode(DBusIfaceTypeNode &node) override { this->visitDefault(node); }
    virtual void visitReturnTypeNode(ReturnTypeNode &node) override { this->visitDefault(node); }
    virtual void visitTypeOfNode(TypeOfNode &node) override { this->visitDefault(node); }
    virtual void visitIntValueNode(IntValueNode &node) override { this->visitDefault(node); }
    virtual void visitLongValueNode(LongValueNode &node) override { this->visitDefault(node); }
    virtual void visitFloatValueNode(FloatValueNode &node) override { this->visitDefault(node); }
    virtual void visitStringValueNode(StringValueNode &node) override { this->visitDefault(node); }
    virtual void visitObjectPathNode(ObjectPathNode &node) override { this->visitDefault(node); }
    virtual void visitStringExprNode(StringExprNode &node) override { this->visitDefault(node); }
    virtual void visitArrayNode(ArrayNode &node) override { this->visitDefault(node); }
    virtual void visitMapNode(MapNode &node) override { this->visitDefault(node); }
    virtual void visitTupleNode(TupleNode &node) override { this->visitDefault(node); }
    virtual void visitVarNode(VarNode &node) override { this->visitDefault(node); }
    virtual void visitAccessNode(AccessNode &node) override { this->visitDefault(node); }
    virtual void visitCastNode(CastNode &node) override { this->visitDefault(node); }
    virtual void visitInstanceOfNode(InstanceOfNode &node) override { this->visitDefault(node); }
    virtual void visitUnaryOpNode(UnaryOpNode &node) override { this->visitDefault(node); }
    virtual void visitBinaryOpNode(BinaryOpNode &node) override { this->visitDefault(node); }
    virtual void visitApplyNode(ApplyNode &node) override { this->visitDefault(node); }
    virtual void visitMethodCallNode(MethodCallNode &node) override { this->visitDefault(node); }
    virtual void visitNewNode(NewNode &node) override { this->visitDefault(node); }
    virtual void visitCondOpNode(CondOpNode &node) override { this->visitDefault(node); }
    virtual void visitTernaryNode(TernaryNode &node) override { this->visitDefault(node); }
    virtual void visitCmdNode(CmdNode &node) override { this->visitDefault(node); }
    virtual void visitCmdArgNode(CmdArgNode &node) override { this->visitDefault(node); }
    virtual void visitRedirNode(RedirNode &node) override { this->visitDefault(node); }
    virtual void visitTildeNode(TildeNode &node) override { this->visitDefault(node); }
    virtual void visitPipedCmdNode(PipedCmdNode &node) override { this->visitDefault(node); }
    virtual void visitSubstitutionNode(SubstitutionNode &node) override { this->visitDefault(node); }
    virtual void visitAssertNode(AssertNode &node) override { this->visitDefault(node); }
    virtual void visitBlockNode(BlockNode &node) override { this->visitDefault(node); }
    virtual void visitBreakNode(BreakNode &node) override { this->visitDefault(node); }
    virtual void visitContinueNode(ContinueNode &node) override { this->visitDefault(node); }
    virtual void visitExportEnvNode(ExportEnvNode &node) override { this->visitDefault(node); }
    virtual void visitImportEnvNode(ImportEnvNode &node) override { this->visitDefault(node); }
    virtual void visitTypeAliasNode(TypeAliasNode &node) override { this->visitDefault(node); }
    virtual void visitForNode(ForNode &node) override { this->visitDefault(node); }
    virtual void visitWhileNode(WhileNode &node) override { this->visitDefault(node); }
    virtual void visitDoWhileNode(DoWhileNode &node) override { this->visitDefault(node); }
    virtual void visitIfNode(IfNode &node) override { this->visitDefault(node); }
    virtual void visitReturnNode(ReturnNode &node) override { this->visitDefault(node); }
    virtual void visitThrowNode(ThrowNode &node) override { this->visitDefault(node); }
    virtual void visitCatchNode(CatchNode &node) override { this->visitDefault(node); }
    virtual void visitTryNode(TryNode &node) override { this->visitDefault(node); }
    virtual void visitVarDeclNode(VarDeclNode &node) override { this->visitDefault(node); }
    virtual void visitAssignNode(AssignNode &node) override { this->visitDefault(node); }
    virtual void visitElementSelfAssignNode(ElementSelfAssignNode &node) override { this->visitDefault(node); }
    virtual void visitFunctionNode(FunctionNode &node) override { this->visitDefault(node); }
    virtual void visitInterfaceNode(InterfaceNode &node) override { this->visitDefault(node); }
    virtual void visitUserDefinedCmdNode(UserDefinedCmdNode &node) override { this->visitDefault(node); }
    virtual void visitBindVarNode(BindVarNode &node) override { this->visitDefault(node); }
    virtual void visitEmptyNode(EmptyNode &node) override { this->visitDefault(node); }
    virtual void visitDummyNode(DummyNode &node) override { this->visitDefault(node); }
    virtual void visitRootNode(RootNode &node) override { this->visitDefault(node); }
};

} // namespace ast
} // namespace ydsh

#endif //YDSH_AST_NODE_H
