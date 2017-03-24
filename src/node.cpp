/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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
#include "cmd.h"


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


// ########################
// ##     NumberNode     ##
// ########################

void NumberNode::dump(NodeDumper &dumper) const {
#define EACH_ENUM(OP) \
    OP(BYTE) \
    OP(INT16) \
    OP(UINT16) \
    OP(INT32) \
    OP(UINT32) \
    OP(INT64) \
    OP(UINT64) \
    OP(FLOAT)

    DUMP_ENUM(kind, EACH_ENUM);
#undef EACH_ENUM

    switch(this->kind) {
    case BYTE:
    case INT16:
    case UINT16:
    case INT32:
    case UINT32:
        DUMP_PRIM(intValue);
        break;
    case INT64:
    case UINT64:
        DUMP_PRIM(longValue);
        break;
    case FLOAT:
        DUMP_PRIM(floatValue);
        break;
    }
}

void NumberNode::accept(NodeVisitor &visitor) {
    visitor.visitNumberNode(*this);
}

// #######################
// ##     StringNode    ##
// #######################

void StringNode::dump(NodeDumper &dumper) const {
#define EACH_ENUM(OP) \
    OP(STRING) \
    OP(OBJECT_PATH) \
    OP(TILDE)

    DUMP_ENUM(kind, EACH_ENUM);

#undef EACH_ENUM

    DUMP(value);
}

