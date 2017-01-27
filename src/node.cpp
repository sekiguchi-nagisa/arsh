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

#include <pwd.h>

#include "symbol.h"
#include "object.h"
#include "node.h"
#include "node_dumper.h"
#include "handle.h"
#include "proc.h"


namespace ydsh {

// ##########################
// ##     BaseTypeNode     ##
// ##########################

void BaseTypeNode::dump(NodeDumper &dumper) const {
    DUMP(typeName);
}

void BaseTypeNode::accept(NodeVisitor &visitor) {
    visitor.visitBaseTypeNode(*this);
}


// #############################
// ##     ReifiedTypeNode     ##
// #############################

ReifiedTypeNode::~ReifiedTypeNode() {
    delete this->templateTypeNode;
    for(TypeNode *t : this->elementTypeNodes) {
        delete t;
    }
}

void ReifiedTypeNode::addElementTypeNode(TypeNode *typeNode) {
    this->elementTypeNodes.push_back(typeNode);
}

void ReifiedTypeNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(templateTypeNode);
    DUMP(elementTypeNodes);
}

void ReifiedTypeNode::accept(NodeVisitor &visitor) {
    visitor.visitReifiedTypeNode(*this);
}


// ##########################
// ##     FuncTypeNode     ##
// ##########################

FuncTypeNode::~FuncTypeNode() {
    delete this->returnTypeNode;

    for(TypeNode *t : this->paramTypeNodes) {
        delete t;
    }
}

void FuncTypeNode::addParamTypeNode(TypeNode *typeNode) {
    this->paramTypeNodes.push_back(typeNode);
}

void FuncTypeNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(returnTypeNode);
    DUMP(paramTypeNodes);
}

void FuncTypeNode::accept(NodeVisitor &visitor) {
    visitor.visitFuncTypeNode(*this);
}

// ###############################
// ##     DBusIfaceTypeNode     ##
// ###############################

void DBusIfaceTypeNode::dump(NodeDumper &dumper) const {
    DUMP(name);
}

void DBusIfaceTypeNode::accept(NodeVisitor &visitor) {
    visitor.visitDBusIfaceTypeNode(*this);
}

// ############################
// ##     ReturnTypeNode     ##
// ############################

ReturnTypeNode::ReturnTypeNode(TypeNode *typeNode) :
        TypeNode(typeNode->getToken()), typeNodes() {
    this->addTypeNode(typeNode);
}

ReturnTypeNode::~ReturnTypeNode() {
    for(auto t : this->typeNodes) {
        delete t;
    }
}

void ReturnTypeNode::addTypeNode(TypeNode *typeNode) {
    this->typeNodes.push_back(typeNode);
    this->updateToken(typeNode->getToken());
}

void ReturnTypeNode::dump(NodeDumper &dumper) const {
    DUMP(typeNodes);
}

void ReturnTypeNode::accept(NodeVisitor &visitor) {
    visitor.visitReturnTypeNode(*this);
}

// ########################
// ##     TypeOfNode     ##
// ########################

TypeOfNode::TypeOfNode(unsigned int startPos, Node *exprNode) :
        TypeNode({startPos, 0}), exprNode(exprNode) {
    this->updateToken(exprNode->getToken());
}

TypeOfNode::~TypeOfNode() {
    delete this->exprNode;
}

void TypeOfNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(exprNode);
}

void TypeOfNode::accept(NodeVisitor &visitor) {
    visitor.visitTypeOfNode(*this);
}


TypeNode *newAnyTypeNode() {
    return new BaseTypeNode({0, 0}, std::string("Any"));
}

TypeNode *newVoidTypeNode() {
    return new BaseTypeNode({0, 0}, std::string("Void"));
}


// ##########################
// ##     IntValueNode     ##
// ##########################

void IntValueNode::dump(NodeDumper &dumper) const {
    DUMP_PRIM(value);
}

void IntValueNode::accept(NodeVisitor &visitor) {
    visitor.visitIntValueNode(*this);
}

// ###########################
// ##     LongValueNode     ##
// ###########################

void LongValueNode::dump(NodeDumper &dumper) const {
    DUMP_PRIM(value);
    DUMP_PRIM(unsignedValue);
}

void LongValueNode::accept(NodeVisitor &visitor) {
    visitor.visitLongValueNode(*this);
}


// ############################
// ##     FloatValueNode     ##
// ############################

void FloatValueNode::dump(NodeDumper &dumper) const {
    DUMP_PRIM(value);
}

void FloatValueNode::accept(NodeVisitor &visitor) {
    visitor.visitFloatValueNode(*this);
}


// ############################
// ##     StringValueNode    ##
// ############################

void StringValueNode::dump(NodeDumper &dumper) const {
    DUMP(value);
}

void StringValueNode::accept(NodeVisitor &visitor) {
    visitor.visitStringValueNode(*this);
}


// ############################
// ##     ObjectPathNode     ##
// ############################

void ObjectPathNode::accept(NodeVisitor &visitor) {
    visitor.visitObjectPathNode(*this);
}

// ############################
// ##     StringExprNode     ##
// ############################

StringExprNode::~StringExprNode() {
    for(Node *e : this->nodes) {
        delete e;
    }
}

void StringExprNode::addExprNode(Node *node) {
    if(dynamic_cast<StringExprNode *>(node) != nullptr) {
        StringExprNode *exprNode = static_cast<StringExprNode *>(node);
        for(auto &e : exprNode->nodes) {
            this->nodes.push_back(e);
        }
        exprNode->nodes.clear();
        delete node;
    } else if(dynamic_cast<StringValueNode *>(node) != nullptr &&
            static_cast<StringValueNode *>(node)->getValue().empty()) { // ignore empty string value node
        /**
         * node must not be empty string value except for calling BinaryOpNode::toStringExpr()
         */
        delete node;
    } else {
        this->nodes.push_back(node);
    }
}

void StringExprNode::dump(NodeDumper &dumper) const {
    DUMP(nodes);
}

void StringExprNode::accept(NodeVisitor &visitor) {
    visitor.visitStringExprNode(*this);
}

