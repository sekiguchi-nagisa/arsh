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
#include "handle.h"

// helper macro for node dumper
#define NAME(f) #f

#define DUMP(field) dumper.dump(NAME(field), field)
#define DUMP_PRIM(field) dumper.dump(NAME(field), std::to_string(field))
#define DUMP_PTR(field) \
    do {\
        if((field) == nullptr) {\
            dumper.dumpNull(NAME(field));\
        } else {\
            dumper.dump(NAME(field), *(field));\
        }\
    } while(false)


// not directly use it.
#define GEN_ENUM_STR(ENUM) case ENUM: ___str__ = #ENUM; break;

#define DUMP_ENUM(val, EACH_ENUM) \
    do {\
        const char *___str__ = nullptr;\
        switch(val) {\
        EACH_ENUM(GEN_ENUM_STR)\
        }\
        dumper.dump(NAME(val), ___str__);\
    } while(false)

// not directly use it.
#define GEN_FLAG_STR(FLAG) \
        if((___set__ & (FLAG))) { if(___count__++ > 0) { ___str__ += " | "; } ___str__ += #FLAG; }

#define DUMP_BITSET(val, EACH_FLAG) \
    do {\
        unsigned int ___count__ = 0;\
        std::string ___str__;\
        auto ___set__ = val;\
        EACH_FLAG(GEN_FLAG_STR)\
        dumper.dump(NAME(val), ___str__);\
    } while(false)



namespace ydsh {

// ##################
// ##     Node     ##
// ##################

void Node::accept(NodeVisitor &visitor) {
    switch(this->nodeKind) {
#define DISPATCH(K) case NodeKind::K: visitor.visit ## K ## Node(*static_cast<K ## Node *>(this)); break;
    EACH_NODE_KIND(DISPATCH)
#undef DISPATCH
    }
}

// ######################
// ##     TypeNode     ##
// ######################

void TypeNode::dump(NodeDumper &dumper) const {
    DUMP_ENUM(typeKind, EACH_TYPE_NODE_KIND);
}


// ##########################
// ##     BaseTypeNode     ##
// ##########################

void BaseTypeNode::dump(NodeDumper &dumper) const {
    TypeNode::dump(dumper);
    DUMP(typeName);
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
    TypeNode::dump(dumper);
    DUMP_PTR(templateTypeNode);
    DUMP(elementTypeNodes);
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
    TypeNode::dump(dumper);
    DUMP_PTR(returnTypeNode);
    DUMP(paramTypeNodes);
}

// ###############################
// ##     DBusIfaceTypeNode     ##
// ###############################

void DBusIfaceTypeNode::dump(NodeDumper &dumper) const {
    TypeNode::dump(dumper);
    DUMP(name);
}

// ############################
// ##     ReturnTypeNode     ##
// ############################

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
    TypeNode::dump(dumper);
    DUMP(typeNodes);
}

// ########################
// ##     TypeOfNode     ##
// ########################

TypeOfNode::~TypeOfNode() {
    delete this->exprNode;
}

void TypeOfNode::dump(NodeDumper &dumper) const {
    TypeNode::dump(dumper);
    DUMP_PTR(exprNode);
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
    DUMP_ENUM(kind, EACH_NUMBER_NODE_KIND);

    switch(this->kind) {
    case Byte:
    case Int16:
    case Uint16:
    case Int32:
    case Uint32:
        DUMP_PRIM(intValue);
        break;
    case Int64:
    case Uint64:
        DUMP_PRIM(longValue);
        break;
    case Float:
        DUMP_PRIM(floatValue);
        break;
    case Signal:
        DUMP_PRIM(intValue);
        break;
    }
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

// ############################
// ##     StringExprNode     ##
// ############################

StringExprNode::~StringExprNode() {
    for(Node *e : this->nodes) {
        delete e;
    }
}

void StringExprNode::addExprNode(Node *node) {
    if(node->is(NodeKind::StringExpr)) {
        auto *exprNode = static_cast<StringExprNode *>(node);
        for(auto &e : exprNode->nodes) {
            this->nodes.push_back(e);
        }
        exprNode->nodes.clear();
        delete node;
    } else if(node->is(NodeKind::String) &&
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

// #######################
// ##     RegexNode     ##
// #######################

void RegexNode::dump(NodeDumper &dumper) const {
    DUMP(reStr);
}

// #######################
// ##     ArrayNode     ##
// #######################

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

// #####################
// ##     MapNode     ##
// #####################

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

// #######################
// ##     TupleNode     ##
// #######################

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

// ########################
// ##     AccessNode     ##
// ########################

AccessNode::~AccessNode() {
    delete this->recvNode;
    delete this->nameNode;
}

void AccessNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(recvNode);
    DUMP_PTR(nameNode);
    AssignableNode::dump(dumper);

#define EACH_ENUM(OP) \
    OP(NOP) \
    OP(DUP_RECV)

    DUMP_ENUM(additionalOp, EACH_ENUM);
#undef EACH_ENUM
}

// ########################
// ##     TypeOpNode     ##
// ########################

TypeOpNode::TypeOpNode(Node *exprNode, TypeNode *type, OpKind init, bool dupTypeToken) :
        Node(NodeKind::TypeOp, exprNode->getToken()), exprNode(exprNode), targetTypeNode(nullptr),
        opKind(init) {
    static const unsigned long tag = (unsigned long) 1L << 63;

    if(dupTypeToken) {
        auto *tok = reinterpret_cast<TypeNode *>(tag | (unsigned long) type);
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
        auto *tok = reinterpret_cast<TypeNode *>(mask & (unsigned long) this->targetTypeNode);
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

// #######################
// ##     ApplyNode     ##
// #######################

ApplyNode::ApplyNode(Node *exprNode, std::vector<Node *> &&argNodes) :
        Node(NodeKind::Apply, exprNode->getToken()),
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

// ############################
// ##     MethodCallNode     ##
// ############################

MethodCallNode::MethodCallNode(Node *recvNode, std::string &&methodName, std::vector<Node *> &&argNodes) :
        Node(NodeKind::MethodCall, recvNode->getToken()),
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

// #####################
// ##     NewNode     ##
// #####################

NewNode::NewNode(unsigned int startPos, TypeNode *targetTypeNode, std::vector<Node *> &&argNodes) :
        Node(NodeKind::New, {startPos, 0}), targetTypeNode(targetTypeNode), argNodes(std::move(argNodes)) {
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
    DUMP(op);
    DUMP_PTR(exprNode);
    DUMP_PTR(methodCallNode);
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
    DUMP(op);
    DUMP_PTR(optNode);
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

bool CmdArgNode::isIgnorableEmptyString() {
    return this->segmentNodes.size() > 1 ||
            (!this->segmentNodes.back()->is(NodeKind::String) &&
                    !this->segmentNodes.back()->is(NodeKind::StringExpr));
}

// #######################
// ##     RedirNode     ##
// #######################

RedirNode::~RedirNode() {
    delete this->targetNode;
}

void RedirNode::dump(NodeDumper &dumper) const {
    DUMP(op);
    DUMP_PTR(targetNode);
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

void CmdNode::addRedirNode(RedirNode *node) {
    this->argNodes.push_back(node);
    this->updateToken(node->getToken());
    this->redirCount++;
}

void CmdNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(nameNode);
    DUMP(argNodes);
    DUMP_PRIM(redirCount);
    DUMP_PRIM(inPipe);
    DUMP_PRIM(inLastPipe);
}

// ##########################
// ##     PipelineNode     ##
// ##########################

PipelineNode::~PipelineNode() {
    for(auto *p : this->nodes) {
        delete p;
    }
}

void PipelineNode::addNode(Node *node) {
    if(node->is(NodeKind::Pipeline)) {
        auto *pipe = static_cast<PipelineNode *>(node);
        for(auto &e : pipe->nodes) {
            this->addNodeImpl(e);
            e = nullptr;
        }
        delete pipe;
    } else {
        this->addNodeImpl(node);
    }
}

void PipelineNode::dump(NodeDumper &dumper) const {
    DUMP(nodes);
    DUMP_PRIM(baseIndex);
}

void PipelineNode::addNodeImpl(Node *node) {
    if(node->is(NodeKind::Cmd)) {
        static_cast<CmdNode *>(node)->setInPipe(true);
        static_cast<CmdNode *>(node)->setInLastPipe(true);
        if(!this->nodes.empty() && this->nodes.back()->is(NodeKind::Cmd)) {
            static_cast<CmdNode *>(this->nodes.back())->setInLastPipe(false);
        }
    }
    this->nodes.push_back(node);
    this->updateToken(node->getToken());
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

// ######################
// ##     WithNode     ##
// ######################

WithNode::~WithNode() {
    delete this->exprNode;

    for(auto &e : this->redirNodes) {
        delete e;
    }
}

void WithNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(exprNode);
    DUMP(redirNodes);
    DUMP_PRIM(baseIndex);
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

    auto *returnNode = EscapeNode::newReturn(exprNode->getToken(), exprNode);
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

// ######################
// ##     LoopNode     ##
// ######################

LoopNode::LoopNode(unsigned int startPos, Node *initNode,
                 Node *condNode, Node *iterNode, BlockNode *blockNode, bool asDoWhile) :
        Node(NodeKind::Loop, {startPos, 0}), initNode(initNode), condNode(condNode),
        iterNode(iterNode), blockNode(blockNode), asDoWhile(asDoWhile) {
    if(this->initNode == nullptr) {
        this->initNode = new EmptyNode();
    }

    if(this->iterNode == nullptr) {
        this->iterNode = new EmptyNode();
    }

    this->updateToken(this->asDoWhile ? this->condNode->getToken() : this->blockNode->getToken());
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

// ####################
// ##     IfNode     ##
// ####################

/**
 * if condNode is InstanceOfNode and targetNode is VarNode, insert VarDeclNode to blockNode.
 */
static void resolveIfIsStatement(Node *condNode, BlockNode *blockNode) {
    if(!condNode->is(NodeKind::TypeOp) || !static_cast<TypeOpNode *>(condNode)->isInstanceOfOp()) {
        return;
    }
    auto *isNode = static_cast<TypeOpNode *>(condNode);

    if(!isNode->getExprNode()->is(NodeKind::Var)) {
        return;
    }
    auto *varNode = static_cast<VarNode *>(isNode->getExprNode());

    VarNode *exprNode = new VarNode({isNode->getPos(), 1}, std::string(varNode->getVarName()));
    auto *castNode = new TypeOpNode(exprNode, isNode->getTargetTypeNode(), TypeOpNode::NO_CAST, true);
    VarDeclNode *declNode =
            new VarDeclNode(isNode->getPos(), std::string(varNode->getVarName()), castNode, VarDeclNode::CONST);
    blockNode->insertNodeToFirst(declNode);
}

IfNode::IfNode(unsigned int startPos, Node *condNode, Node *thenNode, Node *elseNode) :
        Node(NodeKind::If, {startPos, 0}), condNode(condNode), thenNode(thenNode), elseNode(elseNode) {

    if(this->thenNode->is(NodeKind::Block)) {
        resolveIfIsStatement(this->condNode, static_cast<BlockNode *>(this->thenNode));
    }
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

// ########################
// ##     EscapeNode     ##
// ########################

EscapeNode::EscapeNode(Token token, OpKind kind, Node *exprNode) :
        Node(NodeKind::Escape, token), opKind(kind), exprNode(exprNode) {
    if(this->exprNode == nullptr) {
        this->exprNode = new EmptyNode(token);
    } else {
        this->updateToken(this->exprNode->getToken());
    }
}

EscapeNode::~EscapeNode() {
    delete this->exprNode;
}

void EscapeNode::dump(NodeDumper &dumper) const {
#define EACH_ENUM(OP) \
    OP(BREAK) \
    OP(CONTINUE) \
    OP(THROW) \
    OP(RETURN)

    DUMP_ENUM(opKind, EACH_ENUM);
#undef EACH_ENUM

    DUMP_PTR(exprNode);
    DUMP_PRIM(leavingBlock);
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

// #########################
// ##     VarDeclNode     ##
// #########################

VarDeclNode::VarDeclNode(unsigned int startPos, std::string &&varName, Node *exprNode, Kind kind) :
        Node(NodeKind::VarDecl, {startPos, 0}), varName(std::move(varName)), global(false), kind(kind),
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

// ###################################
// ##     ElementSelfAssignNode     ##
// ###################################

ElementSelfAssignNode::ElementSelfAssignNode(MethodCallNode *leftNode, BinaryOpNode *binaryNode) :
        Node(NodeKind::ElementSelfAssign, leftNode->getToken()),
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

// #######################
// ##     EmptyNode     ##
// #######################

void EmptyNode::dump(NodeDumper &) const {
} // do nothing

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
    auto *init_var = new VarDeclNode(startPos, std::move(varName), call_next, VarDeclNode::VAR);

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
        if(leftNode->is(NodeKind::MethodCall) &&
                static_cast<MethodCallNode *>(leftNode)->hasAttribute(MethodCallNode::INDEX)) {
            auto *indexNode = static_cast<MethodCallNode *>(leftNode);
            indexNode->setMethodName(std::string(OP_SET));
            indexNode->refArgNodes().push_back(rightNode);
            return indexNode;
        }
        // assign to variable or field
        return new AssignNode(leftNode, rightNode);
    }

    /**
     * self assignment
     */
    // assign to element
    auto *opNode = new BinaryOpNode(new EmptyNode(rightNode->getToken()), resolveAssignOp(op), rightNode);
    if(leftNode->is(NodeKind::MethodCall) &&
            static_cast<MethodCallNode *>(leftNode)->hasAttribute(MethodCallNode::INDEX)) {
        auto *indexNode = static_cast<MethodCallNode *>(leftNode);
        return new ElementSelfAssignNode(indexNode, opNode);
    }
    // assign to variable or field
    return new AssignNode(leftNode, opNode, true);

}

Node *createIndexNode(Node *recvNode, Node *indexNode) {
    MethodCallNode *methodCallNode = new MethodCallNode(recvNode, std::string(OP_GET));
    methodCallNode->setAttribute(MethodCallNode::INDEX);
    methodCallNode->refArgNodes().push_back(indexNode);
    return methodCallNode;
}

// ########################
// ##     NodeDumper     ##
// ########################

void NodeDumper::dump(const char *fieldName, const char *value) {
    this->writeName(fieldName);

    fputc('"', this->fp);
    while(*value != 0) {
        char ch = *(value++);
        bool escape = true;
        switch(ch) {
        case '\t':
            ch = 't';
            break;
        case '\r':
            ch = 'r';
            break;
        case '\n':
            ch = 'n';
            break;
        case '"':
            ch = '"';
            break;
        case '\\':
            ch = '\\';
            break;
        default:
            escape = false;
            break;
        }
        if(escape) {
            fputc('\\', this->fp);
        }
        fputc(ch, this->fp);
    }
    fputs("\"\n", this->fp);
}

void NodeDumper::dump(const char *fieldName, const std::list<Node *> &nodes) {
    this->writeName(fieldName);
    this->newline();

    this->enterIndent();
    for(Node *node : nodes) {
        this->indent();
        fputs("- ", this->fp);
        this->dumpNodeHeader(*node, true);
        this->enterIndent();
        node->dump(*this);
        this->leaveIndent();
    }
    this->leaveIndent();
}

void NodeDumper::dump(const char *fieldName, const Node &node) {
    // write field name
    this->writeName(fieldName);

    // write node body
    this->newline();
    this->enterIndent();
    this->dump(node);
    this->leaveIndent();
}

void NodeDumper::dump(const char *fieldName, const DSType &type) {
    this->dump(fieldName, this->pool.getTypeName(type));
}

void NodeDumper::dump(const char *fieldName, TokenKind kind) {
    this->dump(fieldName, toString(kind));
}

void NodeDumper::dumpNull(const char *fieldName) {
    this->writeName(fieldName);
    this->newline();
}

void NodeDumper::dump(const Node &node) {
    this->indent();
    this->dumpNodeHeader(node);
    node.dump(*this);
    fflush(this->fp);
}

void NodeDumper::indent() {
    for(unsigned int i = 0; i < this->indentLevel; i++) {
        fputs("  ", this->fp);
    }
}

static const char *toString(NodeKind kind) {
    const char *table[] = {
#define GEN_STR(E) #E,
            EACH_NODE_KIND(GEN_STR)
#undef GEN_STR
    };
    return table[static_cast<unsigned char>(kind)];
}

void NodeDumper::dumpNodeHeader(const Node &node, bool inArray) {
    fprintf(this->fp, "nodeKind: %s\n", toString(node.getNodeKind()));

    if(inArray) {
        this->enterIndent();
    }

    this->indent(); fprintf(this->fp, "token: \n");
    this->enterIndent();
    this->indent(); fprintf(this->fp, "pos: %d\n", node.getPos());
    this->indent(); fprintf(this->fp, "size: %d\n", node.getSize());
    this->leaveIndent();
    this->indent(); fprintf(this->fp, "type: %s\n",
                            (!node.isUntyped() ? this->pool.getTypeName(node.getType()).c_str() : ""));

    if(inArray) {
        this->leaveIndent();
    }
}

void NodeDumper::dumpNodes(const char *fieldName, Node * const * begin, Node *const * end) {
    this->writeName(fieldName);
    this->newline();

    this->enterIndent();
    for(; begin != end; ++begin) {
        Node *node = *begin;

        this->indent();
        fputs("- ", this->fp);
        this->dumpNodeHeader(*node, true);
        this->enterIndent();
        node->dump(*this);
        this->leaveIndent();
    }
    this->leaveIndent();
}

void NodeDumper::writeName(const char *fieldName) {
    this->indent(); fprintf(this->fp, "%s: ", fieldName);
}

void NodeDumper::operator()(const RootNode &rootNode) {
    this->dump(rootNode);
}

void NodeDumper::dump(FILE *fp, TypePool &pool, const RootNode &rootNode) {
    NodeDumper writer(fp, pool);
    writer(rootNode);
    fflush(fp);
}


} // namespace ydsh