void StringNode::accept(NodeVisitor &visitor) {
    visitor.visitStringNode(*this);
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
    } else if(dynamic_cast<StringNode *>(node) != nullptr &&
            static_cast<StringNode *>(node)->getValue().empty()) { // ignore empty string value node
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
// ##     RegexNode     ##
// #######################

void RegexNode::dump(NodeDumper &dumper) const {
    DUMP(reStr);
}

void RegexNode::accept(NodeVisitor &visitor) {
    visitor.visitRegexNode(*this);
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

// ########################
// ##     TypeOpNode     ##
// ########################

TypeOpNode::TypeOpNode(Node *exprNode, TypeNode *type, OpKind init, bool dupTypeToken) :
        Node(exprNode->getToken()), exprNode(exprNode), targetTypeNode(nullptr),
        opKind(init) {
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

TypeOpNode::~TypeOpNode() {
    delete this->exprNode;

    if((long) this->targetTypeNode >= 0) {
        delete this->targetTypeNode;
    }
}

TypeNode *TypeOpNode::getTargetTypeNode() const {
    static const unsigned long mask = ~(1L << 63);
    if((long) this->targetTypeNode < 0) {
        TypeNode *tok = reinterpret_cast<TypeNode *>(mask & (unsigned long) this->targetTypeNode);
        return tok;
    }
    return this->targetTypeNode;
}

void TypeOpNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(exprNode);
    TypeNode *targetTypeToken = this->getTargetTypeNode();
    DUMP_PTR(targetTypeToken);

#define EACH_ENUM(OP) \
    OP(NO_CAST) \
    OP(TO_VOID) \
    OP(NUM_CAST) \
    OP(TO_STRING) \
    OP(CHECK_CAST) \
    OP(CHECK_UNWRAP) \
    OP(PRINT) \
    OP(ALWAYS_FALSE) \
    OP(ALWAYS_TRUE) \
    OP(INSTANCEOF)

    DUMP_ENUM(opKind, EACH_ENUM);
#undef EACH_ENUM
}

void TypeOpNode::accept(NodeVisitor &visitor) {
    visitor.visitTypeOpNode(*this);
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

void BinaryOpNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(leftNode);
    DUMP_PTR(rightNode);
    dumper.dump(NAME(op), TO_NAME(op));
    DUMP_PTR(optNode);
}

void BinaryOpNode::accept(NodeVisitor &visitor) {
    visitor.visitBinaryOpNode(*this);
}

// #########################
// ##     TernaryNode     ##
// #########################

TernaryNode::~TernaryNode() {
    delete this->condNode;
    delete this->leftNode;
    delete this->rightNode;
}

void TernaryNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(condNode);
    DUMP_PTR(leftNode);
    DUMP_PTR(rightNode);
}

void TernaryNode::accept(NodeVisitor &visitor) {
    visitor.visitTernaryNode(*this);
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
            (dynamic_cast<StringNode *>(this->segmentNodes.back()) == nullptr &&
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
    this->redirCount++;
}

void CmdNode::addRedirOption(TokenKind kind, Token token) {
    this->addRedirOption(kind, new CmdArgNode(new StringNode(token, std::string(""))));
}

void CmdNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(nameNode);
    DUMP(argNodes);
    DUMP_PRIM(redirCount);
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
    for(Node *n : this->nodes) {
        delete n;
    }
}

void BlockNode::addNode(Node *node) {
    this->nodes.push_back(node);
}

void BlockNode::insertNodeToFirst(Node *node) {
    this->nodes.insert(this->nodes.begin(), node);
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
    DUMP(nodes);
    DUMP_PRIM(baseIndex);
    DUMP_PRIM(varSize);
    DUMP_PRIM(maxVarSize);
}

void BlockNode::accept(NodeVisitor &visitor) {
    visitor.visitBlockNode(*this);
}

// ######################
// ##     JumpNode     ##
// ######################

void JumpNode::dump(NodeDumper &dumper) const {
    DUMP_PRIM(asBreak);
    DUMP_PRIM(leavingBlock);
}

void JumpNode::accept(NodeVisitor &visitor) {
    visitor.visitJumpNode(*this);
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

// ######################
// ##     LoopNode     ##
// ######################

LoopNode::LoopNode(unsigned int startPos, Node *initNode,
                 Node *condNode, Node *iterNode, BlockNode *blockNode, bool asDoWhile) :
        Node({startPos, 0}), initNode(initNode), condNode(condNode),
        iterNode(iterNode), blockNode(blockNode), asDoWhile(asDoWhile) {
    if(this->initNode == nullptr) {
        this->initNode = new EmptyNode();
    }

    if(this->condNode == nullptr) {
        this->condNode = new VarNode({startPos, 1}, std::string(VAR_TRUE));
    }

    if(this->iterNode == nullptr) {
        this->iterNode = new EmptyNode();
    }

    this->updateToken(this->asDoWhile ? condNode->getToken() : blockNode->getToken());
}

LoopNode::~LoopNode() {
    delete this->initNode;
    delete this->condNode;
    delete this->iterNode;
    delete this->blockNode;
}

void LoopNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(initNode);
    DUMP_PTR(condNode);
    DUMP_PTR(iterNode);
    DUMP_PTR(blockNode);
    DUMP_PRIM(asDoWhile);
}

void LoopNode::accept(NodeVisitor &visitor) {
    visitor.visitLoopNode(*this);
}

// ####################
// ##     IfNode     ##
// ####################

/**
 * if condNode is InstanceOfNode and targetNode is VarNode, insert VarDeclNode to blockNode.
 */
static void resolveIfIsStatement(Node *condNode, BlockNode *blockNode) {
    auto *isNode = dynamic_cast<TypeOpNode *>(condNode);
    if(isNode == nullptr || !isNode->isInstanceOfOp()) {
        return;
    }

    VarNode *varNode = dynamic_cast<VarNode *>(isNode->getExprNode());
    if(varNode == nullptr) {
        return;
    }

    VarNode *exprNode = new VarNode({isNode->getPos(), 1}, std::string(varNode->getVarName()));
    TypeOpNode *castNode = new TypeOpNode(exprNode, isNode->getTargetTypeNode(), TypeOpNode::NO_CAST, true);
    VarDeclNode *declNode =
            new VarDeclNode(isNode->getPos(), std::string(varNode->getVarName()), castNode, VarDeclNode::CONST);
    blockNode->insertNodeToFirst(declNode);
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
        Node(token), exprNode(exprNode != nullptr ? exprNode : new EmptyNode(token)) {
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

VarDeclNode::VarDeclNode(unsigned int startPos, std::string &&varName, Node *exprNode, Kind kind) :
        Node({startPos, 0}), varName(std::move(varName)), global(false), kind(kind),
        varIndex(0), exprNode(exprNode) {
    if(this->exprNode != nullptr) {
        this->updateToken(exprNode->getToken());
    }
}

VarDeclNode::~VarDeclNode() {
    delete this->exprNode;
}

void VarDeclNode::setAttribute(FieldHandle *handle) {
    this->global = handle->attr().has(FieldAttribute::GLOBAL);
    this->varIndex = handle->getFieldIndex();
}

void VarDeclNode::dump(NodeDumper &dumper) const {
    DUMP(varName);
    DUMP_PRIM(global);
    DUMP_PRIM(varIndex);
    DUMP_PTR(exprNode);

#define EACH_ENUM(OP) \
    OP(VAR) \
    OP(CONST) \
    OP(IMPORT_ENV) \
    OP(EXPORT_ENV)

    DUMP_ENUM(kind, EACH_ENUM);
#undef EACH_ENUM
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
    for(Node *n : this->nodes) {
        delete n;
    }
}

void RootNode::addNode(Node *node) {
    this->nodes.push_back(node);
}

void RootNode::dump(NodeDumper &dumper) const {
    DUMP(nodes);
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

LoopNode *createForInNode(unsigned int startPos, std::string &&varName, Node *exprNode, BlockNode *blockNode) {
    Token dummy = {startPos, 1};

    // create for-init
    MethodCallNode *call_iter = new MethodCallNode(exprNode, std::string(OP_ITER));
    std::string reset_var_name(std::to_string(rand()));
    VarDeclNode *reset_varDecl = new VarDeclNode(startPos, std::string(reset_var_name), call_iter, VarDeclNode::CONST);

    // create for-cond
    VarNode *reset_var = new VarNode(dummy, std::string(reset_var_name));
    MethodCallNode *call_hasNext = new MethodCallNode(reset_var, std::string(OP_HAS_NEXT));

    // create forIn-init
    reset_var = new VarNode(dummy, std::string(reset_var_name));
    MethodCallNode *call_next = new MethodCallNode(reset_var, std::string(OP_NEXT));
    VarDeclNode *init_var = new VarDeclNode(startPos, std::move(varName), call_next, VarDeclNode::VAR);

    // insert init to block
    blockNode->insertNodeToFirst(init_var);

    return new LoopNode(startPos, reset_varDecl, call_hasNext, nullptr, blockNode);
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

} // namespace ydsh