// #######################
// ##     ArrayNode     ##
// #######################

ArrayNode::ArrayNode(unsigned int startPos, Node *node) :
        Node({startPos, 0}), nodes() {
    this->nodes.push_back(node);
}

ArrayNode::~ArrayNode() {
    for(Node *e : this->nodes) {
        delete e;
    }
}

void ArrayNode::addExprNode(Node *node) {
    this->nodes.push_back(node);
}

void ArrayNode::dump(NodeDumper &dumper) const {
    DUMP(nodes);
}

void ArrayNode::accept(NodeVisitor &visitor) {
    visitor.visitArrayNode(*this);
}

// #####################
// ##     MapNode     ##
// #####################

MapNode::MapNode(unsigned int startPos, Node *keyNode, Node *valueNode) :
        Node({startPos, 0}), keyNodes(), valueNodes() {
    this->keyNodes.push_back(keyNode);
    this->valueNodes.push_back(valueNode);
}

MapNode::~MapNode() {
    for(Node *e : this->keyNodes) {
        delete e;
    }

    for(Node *e : this->valueNodes) {
        delete e;
    }
}

void MapNode::addEntry(Node *keyNode, Node *valueNode) {
    this->keyNodes.push_back(keyNode);
    this->valueNodes.push_back(valueNode);
}

void MapNode::dump(NodeDumper &dumper) const {
    DUMP(keyNodes);
    DUMP(valueNodes);
}

void MapNode::accept(NodeVisitor &visitor) {
    visitor.visitMapNode(*this);
}

// #######################
// ##     TupleNode     ##
// #######################

TupleNode::TupleNode(unsigned int startPos, Node *leftNode, Node *rightNode) :
        Node({startPos, 0}), nodes(2) {
    this->nodes[0] = leftNode;
    this->nodes[1] = rightNode;
}

TupleNode::~TupleNode() {
    for(Node *node : this->nodes) {
        delete node;
    }
}

void TupleNode::addNode(Node *node) {
    this->nodes.push_back(node);
}

void TupleNode::dump(NodeDumper &dumper) const {
    DUMP(nodes);
}

void TupleNode::accept(NodeVisitor &visitor) {
    visitor.visitTupleNode(*this);
}

// ############################
// ##     AssignableNode     ##
// ############################

void AssignableNode::dump(NodeDumper &dumper) const {
    DUMP_PRIM(index);
    dumper.dump("attribute", this->attribute.str().c_str());
}

// #####################
// ##     VarNode     ##
// #####################

void VarNode::dump(NodeDumper &dumper) const {
    DUMP(varName);
    AssignableNode::dump(dumper);
}

void VarNode::accept(NodeVisitor &visitor) {
    visitor.visitVarNode(*this);
}

// ########################
// ##     AccessNode     ##
// ########################

AccessNode::~AccessNode() {
    delete this->recvNode;
}

void AccessNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(recvNode);
    DUMP(fieldName);
    AssignableNode::dump(dumper);

#define EACH_ENUM(OP) \
    OP(NOP) \
    OP(DUP_RECV)

    DUMP_ENUM(additionalOp, EACH_ENUM);
#undef EACH_ENUM
}

void AccessNode::accept(NodeVisitor &visitor) {
    visitor.visitAccessNode(*this);
}

// ######################
// ##     CastNode     ##
// ######################

CastNode::CastNode(Node *exprNode, TypeNode *type, bool dupTypeToken) :
        Node(exprNode->getToken()), exprNode(exprNode), targetTypeNode(nullptr),
        opKind(NO_CAST), numCastOp(NOP) {
    static const unsigned long tag = (unsigned long) 1L << 63;

    if(dupTypeToken) {
        TypeNode *tok = reinterpret_cast<TypeNode *>(tag | (unsigned long) type);
        this->targetTypeNode = tok;
    } else {
        this->targetTypeNode = type;
    }

    if(this->getTargetTypeNode() != nullptr) {
        this->updateToken(this->getTargetTypeNode()->getToken());
    }
}

CastNode::~CastNode() {
    delete this->exprNode;

    if((long) this->targetTypeNode >= 0) {
        delete this->targetTypeNode;
    }
}

TypeNode *CastNode::getTargetTypeNode() const {
    static const unsigned long mask = ~(1L << 63);
    if((long) this->targetTypeNode < 0) {
        TypeNode *tok = reinterpret_cast<TypeNode *>(mask & (unsigned long) this->targetTypeNode);
        return tok;
    }
    return this->targetTypeNode;
}

bool CastNode::resolveCastOp(TypePool &pool) {
    auto &exprType = this->exprNode->getType();
    auto &targetType = this->getType();

    /**
     * nop
     */
    if(targetType.isSameOrBaseTypeOf(exprType)) {
        return true;
    }

    /**
     * number cast
     */
    int beforeIndex = pool.getNumTypeIndex(exprType);
    int afterIndex = pool.getNumTypeIndex(targetType);
    if(beforeIndex > -1 && afterIndex > -1) {
        static const unsigned short table[8][8] = {
                {NOP,           COPY_INT,        COPY_INT,        COPY_INT, COPY_INT, NEW_LONG,   NEW_LONG,   U32_TO_D},
                {TO_B,          NOP,             TO_U16,          COPY_INT, COPY_INT, I_NEW_LONG, I_NEW_LONG, I32_TO_D},
                {TO_B,          TO_I16,          NOP,             COPY_INT, COPY_INT, NEW_LONG,   NEW_LONG,   U32_TO_D},
                {TO_B,          TO_I16,          TO_U16,          NOP,      COPY_INT, I_NEW_LONG, I_NEW_LONG, I32_TO_D},
                {TO_B,          TO_I16,          TO_U16,          COPY_INT, NOP,      NEW_LONG,   NEW_LONG,   U32_TO_D},
                {NEW_INT|TO_B,  NEW_INT|TO_I16,  NEW_INT|TO_U16,  NEW_INT,  NEW_INT,  NOP,        COPY_LONG,  I64_TO_D},
                {NEW_INT|TO_B,  NEW_INT|TO_I16,  NEW_INT|TO_U16,  NEW_INT,  NEW_INT,  COPY_LONG,  NOP,        U64_TO_D},
                {D_TO_U32|TO_B, D_TO_I32|TO_I16, D_TO_U32|TO_U16, D_TO_I32, D_TO_U32, D_TO_I64,   D_TO_U64,   NOP},
        };

        assert(beforeIndex >= 0 && beforeIndex <= 8);
        assert(afterIndex >= 0 && afterIndex <= 8);
        this->setOpKind(CastNode::NUM_CAST);
        this->numCastOp = table[beforeIndex][afterIndex];
        return true;
    }

    /**
     * to string
     */
    if(targetType == pool.getStringType()) {
        this->setOpKind(CastNode::TO_STRING);
        return true;
    }

    /**
     * check cast
     */
    if(!targetType.isBottomType() && exprType.isSameOrBaseTypeOf(targetType)) {
        this->setOpKind(CastNode::CHECK_CAST);
        return true;
    }

    return false;
}

void CastNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(exprNode);
    TypeNode *targetTypeToken = this->getTargetTypeNode();
    DUMP_PTR(targetTypeToken);

#define EACH_ENUM(OP) \
    OP(NO_CAST) \
    OP(TO_VOID) \
    OP(NUM_CAST) \
    OP(TO_STRING) \
    OP(CHECK_CAST)

    DUMP_ENUM(opKind, EACH_ENUM);
#undef EACH_ENUM

#define EACH_FLAG(OP) \
    OP(NOP) \
    OP(COPY_INT) \
    OP(TO_B) \
    OP(TO_U16) \
    OP(TO_I16) \
    OP(NEW_LONG) \
    OP(COPY_LONG) \
    OP(I_NEW_LONG) \
    OP(NEW_INT) \
    OP(U32_TO_D) \
    OP(I32_TO_D) \
    OP(U64_TO_D) \
    OP(I64_TO_D) \
    OP(D_TO_U32) \
    OP(D_TO_I32) \
    OP(D_TO_U64) \
    OP(D_TO_I64)

    DUMP_BITSET(numCastOp, EACH_FLAG);
#undef EACH_FLAG
}

void CastNode::accept(NodeVisitor &visitor) {
    visitor.visitCastNode(*this);
}

CastNode *CastNode::newTypedCastNode(TypePool &pool, Node *targetNode, DSType &type) {
    assert(!targetNode->isUntyped());
    CastNode *castNode = new CastNode(targetNode, nullptr);
    castNode->setType(type);
    if(type == pool.getVoidType()) {
        castNode->setOpKind(CastNode::TO_VOID);
    } else {
        bool s = castNode->resolveCastOp(pool);
        (void) s;   // do nothing
        assert(s);
    }
    return castNode;
}


// ############################
// ##     InstanceOfNode     ##
// ############################

InstanceOfNode::InstanceOfNode(Node *targetNode, TypeNode *typeNode) :
    Node(targetNode->getToken()), targetNode(targetNode),
    targetTypeNode(typeNode), opKind(ALWAYS_FALSE) {
    this->updateToken(typeNode->getToken());
}

InstanceOfNode::~InstanceOfNode() {
    delete this->targetNode;
    delete this->targetTypeNode;
}

void InstanceOfNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(targetNode);
    DUMP_PTR(targetTypeNode);

#define EACH_ENUM(OP) \
    OP(ALWAYS_FALSE) \
    OP(ALWAYS_TRUE) \
    OP(INSTANCEOF)

    DUMP_ENUM(opKind, EACH_ENUM);
#undef EACH_ENUM
}

void InstanceOfNode::accept(NodeVisitor &visitor) {
    visitor.visitInstanceOfNode(*this);
}

// #######################
// ##     PrintNode     ##
// #######################

PrintNode::PrintNode(Node *exprNode) : Node(exprNode->getToken()), exprNode(exprNode) { }

PrintNode::~PrintNode() {
    delete this->exprNode;
}

void PrintNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(exprNode);
}

void PrintNode::accept(NodeVisitor &visitor) {
    visitor.visitPrintNode(*this);
}

PrintNode *PrintNode::newTypedPrintNode(TypePool &pool, Node *exprNode) {
    assert(!exprNode->isUntyped());
    PrintNode *node = new PrintNode(exprNode);
    node->setType(pool.getVoidType());
    return node;
}


// #######################
// ##     ApplyNode     ##
// #######################

ApplyNode::ApplyNode(Node *exprNode, std::vector<Node *> &&argNodes) :
        Node(exprNode->getToken()),
        exprNode(exprNode), argNodes(std::move(argNodes)) {
    if(!this->argNodes.empty()) {
        this->updateToken(this->argNodes.back()->getToken());
    }
}

ApplyNode::~ApplyNode() {
    delete this->exprNode;

    for(Node *n : this->argNodes) {
        delete n;
    }
}

void ApplyNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(exprNode);
    DUMP(argNodes);
}

void ApplyNode::accept(NodeVisitor &visitor) {
    visitor.visitApplyNode(*this);
}

// ############################
// ##     MethodCallNode     ##
// ############################

MethodCallNode::MethodCallNode(Node *recvNode, std::string &&methodName, std::vector<Node *> &&argNodes) :
        Node(recvNode->getToken()),
        recvNode(recvNode), methodName(std::move(methodName)),
        argNodes(std::move(argNodes)), handle(), attributeSet() {
    if(!this->argNodes.empty()) {
        this->updateToken(this->argNodes.back()->getToken());
    }
}

MethodCallNode::~MethodCallNode() {
    delete this->recvNode;

    for(Node *n : this->argNodes) {
        delete n;
    }
}

void MethodCallNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(recvNode);
    DUMP(methodName);
    unsigned int methodIndex =
            this->handle != nullptr ? this->handle->getMethodIndex() : 0;
    DUMP_PRIM(methodIndex);
    DUMP(argNodes);

#define EACH_FLAG(OP) \
    OP(INDEX) \
    OP(ICALL)

    DUMP_BITSET(attributeSet, EACH_FLAG);

#undef EACH_FLAG
}

void MethodCallNode::accept(NodeVisitor &visitor) {
    visitor.visitMethodCallNode(*this);
}

// #####################
// ##     NewNode     ##
// #####################

NewNode::NewNode(unsigned int startPos, TypeNode *targetTypeNode, std::vector<Node *> &&argNodes) :
        Node({startPos, 0}), targetTypeNode(targetTypeNode), argNodes(std::move(argNodes)) {
    if(!this->argNodes.empty()) {
        this->updateToken(this->argNodes.back()->getToken());
    }
}

NewNode::~NewNode() {
    delete this->targetTypeNode;

    for(Node *n : this->argNodes) {
        delete n;
    }
}

void NewNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(targetTypeNode);
    DUMP(argNodes);
}

void NewNode::accept(NodeVisitor &visitor) {
    visitor.visitNewNode(*this);
}

// #########################
// ##     UnaryOpNode     ##
// #########################

UnaryOpNode::~UnaryOpNode() {
    delete this->exprNode;
    delete this->methodCallNode;
}

MethodCallNode *UnaryOpNode::createApplyNode() {
    this->methodCallNode = new MethodCallNode(this->exprNode, resolveUnaryOpName(this->op));

    // assign null to prevent double free
    this->exprNode = nullptr;

    return this->methodCallNode;
}

void UnaryOpNode::dump(NodeDumper &dumper) const {
    dumper.dump(NAME(op), TO_NAME(op));
    DUMP_PTR(exprNode);
    DUMP_PTR(methodCallNode);
}

void UnaryOpNode::accept(NodeVisitor &visitor) {
    visitor.visitUnaryOpNode(*this);
}


// ##########################
// ##     BinaryOpNode     ##
// ##########################

BinaryOpNode::~BinaryOpNode() {
    delete this->leftNode;
    delete this->rightNode;
    delete this->optNode;
}

void BinaryOpNode::toMethodCall(BinaryOpNode &node) {
    auto *methodCallNode = new MethodCallNode(node.leftNode, resolveBinaryOpName(node.op));
    methodCallNode->refArgNodes().push_back(node.rightNode);

    // assign null to prevent double free
    node.leftNode = nullptr;
    node.rightNode = nullptr;

    node.optNode = methodCallNode;
}

void BinaryOpNode::toStringExpr(TypePool &pool, BinaryOpNode &node) {
    int needCast = 0;
    if(node.leftNode->getType() != pool.getStringType()) {
        needCast--;
    }
    if(node.rightNode->getType() != pool.getStringType()) {
        needCast++;
    }

    // perform string cast
    if(needCast == -1) {
        node.leftNode = CastNode::newTypedCastNode(pool, node.leftNode, pool.getStringType());
    } else if(needCast == 1) {
        node.rightNode = CastNode::newTypedCastNode(pool, node.rightNode, pool.getStringType());
    }

    auto *exprNode = new StringExprNode(node.leftNode->getPos());
    exprNode->addExprNode(node.leftNode);
    exprNode->addExprNode(node.rightNode);

    // assign null to prevent double free
    node.leftNode = nullptr;
    node.rightNode = nullptr;

    node.optNode = exprNode;
}

void BinaryOpNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(leftNode);
    DUMP_PTR(rightNode);
    dumper.dump(NAME(op), TO_NAME(op));
    DUMP_PTR(optNode);
}

void BinaryOpNode::accept(NodeVisitor &visitor) {
    visitor.visitBinaryOpNode(*this);
}

// ########################
// ##     CondOpNode     ##
// ########################

CondOpNode::CondOpNode(Node *leftNode, Node *rightNode, bool isAndOp) :
        Node(leftNode->getToken()), leftNode(leftNode), rightNode(rightNode), andOp(isAndOp) {
    this->updateToken(rightNode->getToken());
}

CondOpNode::~CondOpNode() {
    delete this->leftNode;
    delete this->rightNode;
}

void CondOpNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(leftNode);
    DUMP_PTR(rightNode);
    DUMP_PRIM(andOp);
}

void CondOpNode::accept(NodeVisitor &visitor) {
    visitor.visitCondOpNode(*this);
}

// ########################
// ##     CmdArgNode     ##
// ########################

CmdArgNode::~CmdArgNode() {
    for(Node *e : this->segmentNodes) {
        delete e;
    }
}

void CmdArgNode::addSegmentNode(Node *node) {
    this->segmentNodes.push_back(node);
    this->updateToken(node->getToken());
}

void CmdArgNode::dump(NodeDumper &dumper) const {
    DUMP(segmentNodes);
}

void CmdArgNode::accept(NodeVisitor &visitor) {
    visitor.visitCmdArgNode(*this);
}

bool CmdArgNode::isIgnorableEmptyString() {
    return this->segmentNodes.size() > 1 ||
            (dynamic_cast<StringValueNode *>(this->segmentNodes.back()) == nullptr &&
                    dynamic_cast<StringExprNode *>(this->segmentNodes.back()) == nullptr);
}

// #######################
// ##     RedirNode     ##
// #######################

RedirNode::RedirNode(TokenKind kind, CmdArgNode *node) :
        Node(node->getToken()), op(RedirectOP::DUMMY), targetNode(node) {
    switch(kind) {
#define GEN_CASE(ENUM, STR) case REDIR_##ENUM : this->op = RedirectOP::ENUM; break;
    EACH_RedirectOP(GEN_CASE)
#undef GEN_CASE
    default:
        fatal("unsupported redirect op: %s\n", TO_NAME(kind));
        break;
    }
}

RedirNode::~RedirNode() {
    delete this->targetNode;
}

void RedirNode::dump(NodeDumper &dumper) const {
    const char *redirOpStr[] = {
#define GEN_STR(ENUM, STR) #ENUM,
            EACH_RedirectOP(GEN_STR)
#undef GEN_STR
    };

    dumper.dump(NAME(op), redirOpStr[this->op]);
    DUMP_PTR(targetNode);
}

void RedirNode::accept(NodeVisitor &visitor) {
    visitor.visitRedirNode(*this);
}

// #######################
// ##     TildeNode     ##
// #######################

void TildeNode::dump(NodeDumper &dumper) const {
    DUMP(value);
}

void TildeNode::accept(NodeVisitor &visitor) {
    visitor.visitTildeNode(*this);
}

// #####################
// ##     CmdNode     ##
// #####################

CmdNode::~CmdNode() {
    delete this->nameNode;

    for(auto *e : this->argNodes) {
        delete e;
    }
}

void CmdNode::addArgNode(CmdArgNode *node) {
    this->argNodes.push_back(node);
    this->updateToken(node->getToken());
}

void CmdNode::addRedirOption(TokenKind kind, CmdArgNode *node) {
    this->argNodes.push_back(new RedirNode(kind, node));
    this->updateToken(node->getToken());
}

void CmdNode::addRedirOption(TokenKind kind, Token token) {
    this->addRedirOption(kind, new CmdArgNode(new StringValueNode(token, std::string(""))));
}

void CmdNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(nameNode);
    DUMP(argNodes);
}

void CmdNode::accept(NodeVisitor &visitor) {
    visitor.visitCmdNode(*this);
}

// ##########################
// ##     PipedCmdNode     ##
// ##########################

PipedCmdNode::~PipedCmdNode() {
    for(auto *p : this->cmdNodes) {
        delete p;
    }
}

void PipedCmdNode::addCmdNodes(Node *node) {
    this->cmdNodes.push_back(node);
    this->updateToken(node->getToken());
}

void PipedCmdNode::dump(NodeDumper &dumper) const {
    DUMP(cmdNodes);
}

void PipedCmdNode::accept(NodeVisitor &visitor) {
    visitor.visitPipedCmdNode(*this);
}

// ##############################
// ##     SubstitutionNode     ##
// ##############################

SubstitutionNode::~SubstitutionNode() {
    delete this->exprNode;
}

void SubstitutionNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(exprNode);
    DUMP_PRIM(strExpr);
}

void SubstitutionNode::accept(NodeVisitor &visitor) {
    visitor.visitSubstitutionNode(*this);
}

// ########################
// ##     AssertNode     ##
// ########################

AssertNode::~AssertNode() {
    delete this->condNode;
    delete this->messageNode;
}

void AssertNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(condNode);
    DUMP_PTR(messageNode);
}

void AssertNode::accept(NodeVisitor &visitor) {
    visitor.visitAssertNode(*this);
}

// #######################
// ##     BlockNode     ##
// #######################

BlockNode::~BlockNode() {
    for(Node *n : this->nodeList) {
        delete n;
    }
}

void BlockNode::addNode(Node *node) {
    this->nodeList.push_back(node);
}

void BlockNode::insertNodeToFirst(Node *node) {
    this->nodeList.push_front(node);
}

void BlockNode::addReturnNodeToLast(TypePool &pool, Node *exprNode) {
    assert(!this->isUntyped() && !this->getType().isBottomType());
    assert(!exprNode->isUntyped());

    ReturnNode *returnNode = new ReturnNode(exprNode->getToken(), exprNode);
    returnNode->setType(pool.getBottomType());
    this->addNode(returnNode);
    this->setType(returnNode->getType());
}

void BlockNode::dump(NodeDumper &dumper) const {
    DUMP(nodeList);
    DUMP_PRIM(baseIndex);
    DUMP_PRIM(varSize);
}

void BlockNode::accept(NodeVisitor &visitor) {
    visitor.visitBlockNode(*this);
}

// ######################
// ##     JumpNode     ##
// ######################

void JumpNode::dump(NodeDumper &dumper) const {
    DUMP_PRIM(leavingBlock);
}

void JumpNode::accept(NodeVisitor &visitor) {
    visitor.visitJumpNode(*this);
}

// ###########################
// ##     ExportEnvNode     ##
// ###########################

ExportEnvNode::~ExportEnvNode() {
    delete this->exprNode;
}

void ExportEnvNode::setAttribute(FieldHandle *handle) {
    this->global = handle->attr().has(FieldAttribute::GLOBAL);
    this->varIndex = handle->getFieldIndex();
}

void ExportEnvNode::dump(NodeDumper &dumper) const {
    DUMP(envName);
    DUMP_PTR(exprNode);
    DUMP_PRIM(global);
    DUMP_PRIM(varIndex);
}

void ExportEnvNode::accept(NodeVisitor &visitor) {
    visitor.visitExportEnvNode(*this);
}

// ###########################
// ##     ImportEnvNode     ##
// ###########################

ImportEnvNode::~ImportEnvNode() {
    delete this->defaultValueNode;
}

void ImportEnvNode::setAttribute(FieldHandle *handle) {
    this->global = handle->attr().has(FieldAttribute::GLOBAL);
    this->varIndex = handle->getFieldIndex();
}

void ImportEnvNode::dump(NodeDumper &dumper) const {
    DUMP(envName);
    DUMP_PTR(defaultValueNode);
    DUMP_PRIM(global);
    DUMP_PRIM(varIndex);
}

void ImportEnvNode::accept(NodeVisitor &visitor) {
    visitor.visitImportEnvNode(*this);
}

// ###########################
// ##     TypeAliasNode     ##
// ###########################

TypeAliasNode::~TypeAliasNode() {
    delete this->targetTypeNode;
}

void TypeAliasNode::dump(NodeDumper &dumper) const {
    DUMP(alias);
    DUMP_PTR(targetTypeNode);
}

void TypeAliasNode::accept(NodeVisitor &visitor) {
    visitor.visitTypeAliasNode(*this);
}

// #####################
// ##     ForNode     ##
// #####################

ForNode::ForNode(unsigned int startPos, Node *initNode,
                 Node *condNode, Node *iterNode, BlockNode *blockNode) :
        Node({startPos, 0}), initNode(initNode), condNode(condNode),
        iterNode(iterNode), blockNode(blockNode) {
    if(this->initNode == nullptr) {
        this->initNode = new EmptyNode();
    }

    if(this->condNode == nullptr) {
        this->condNode = new VarNode({startPos, 1}, std::string(VAR_TRUE));
    }

    if(this->iterNode == nullptr) {
        this->iterNode = new EmptyNode();
    }

    this->updateToken(blockNode->getToken());
}

ForNode::~ForNode() {
    delete this->initNode;
    delete this->condNode;
    delete this->iterNode;
    delete this->blockNode;
}

void ForNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(initNode);
    DUMP_PTR(condNode);
    DUMP_PTR(iterNode);
    DUMP_PTR(blockNode);
}

void ForNode::accept(NodeVisitor &visitor) {
    visitor.visitForNode(*this);
}

// #######################
// ##     WhileNode     ##
// #######################

WhileNode::~WhileNode() {
    delete this->condNode;
    delete this->blockNode;
}

void WhileNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(condNode);
    DUMP_PTR(blockNode);
}

void WhileNode::accept(NodeVisitor &visitor) {
    visitor.visitWhileNode(*this);
}

// #########################
// ##     DoWhileNode     ##
// #########################

DoWhileNode::~DoWhileNode() {
    delete this->blockNode;
    delete this->condNode;
}

void DoWhileNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(blockNode);
    DUMP_PTR(condNode);
}

void DoWhileNode::accept(NodeVisitor &visitor) {
    visitor.visitDoWhileNode(*this);
}

// ####################
// ##     IfNode     ##
// ####################

/**
 * if condNode is InstanceOfNode and targetNode is VarNode, insert VarDeclNode to blockNode.
 */
static void resolveIfIsStatement(Node *condNode, Node *thenNode) {
    assert(dynamic_cast<BlockNode *>(thenNode) != nullptr);

    InstanceOfNode *isNode = dynamic_cast<InstanceOfNode *>(condNode);
    if(isNode == nullptr) {
        return;
    }

    VarNode *varNode = dynamic_cast<VarNode *>(isNode->getTargetNode());
    if(varNode == nullptr) {
        return;
    }

    VarNode *exprNode = new VarNode({isNode->getPos(), 1}, std::string(varNode->getVarName()));
    CastNode *castNode = new CastNode(exprNode, isNode->getTargetTypeNode(), true);
    VarDeclNode *declNode =
            new VarDeclNode(isNode->getPos(), std::string(varNode->getVarName()), castNode, true);
    static_cast<BlockNode *>(thenNode)->insertNodeToFirst(declNode);
}

IfNode::IfNode(unsigned int startPos, Node *condNode, BlockNode *thenNode, Node *elseNode) :
        Node({startPos, 0}), condNode(condNode), thenNode(thenNode), elseNode(elseNode) {

    resolveIfIsStatement(this->condNode, this->thenNode);
    this->updateToken(thenNode->getToken());
    if(this->elseNode != nullptr) {
        this->updateToken(this->elseNode->getToken());
    }
    if(this->elseNode == nullptr) {
        this->elseNode = new EmptyNode(this->getToken());
    }
}

IfNode::~IfNode() {
    delete this->condNode;
    delete this->thenNode;
    delete this->elseNode;
}

void IfNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(condNode);
    DUMP_PTR(thenNode);
    DUMP_PTR(elseNode);
}

void IfNode::accept(NodeVisitor &visitor) {
    visitor.visitIfNode(*this);
}

// ########################
// ##     ReturnNode     ##
// ########################

ReturnNode::ReturnNode(Token token, Node *exprNode) :
        BlockEndNode(token), exprNode(exprNode != nullptr ? exprNode : new EmptyNode(token)) {
    if(exprNode != nullptr) {
        this->updateToken(exprNode->getToken());
    }
}

ReturnNode::~ReturnNode() {
    delete this->exprNode;
}

void ReturnNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(exprNode);
}

void ReturnNode::accept(NodeVisitor &visitor) {
    visitor.visitReturnNode(*this);
}

// #######################
// ##     ThrowNode     ##
// #######################

ThrowNode::~ThrowNode() {
    delete this->exprNode;
}

void ThrowNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(exprNode);
}

void ThrowNode::accept(NodeVisitor &visitor) {
    visitor.visitThrowNode(*this);
}

// #######################
// ##     CatchNode     ##
// #######################

CatchNode::~CatchNode() {
    delete this->typeNode;
    delete this->blockNode;
}

void CatchNode::setAttribute(FieldHandle *handle) {
    this->varIndex = handle->getFieldIndex();
}

void CatchNode::dump(NodeDumper &dumper) const {
    DUMP(exceptionName);
    DUMP_PTR(typeNode);
    DUMP_PTR(blockNode);
    DUMP_PRIM(varIndex);
}

void CatchNode::accept(NodeVisitor &visitor) {
    visitor.visitCatchNode(*this);
}

// #####################
// ##     TryNode     ##
// #####################

TryNode::~TryNode() {
    delete this->blockNode;

    for(CatchNode *n : this->catchNodes) {
        delete n;
    }

    delete this->finallyNode;
}

void TryNode::addCatchNode(CatchNode *catchNode) {
    this->catchNodes.push_back(catchNode);
    this->updateToken(catchNode->getToken());
}

void TryNode::addFinallyNode(BlockNode *finallyNode) {
    this->finallyNode = finallyNode;
    this->updateToken(finallyNode->getToken());
}

void TryNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(blockNode);
    DUMP(catchNodes);

    DUMP_PTR(finallyNode);
}

void TryNode::accept(NodeVisitor &visitor) {
    visitor.visitTryNode(*this);
}

// #########################
// ##     VarDeclNode     ##
// #########################

VarDeclNode::VarDeclNode(unsigned int startPos, std::string &&varName, Node *initValueNode, bool readOnly) :
        Node({startPos, 0}), varName(std::move(varName)), readOnly(readOnly), global(false),
        varIndex(0), initValueNode(initValueNode) {
    if(this->initValueNode != nullptr) {
        this->updateToken(initValueNode->getToken());
    }
}

VarDeclNode::~VarDeclNode() {
    delete this->initValueNode;
}

void VarDeclNode::setAttribute(FieldHandle *handle) {
    this->global = handle->attr().has(FieldAttribute::GLOBAL);
    this->varIndex = handle->getFieldIndex();
}

void VarDeclNode::dump(NodeDumper &dumper) const {
    DUMP(varName);
    DUMP_PRIM(readOnly);
    DUMP_PRIM(global);
    DUMP_PRIM(varIndex);
    DUMP_PTR(initValueNode);
}

void VarDeclNode::accept(NodeVisitor &visitor) {
    visitor.visitVarDeclNode(*this);
}

// ########################
// ##     AssignNode     ##
// ########################

AssignNode::~AssignNode() {
    delete this->leftNode;
    delete this->rightNode;
}

void AssignNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(leftNode);
    DUMP_PTR(rightNode);

#define EACH_FLAG(OP) \
    OP(SELF_ASSIGN) \
    OP(FIELD_ASSIGN)

    DUMP_BITSET(attributeSet, EACH_FLAG);
#undef EACH_FLAG
}

void AssignNode::accept(NodeVisitor &visitor) {
    visitor.visitAssignNode(*this);
}

// ###################################
// ##     ElementSelfAssignNode     ##
// ###################################

ElementSelfAssignNode::ElementSelfAssignNode(MethodCallNode *leftNode, BinaryOpNode *binaryNode) :
        Node(leftNode->getToken()),
        recvNode(), indexNode(),
        getterNode(), setterNode(), rightNode(binaryNode) {
    this->updateToken(binaryNode->getToken());

    // init recv, indexNode
    this->recvNode = leftNode->getRecvNode();
    leftNode->setRecvNode(nullptr);
    this->indexNode = leftNode->getArgNodes()[0];
    leftNode->refArgNodes()[0] = nullptr;
    delete leftNode;

    // init getter node
    this->getterNode = new MethodCallNode(new EmptyNode(), std::string(OP_GET));
    this->getterNode->refArgNodes().push_back(new EmptyNode());

    // init setter node
    this->setterNode = new MethodCallNode(new EmptyNode(), std::string(OP_SET));
    this->setterNode->refArgNodes().push_back(new EmptyNode());
    this->setterNode->refArgNodes().push_back(new EmptyNode());
}

ElementSelfAssignNode::~ElementSelfAssignNode() {
    delete this->recvNode;
    delete this->indexNode;
    delete this->getterNode;
    delete this->setterNode;
    delete this->rightNode;
}

void ElementSelfAssignNode::setRecvType(DSType &type) {
    this->getterNode->getRecvNode()->setType(type);
    this->setterNode->getRecvNode()->setType(type);
}

void ElementSelfAssignNode::setIndexType(DSType &type) {
    this->getterNode->refArgNodes()[0]->setType(type);
    this->setterNode->refArgNodes()[0]->setType(type);
}

void ElementSelfAssignNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(recvNode);
    DUMP_PTR(indexNode);
    DUMP_PTR(getterNode);
    DUMP_PTR(setterNode);
    DUMP_PTR(rightNode);
}

void ElementSelfAssignNode::accept(NodeVisitor &visitor) {
    visitor.visitElementSelfAssignNode(*this);
}


// ##########################
// ##     FunctionNode     ##
// ##########################

FunctionNode::~FunctionNode() {
    for(VarNode *n : this->paramNodes) {
        delete n;
    }

    for(TypeNode *t : this->paramTypeNodes) {
        delete t;
    }

    delete this->returnTypeNode;
    delete this->blockNode;
}

void FunctionNode::addParamNode(VarNode *node, TypeNode *paramType) {
    this->paramNodes.push_back(node);
    this->paramTypeNodes.push_back(paramType);
}

TypeNode *FunctionNode::getReturnTypeToken() {
    if(this->returnTypeNode == nullptr) {
        this->returnTypeNode = newVoidTypeNode();
    }
    return this->returnTypeNode;
}

void FunctionNode::dump(NodeDumper &dumper) const {
    DUMP(name);
    DUMP(paramNodes);
    DUMP(paramTypeNodes);

    DUMP_PTR(returnTypeNode);
    DUMP_PTR(blockNode);
    DUMP_PRIM(maxVarNum);
    DUMP_PRIM(varIndex);
    dumper.dump("sourceName", this->srcInfoPtr->getSourceName());
}

void FunctionNode::accept(NodeVisitor &visitor) {
    visitor.visitFunctionNode(*this);
}

// ###########################
// ##     InterfaceNode     ##
// ###########################

InterfaceNode::~InterfaceNode() {
    for(FunctionNode *node : this->methodDeclNodes) {
        delete node;
    }

    for(VarDeclNode *node : this->fieldDeclNodes) {
        delete node;
    }

    for(TypeNode *t : this->fieldTypeNodes) {
        delete t;
    }
}

void InterfaceNode::addMethodDeclNode(FunctionNode *methodDeclNode) {
    this->methodDeclNodes.push_back(methodDeclNode);
}

void InterfaceNode::addFieldDecl(VarDeclNode *node, TypeNode *typeToken) {
    this->fieldDeclNodes.push_back(node);
    this->fieldTypeNodes.push_back(typeToken);
    this->updateToken(typeToken->getToken());
}

void InterfaceNode::dump(NodeDumper &dumper) const {
    DUMP(interfaceName);
    DUMP(methodDeclNodes);
    DUMP(fieldDeclNodes);
    DUMP(fieldTypeNodes);
}

void InterfaceNode::accept(NodeVisitor &visitor) {
    visitor.visitInterfaceNode(*this);
}

// ################################
// ##     UserDefinedCmdNode     ##
// ################################

UserDefinedCmdNode::~UserDefinedCmdNode() {
    delete this->blockNode;
}

void UserDefinedCmdNode::dump(NodeDumper &dumper) const {
    DUMP(name);
    DUMP_PRIM(udcIndex);
    DUMP_PTR(blockNode);
    DUMP_PRIM(maxVarNum);
    dumper.dump("sourceName", this->srcInfoPtr->getSourceName());
}

void UserDefinedCmdNode::accept(NodeVisitor &visitor) {
    visitor.visitUserDefinedCmdNode(*this);
}

// #######################
// ##     EmptyNode     ##
// #######################

void EmptyNode::dump(NodeDumper &) const {
} // do nothing

void EmptyNode::accept(NodeVisitor &visitor) {
    visitor.visitEmptyNode(*this);
}

// ######################
// ##     RootNode     ##
// ######################

RootNode::~RootNode() {
    for(Node *n : this->nodeList) {
        delete n;
    }
}

void RootNode::addNode(Node *node) {
    this->nodeList.push_back(node);
}

void RootNode::dump(NodeDumper &dumper) const {
    DUMP(nodeList);
    dumper.dump("sourceName", this->srcInfoPtr->getSourceName());
    DUMP_PRIM(maxVarNum);
    DUMP_PRIM(maxGVarNum);
}

void RootNode::accept(NodeVisitor &visitor) {
    visitor.visitRootNode(*this);
}

// for node creation

const char *resolveUnaryOpName(TokenKind op) {
    switch(op) {
    case PLUS:  // +
        return OP_PLUS;
    case MINUS: // -
        return OP_MINUS;
    case NOT:   // not
        return OP_NOT;
    default:
        fatal("unsupported unary op: %s\n", TO_NAME(op));
        return nullptr;
    }
}

const char *resolveBinaryOpName(TokenKind op) {
    switch(op) {
    case PLUS:
        return OP_ADD;
    case MINUS:
        return OP_SUB;
    case MUL:
        return OP_MUL;
    case DIV:
        return OP_DIV;
    case MOD:
        return OP_MOD;
    case EQ:
        return OP_EQ;
    case NE:
        return OP_NE;
    case LA:
        return OP_LT;
    case RA:
        return OP_GT;
    case LE:
        return OP_LE;
    case GE:
        return OP_GE;
    case AND:
        return OP_AND;
    case OR:
        return OP_OR;
    case XOR:
        return OP_XOR;
    case MATCH:
        return OP_MATCH;
    case UNMATCH:
        return OP_UNMATCH;
    default:
        fatal("unsupported binary op: %s\n", TO_NAME(op));
        return nullptr;
    }
}

TokenKind resolveAssignOp(TokenKind op) {
    switch(op) {
    case INC:
        return PLUS;
    case DEC:
        return MINUS;
    case ADD_ASSIGN:
        return PLUS;
    case SUB_ASSIGN:
        return MINUS;
    case MUL_ASSIGN:
        return MUL;
    case DIV_ASSIGN:
        return DIV;
    case MOD_ASSIGN:
        return MOD;
    default:
        fatal("unsupported assign op: %s\n", TO_NAME(op));
        return INVALID;
    }
}

ForNode *createForInNode(unsigned int startPos, std::string &&varName, Node *exprNode, BlockNode *blockNode) {
    Token dummy = {startPos, 1};

    // create for-init
    MethodCallNode *call_iter = new MethodCallNode(exprNode, std::string(OP_ITER));
    std::string reset_var_name(std::to_string(rand()));
    VarDeclNode *reset_varDecl = new VarDeclNode(startPos, std::string(reset_var_name), call_iter, true);

    // create for-cond
    VarNode *reset_var = new VarNode(dummy, std::string(reset_var_name));
    MethodCallNode *call_hasNext = new MethodCallNode(reset_var, std::string(OP_HAS_NEXT));

    // create forIn-init
    reset_var = new VarNode(dummy, std::string(reset_var_name));
    MethodCallNode *call_next = new MethodCallNode(reset_var, std::string(OP_NEXT));
    VarDeclNode *init_var = new VarDeclNode(startPos, std::move(varName), call_next, false);

    // insert init to block
    blockNode->insertNodeToFirst(init_var);

    return new ForNode(startPos, reset_varDecl, call_hasNext, nullptr, blockNode);
}

Node *createSuffixNode(Node *leftNode, TokenKind op, Token token) {
    return createAssignNode(leftNode, op, IntValueNode::newByte(token, 1));
}

Node *createAssignNode(Node *leftNode, TokenKind op, Node *rightNode) {
    /*
     * basic assignment
     */
    if(op == ASSIGN) {
        // assign to element(actually call SET)
        MethodCallNode *indexNode = dynamic_cast<MethodCallNode *>(leftNode);
        if(indexNode != nullptr && indexNode->hasAttribute(MethodCallNode::INDEX)) {
            indexNode->setMethodName(std::string(OP_SET));
            indexNode->refArgNodes().push_back(rightNode);
            return indexNode;
        } else {
            // assign to variable or field
            return new AssignNode(leftNode, rightNode);
        }
    }

    /**
     * self assignment
     */
    // assign to element
    BinaryOpNode *opNode = new BinaryOpNode(new EmptyNode(), resolveAssignOp(op), rightNode);
    MethodCallNode *indexNode = dynamic_cast<MethodCallNode *>(leftNode);
    if(indexNode != nullptr && indexNode->hasAttribute(MethodCallNode::INDEX)) {
        return new ElementSelfAssignNode(indexNode, opNode);
    } else {
        // assign to variable or field
        return new AssignNode(leftNode, opNode, true);
    }
}

Node *createIndexNode(Node *recvNode, Node *indexNode) {
    MethodCallNode *methodCallNode = new MethodCallNode(recvNode, std::string(OP_GET));
    methodCallNode->setAttribute(MethodCallNode::INDEX);
    methodCallNode->refArgNodes().push_back(indexNode);
    return methodCallNode;
}

Node *createBinaryOpNode(Node *leftNode, TokenKind op, Node *rightNode) {
    switch(op) {
    case COND_OR:
        return new CondOpNode(leftNode, rightNode, false);
    case COND_AND:
        return new CondOpNode(leftNode, rightNode, true);
    default:
        return new BinaryOpNode(leftNode, op, rightNode);
    }
}

} // namespace ydsh