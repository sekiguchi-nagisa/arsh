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

#include "../core/symbol.h"
#include "../core/object.h"
#include "../core/context.h"
#include "node_dumper.h"

// helper macro
#define EVAL(ctx, node) \
    do {\
        EvalStatus status = (node)->eval(ctx);\
        if(status != EvalStatus::SUCCESS) {\
            return status;\
        }\
    } while(false)

namespace ydsh {
namespace ast {

// ##################
// ##     Node     ##
// ##################

void Node::updateToken(Token token) {
    if(token.pos > this->token.pos) {
        this->token.size = token.pos + token.size - this->token.pos;
    }
}

void Node::setType(DSType &type) {
    this->type = &type;
}

// ######################
// ##     TypeNode     ##
// ######################

EvalStatus TypeNode::eval(RuntimeContext &) {
    fatal("evaluation of TypeNode is unsupported\n");
}

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
    DUMP_NODES(elementTypeNodes);
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
    DUMP_NODES(paramTypeNodes);
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
    DUMP_NODES(typeNodes);
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

void IntValueNode::setType(DSType &type) {
    this->type = &type;
    this->value = DSValue::create<Int_Object>(*this->type, this->tempValue);
}

void IntValueNode::dump(NodeDumper &dumper) const {
    DUMP_PRIM(tempValue);
    if(this->type == nullptr) {
        dumper.dump(NAME(value), "(null)");
    } else {
        Int_Object *obj = typeAs<Int_Object>(this->value);
        dumper.dump(NAME(value), std::to_string(obj->getValue()));
    }
}

void IntValueNode::accept(NodeVisitor &visitor) {
    visitor.visitIntValueNode(*this);
}

EvalStatus IntValueNode::eval(RuntimeContext &ctx) {
    ctx.push(this->value);
    return EvalStatus::SUCCESS;
}

// ###########################
// ##     LongValueNode     ##
// ###########################

void LongValueNode::setType(DSType &type) {
    this->type = &type;
    this->value = DSValue::create<Long_Object>(*this->type, this->tempValue);
}

void LongValueNode::dump(NodeDumper &dumper) const {
    DUMP_PRIM(tempValue);
    DUMP_PRIM(unsignedValue);
    if(this->type == nullptr) {
        dumper.dump(NAME(value), "(null)");
    } else {
        Long_Object *obj = typeAs<Long_Object>(this->value);
        dumper.dump(NAME(value), std::to_string(obj->getValue()));
    }
}

void LongValueNode::accept(NodeVisitor &visitor) {
    visitor.visitLongValueNode(*this);
}

EvalStatus LongValueNode::eval(RuntimeContext &ctx) {
    ctx.push(this->value);
    return EvalStatus::SUCCESS;
}


// ############################
// ##     FloatValueNode     ##
// ############################

void FloatValueNode::setType(DSType &type) {
    this->type = &type;
    this->value = DSValue::create<Float_Object>(*this->type, this->tempValue);
}

void FloatValueNode::dump(NodeDumper &dumper) const {
    DUMP_PRIM(tempValue);
    if(this->type == nullptr) {
        dumper.dump(NAME(value), "(null)");
    } else {
        Float_Object *obj = typeAs<Float_Object>(this->value);
        dumper.dump(NAME(value), std::to_string(obj->getValue()));
    }
}

void FloatValueNode::accept(NodeVisitor &visitor) {
    visitor.visitFloatValueNode(*this);
}

EvalStatus FloatValueNode::eval(RuntimeContext &ctx) {
    ctx.push(this->value);
    return EvalStatus::SUCCESS;
}

// ############################
// ##     StringValueNode    ##
// ############################

void StringValueNode::setType(DSType &type) {
    this->type = &type;
    this->value = DSValue::create<String_Object>(*this->type, std::move(this->tempValue));
}

void StringValueNode::dump(NodeDumper &dumper) const {
    if(this->type == nullptr) {
        dumper.dump(NAME(tempValue), this->tempValue);
        dumper.dump(NAME(value), "");

    } else {
        String_Object *obj = typeAs<String_Object>(this->value);
        dumper.dump(NAME(tempValue), "");
        dumper.dump(NAME(value), obj->getValue());
    }
}

void StringValueNode::accept(NodeVisitor &visitor) {
    visitor.visitStringValueNode(*this);
}

EvalStatus StringValueNode::eval(RuntimeContext &ctx) {
    ctx.push(this->value);
    return EvalStatus::SUCCESS;
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
    this->nodes.push_back(node);
}

void StringExprNode::dump(NodeDumper &dumper) const {
    DUMP(nodes);
}

void StringExprNode::accept(NodeVisitor &visitor) {
    visitor.visitStringExprNode(*this);
}

EvalStatus StringExprNode::eval(RuntimeContext &ctx) {
    unsigned int size = this->nodes.size();
    if(size == 0) {
        ctx.push(ctx.getEmptyStrObj());
    } else if(size == 1) {
        EVAL(ctx, this->nodes[0]);
    } else {
        std::string str;
        for(Node *node : this->nodes) {
            EVAL(ctx, node);
            str += typeAs<String_Object>(ctx.pop())->getValue();
        }
        ctx.push(DSValue::create<String_Object>(*this->type, std::move(str)));
    }
    return EvalStatus::SUCCESS;
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

EvalStatus ArrayNode::eval(RuntimeContext &ctx) {
    auto value = DSValue::create<Array_Object>(*this->type);
    for(Node *node : this->nodes) {
        EVAL(ctx, node);
        typeAs<Array_Object>(value)->append(ctx.pop());
    }
    ctx.push(std::move(value));
    return EvalStatus::SUCCESS;
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

EvalStatus MapNode::eval(RuntimeContext &ctx) {
    auto map = DSValue::create<Map_Object>(*this->type);
    unsigned int size = this->keyNodes.size();
    for(unsigned int i = 0; i < size; i++) {
        EVAL(ctx, this->keyNodes[i]);
        auto key = ctx.pop();
        EVAL(ctx, this->valueNodes[i]);
        auto value = ctx.pop();
        typeAs<Map_Object>(map)->set(key, value);
    }
    ctx.push(std::move(map));
    return EvalStatus::SUCCESS;
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

EvalStatus TupleNode::eval(RuntimeContext &ctx) {
    unsigned int size = this->nodes.size();
    auto value = DSValue::create<Tuple_Object>(*this->type);
    for(unsigned int i = 0; i < size; i++) {
        EVAL(ctx, this->nodes[i]);
        typeAs<Tuple_Object>(value)->set(i, ctx.pop());
    }
    ctx.push(std::move(value));
    return EvalStatus::SUCCESS;
}

// ############################
// ##     AssignableNode     ##
// ############################

void AssignableNode::setAttribute(FieldHandle *handle) {
    this->index = handle->getFieldIndex();
    this->readOnly = handle->isReadOnly();
    this->global = handle->isGlobal();
    this->env = handle->isEnv();
    this->interface = handle->withinInterface();
}

void AssignableNode::dump(NodeDumper &dumper) const {
    DUMP_PRIM(index);
    DUMP_PRIM(readOnly);
    DUMP_PRIM(global);
    DUMP_PRIM(env);
    DUMP_PRIM(interface);
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

EvalStatus VarNode::eval(RuntimeContext &ctx) {
    if(this->isEnv()) {
        ctx.loadEnv(this->getIndex(), this->isGlobal());
    } else {
        if(this->isGlobal()) {
            ctx.loadGlobal(this->getIndex());
        } else {
            ctx.loadLocal(this->getIndex());
        }
        if(this->type != nullptr && this->type->isFuncType()) {
            ctx.peek()->setType(this->type);
        }
    }
    return EvalStatus::SUCCESS;
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

EvalStatus AccessNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->recvNode);

    switch(this->additionalOp) {
    case NOP: {
        if(this->withinInterface()) {
            return ctx.loadField(&this->recvNode->getType(), this->fieldName, this->type);
        }

        ctx.loadField(this->getIndex());
        break;
    }
    case DUP_RECV: {
        if(this->withinInterface()) {
            return ctx.dupAndLoadField(&this->recvNode->getType(), this->fieldName, this->type);
        }

        ctx.dupAndLoadField(this->getIndex());
        break;
    }
    }

    return EvalStatus::SUCCESS;
}

std::pair<Node *, std::string> AccessNode::split(AccessNode *accessNode) {
    Node *node = accessNode->recvNode;
    accessNode->recvNode = nullptr;

    std::pair<Node *, std::string> pair(node, std::move(accessNode->fieldName));
    delete accessNode;
    return pair;
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

EvalStatus CastNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->exprNode);

    switch(this->opKind) {
    case NO_CAST:
        break;
    case TO_VOID:
        ctx.popNoReturn();
        break;
    case NUM_CAST: {
        for(int i = 15; i > -1; i--) {  //FIXME: split cast node
            unsigned short flag = (1 << i);
            if(!hasFlag(this->numCastOp, flag)) {
                continue;
            }
            NumberCastOp op = static_cast<NumberCastOp>(flag);
            switch(op) {
            case NOP:
                break;  // do nothing
            case COPY_INT: {
                int v = typeAs<Int_Object>(ctx.pop())->getValue();
                ctx.push(DSValue::create<Int_Object>(*this->type, v));
                break;
            }
            case TO_B: {
                unsigned int v = typeAs<Int_Object>(ctx.pop())->getValue();
                v &= 0xFF;  // fill higher bits (8th ~ 31) with 0
                ctx.push(DSValue::create<Int_Object>(*this->type, v));
                break;
            }
            case TO_U16: {
                unsigned int v = typeAs<Int_Object>(ctx.pop())->getValue();
                v &= 0xFFFF;    // fill higher bits (16th ~ 31th) with 0
                ctx.push(DSValue::create<Int_Object>(*this->type, v));
                break;
            }
            case TO_I16: {
                unsigned int v = typeAs<Int_Object>(ctx.pop())->getValue();
                v &= 0xFFFF;    // fill higher bits (16th ~ 31th) with 0
                if(v & 0x8000) {    // if 15th bit is 1, fill higher bits with 1
                    v |= 0xFFFF0000;
                }
                ctx.push(DSValue::create<Int_Object>(*this->type, v));
                break;
            }
            case NEW_LONG: {
                unsigned int v = typeAs<Int_Object>(ctx.pop())->getValue();
                unsigned long l = v;
                ctx.push(DSValue::create<Long_Object>(*this->type, l));
                break;
            }
            case COPY_LONG: {
                long v = typeAs<Long_Object>(ctx.pop())->getValue();
                ctx.push(DSValue::create<Long_Object>(*this->type, v));
                break;
            }
            case I_NEW_LONG: {
                int v = typeAs<Int_Object>(ctx.pop())->getValue();
                long l = v;
                ctx.push(DSValue::create<Long_Object>(*this->type, l));
                break;
            }
            case NEW_INT: {
                unsigned long l = typeAs<Long_Object>(ctx.pop())->getValue();
                unsigned int v = static_cast<unsigned int>(l);
                ctx.push(DSValue::create<Int_Object>(*this->type, v));
                break;
            }
            case U32_TO_D: {
                unsigned int v = typeAs<Int_Object>(ctx.pop())->getValue();
                double d = static_cast<double>(v);
                ctx.push(DSValue::create<Float_Object>(*this->type, d));
                break;
            }
            case I32_TO_D: {
                int v = typeAs<Int_Object>(ctx.pop())->getValue();
                double d = static_cast<double>(v);
                ctx.push(DSValue::create<Float_Object>(*this->type, d));
                break;
            }
            case U64_TO_D: {
                unsigned long v = typeAs<Long_Object>(ctx.pop())->getValue();
                double d = static_cast<double>(v);
                ctx.push(DSValue::create<Float_Object>(*this->type, d));
                break;
            }
            case I64_TO_D: {
                long v = typeAs<Long_Object>(ctx.pop())->getValue();
                double d = static_cast<double>(v);
                ctx.push(DSValue::create<Float_Object>(*this->type, d));
                break;
            }
            case D_TO_U32: {
                double d = typeAs<Float_Object>(ctx.pop())->getValue();
                unsigned int v = static_cast<unsigned int>(d);
                ctx.push(DSValue::create<Int_Object>(*this->type, v));
                break;
            }
            case D_TO_I32: {
                double d = typeAs<Float_Object>(ctx.pop())->getValue();
                int v = static_cast<int>(d);
                ctx.push(DSValue::create<Int_Object>(*this->type, v));
                break;
            }
            case D_TO_U64: {
                double d = typeAs<Float_Object>(ctx.pop())->getValue();
                unsigned long v = static_cast<unsigned long>(d);
                ctx.push(DSValue::create<Long_Object>(*this->type, v));
                break;
            }
            case D_TO_I64:
                double d = typeAs<Float_Object>(ctx.pop())->getValue();
                long v = static_cast<long>(d);
                ctx.push(DSValue::create<Long_Object>(*this->type, v));
                break;
            }
        }
        break;
    }
    case TO_STRING: {
        return ctx.toString(this->getStartPos());
    }
    case CHECK_CAST: {
        return ctx.checkCast(this->getStartPos(), this->type) ? EvalStatus::SUCCESS : EvalStatus::THROW;
    }
    }
    return EvalStatus::SUCCESS;
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

EvalStatus InstanceOfNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->targetNode);

    switch(this->opKind) {
    case INSTANCEOF:
        ctx.instanceOf(&this->targetTypeNode->getType());
        break;
    case ALWAYS_TRUE:
        ctx.popNoReturn();
        ctx.push(ctx.getTrueObj());
        break;
    case ALWAYS_FALSE:
        ctx.popNoReturn();
        ctx.push(ctx.getFalseObj());
        break;
    }
    return EvalStatus::SUCCESS;
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

EvalStatus PrintNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->exprNode);
    auto s = ctx.toString(this->exprNode->getStartPos());
    if(s != EvalStatus::SUCCESS) {
        return s;
    }
    ctx.printStackTop(&this->exprNode->getType());
    return EvalStatus::SUCCESS;
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

/**
 * stack state in function applyFuncObject    stack grow ===>
 *
 * +-----------+---------+------------------+   +--------+
 * | stack top | funcObj | param1(receiver) | ~ | paramN |
 * +-----------+---------+------------------+   +--------+
 *                       |    new offset    |   |        |
 */
EvalStatus ApplyNode::eval(RuntimeContext &ctx) {
    unsigned int actualParamSize = this->getArgNodes().size();

    // push func object
    EVAL(ctx, this->exprNode);

    // push arguments
    for(Node *argNode : this->argNodes) {
        EVAL(ctx, argNode);
    }

    // call function
    return ctx.applyFuncObject(this->getStartPos(), this->type->isVoidType(), actualParamSize);
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

EvalStatus MethodCallNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->recvNode);

    // push arguments
    for(Node *argNode : this->argNodes) {
        EVAL(ctx, argNode);
    }

    return ctx.callMethod(this->getStartPos(), this->methodName, this->handle);
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

EvalStatus NewNode::eval(RuntimeContext &ctx) {
    unsigned int paramSize = this->argNodes.size();

    ctx.newDSObject(this->type);

    // push arguments
    for(Node *argNode : this->argNodes) {
        EVAL(ctx, argNode);
    }

    // call constructor
    return ctx.callConstructor(this->getStartPos(), paramSize);
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

EvalStatus UnaryOpNode::eval(RuntimeContext &ctx) {
    return this->methodCallNode->eval(ctx);
}


// ##########################
// ##     BinaryOpNode     ##
// ##########################

BinaryOpNode::~BinaryOpNode() {
    delete this->leftNode;
    delete this->rightNode;
    delete this->methodCallNode;
}

MethodCallNode *BinaryOpNode::createApplyNode() {
    this->methodCallNode = new MethodCallNode(this->leftNode, resolveBinaryOpName(this->op));
    this->methodCallNode->refArgNodes().push_back(this->rightNode);

    // assign null to prevent double free.
    this->leftNode = nullptr;
    this->rightNode = nullptr;

    return this->methodCallNode;
}

void BinaryOpNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(leftNode);
    DUMP_PTR(rightNode);
    dumper.dump(NAME(op), TO_NAME(op));
    DUMP_PTR(methodCallNode);
}

void BinaryOpNode::accept(NodeVisitor &visitor) {
    visitor.visitBinaryOpNode(*this);
}

EvalStatus BinaryOpNode::eval(RuntimeContext &ctx) {
    return this->methodCallNode->eval(ctx);
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

EvalStatus CondOpNode::eval(RuntimeContext &ctx) {
    // eval left node
    EVAL(ctx, this->leftNode);

    if(this->andOp) {   // and
        if(typeAs<Boolean_Object>(ctx.peek())->getValue()) {
            ctx.popNoReturn();
            return this->rightNode->eval(ctx);
        } else {
            return EvalStatus::SUCCESS;
        }
    } else {    // or
        if(typeAs<Boolean_Object>(ctx.peek())->getValue()) {
            return EvalStatus::SUCCESS;
        } else {
            ctx.popNoReturn();
            return this->rightNode->eval(ctx);
        }
    }
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

EvalStatus TernaryNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->condNode);

    if(typeAs<Boolean_Object>(ctx.pop())->getValue()) {
        return this->leftNode->eval(ctx);
    } else {
        return this->rightNode->eval(ctx);
    }
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

EvalStatus CmdArgNode::eval(RuntimeContext &ctx) {
    EvalStatus s = this->evalImpl(ctx);
    if(s != EvalStatus::SUCCESS) {
        return s;
    }

    ctx.addArg(this->isIgnorableEmptyString());
    return EvalStatus::SUCCESS;
}

EvalStatus CmdArgNode::evalImpl(RuntimeContext &ctx) {
    if(this->segmentNodes.size() == 1) {
        EVAL(ctx, this->segmentNodes[0]);
        return EvalStatus::SUCCESS;
    }

    std::string str;
    unsigned int index = 0;
    unsigned int size = this->segmentNodes.size();
    if(dynamic_cast<TildeNode *>(this->segmentNodes[0]) != nullptr) {
        str += static_cast<TildeNode *>(this->segmentNodes[0])->expand(false);
        index++;
    }

    for(; index < size; index++) {
        EVAL(ctx, this->segmentNodes[index]);
        str += typeAs<String_Object>(ctx.pop())->getValue();
    }
    ctx.push(DSValue::create<String_Object>(ctx.getPool().getStringType(), std::move(str)));
    return EvalStatus::SUCCESS;
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
    static const char *redirOpStr[] = {
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

EvalStatus RedirNode::eval(RuntimeContext &ctx) {
    EvalStatus s = this->targetNode->evalImpl(ctx);
    if(s != EvalStatus::SUCCESS) {
        return s;
    }
    ctx.addRedirOption(this->op);
    return EvalStatus::SUCCESS;
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

std::string TildeNode::expand(bool isLastSegment) {
    if(!isLastSegment && strchr(this->value.c_str(), '/') == nullptr) {
        return this->value;
    }
    return expandTilde(this->value.c_str());
}

EvalStatus TildeNode::eval(RuntimeContext &ctx) {
    ctx.push(DSValue::create<String_Object>(ctx.getPool().getStringType(), this->expand()));
    return EvalStatus::SUCCESS;
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

EvalStatus CmdNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->nameNode);

    ctx.openProc();

    for(Node *node : this->argNodes) {
        EVAL(ctx, node);
    }

    ctx.closeProc();
    return EvalStatus::SUCCESS;
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

EvalStatus PipedCmdNode::eval(RuntimeContext &ctx) {
    ctx.pushNewPipeline();

    for(auto &node : this->cmdNodes) {
        EVAL(ctx, node);
    }

    EvalStatus status = ctx.callPipedCommand(this->getStartPos());

    // push exit status as boolean
    if(ctx.getExitStatus() == 0) {
        ctx.push(ctx.getTrueObj());
    } else {
        ctx.push(ctx.getFalseObj());
    }

    return status;
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

static bool isSpace(int ch) {
    return ch == ' ' || ch == '\t' || ch == '\n';
}

static bool isFieldSep(const char *ifs, int ch) {
    for(unsigned int i = 0; ifs[i] != '\0'; i++) {
        if(ifs[i] == ch) {
            return true;
        }
    }
    return false;
}

static bool hasSpace(const char *ifs) {
    for(unsigned int i = 0; ifs[i] != '\0'; i++) {
        if(isSpace(ifs[i])) {
            return true;
        }
    }
    return false;
}

EvalStatus SubstitutionNode::eval(RuntimeContext &ctx) {
    // capture stdout
    pid_t pipefds[2];

    if(pipe(pipefds) < 0) {
        perror("pipe creation failed\n");
        exit(1);    //FIXME: throw exception
    }

    pid_t pid = xfork();
    if(pid > 0) {   // parent process
        close(pipefds[WRITE_PIPE]);

        DSValue obj;

        if(this->strExpr) {  // capture stdout as String
            static const int bufSize = 256;
            char buf[bufSize + 1];
            std::string str;
            while(true) {
                int readSize = read(pipefds[READ_PIPE], buf, bufSize);
                if(readSize == -1 && (errno == EAGAIN || errno == EINTR)) {
                    continue;
                }
                if(readSize <= 0) {
                    break;
                }
                buf[readSize] = '\0';
                str += buf;
            }

            // remove last newlines
            std::string::size_type pos = str.find_last_not_of('\n');
            if(pos == std::string::npos) {
                str.clear();
            } else {
                str.erase(pos + 1);
            }

            obj = DSValue::create<String_Object>(*this->type, std::move(str));
        } else {    // capture stdout as String Array
            const char *ifs = ctx.getIFS();
            unsigned int skipCount = 1;

            static const int bufSize = 256;
            char buf[bufSize];
            std::string str;
            obj = DSValue::create<Array_Object>(*this->type);
            Array_Object *array = typeAs<Array_Object>(obj);

            while(true) {
                int readSize = read(pipefds[READ_PIPE], buf, bufSize);
                if(readSize == -1 && (errno == EINTR || errno == EAGAIN)) {
                    continue;
                }
                if(readSize <= 0) {
                    break;
                }

                for(int i = 0; i < readSize; i++) {
                    char ch = buf[i];
                    bool fieldSep = isFieldSep(ifs, ch);
                    if(fieldSep && skipCount > 0) {
                        if(isSpace(ch)) {
                            continue;
                        }
                        if(--skipCount == 1) {
                            continue;
                        }
                    }
                    skipCount = 0;
                    if(fieldSep) {
                        array->append(DSValue::create<String_Object>(
                                ctx.getPool().getStringType(), std::move(str)));
                        str = "";
                        skipCount = isSpace(ch) ? 2 : 1;
                        continue;
                    }
                    str += ch;
                }
            }

            // remove last newline
            while(!str.empty() && str.back() == '\n') {
                str.pop_back();
            }

            // append remain
            if(!str.empty() || !hasSpace(ifs)) {
                array->append(DSValue::create<String_Object>(
                        ctx.getPool().getStringType(), std::move(str)));
            }
        }
        close(pipefds[READ_PIPE]);

        // wait exit
        int status;
        ctx.xwaitpid(pid, status, 0);
        if(WIFEXITED(status)) {
            ctx.updateExitStatus(WEXITSTATUS(status));
        }
        if(WIFSIGNALED(status)) {
            ctx.updateExitStatus(WTERMSIG(status));
        }

        // push object
        ctx.push(std::move(obj));
        return EvalStatus::SUCCESS;
    } else if(pid == 0) {   // child process
        dup2(pipefds[WRITE_PIPE], STDOUT_FILENO);
        close(pipefds[READ_PIPE]);
        close(pipefds[WRITE_PIPE]);

        if(this->exprNode->eval(ctx) == EvalStatus::THROW) {
            if(ctx.getPool().getErrorType().isSameOrBaseTypeOf(*ctx.getThrownObject()->getType())) {
                ctx.reportError();
            }
            exit(1);
        } //FIXME: propagate error

        exit(ctx.getExitStatus());
    } else {
        perror("fork failed");
        exit(1);    //FIXME: throw exception
    }
}

// ########################
// ##     AssertNode     ##
// ########################

AssertNode::~AssertNode() {
    delete this->condNode;
}

void AssertNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(condNode);
}

void AssertNode::accept(NodeVisitor &visitor) {
    visitor.visitAssertNode(*this);
}

EvalStatus AssertNode::eval(RuntimeContext &ctx) {
    if(ctx.isAssertion()) {
        EVAL(ctx, this->condNode);
        return ctx.checkAssertion(this->condNode->getStartPos());
    }
    return EvalStatus::SUCCESS;
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

void BlockNode::dump(NodeDumper &dumper) const {
    DUMP(nodeList);
}

void BlockNode::accept(NodeVisitor &visitor) {
    visitor.visitBlockNode(*this);
}

EvalStatus BlockNode::eval(RuntimeContext &ctx) {
    for(Node *node : this->nodeList) {
        EvalStatus status = node->eval(ctx);
        if(status != EvalStatus::SUCCESS) {
            return status;
        }
    }
    return EvalStatus::SUCCESS;
}

// #######################
// ##     BreakNode     ##
// #######################

void BreakNode::dump(NodeDumper &) const {
} // do nothing

void BreakNode::accept(NodeVisitor &visitor) {
    visitor.visitBreakNode(*this);
}

EvalStatus BreakNode::eval(RuntimeContext &) {
    return EvalStatus::BREAK;
}

// ##########################
// ##     ContinueNode     ##
// ##########################

void ContinueNode::dump(NodeDumper &) const {
} // do nothing

void ContinueNode::accept(NodeVisitor &visitor) {
    visitor.visitContinueNode(*this);
}

EvalStatus ContinueNode::eval(RuntimeContext &) {
    return EvalStatus::CONTINUE;
}

// ###########################
// ##     ExportEnvNode     ##
// ###########################

ExportEnvNode::~ExportEnvNode() {
    delete this->exprNode;
}

void ExportEnvNode::setAttribute(FieldHandle *handle) {
    this->global = handle->isGlobal();
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

EvalStatus ExportEnvNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->exprNode);
    ctx.exportEnv(this->envName, this->varIndex, this->global);
    return EvalStatus::SUCCESS;
}

// ###########################
// ##     ImportEnvNode     ##
// ###########################

ImportEnvNode::~ImportEnvNode() {
    delete this->defaultValueNode;
}

void ImportEnvNode::setAttribute(FieldHandle *handle) {
    this->global = handle->isGlobal();
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

EvalStatus ImportEnvNode::eval(RuntimeContext &ctx) {
    bool hasDefault = this->getDefaultValueNode() != nullptr;
    if(hasDefault) {
        EVAL(ctx, this->defaultValueNode);
    }
    return ctx.importEnv(this->getStartPos(), this->envName, this->varIndex, this->global, hasDefault);
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

EvalStatus TypeAliasNode::eval(RuntimeContext &) {
    return EvalStatus::SUCCESS;    // do nothing.
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

EvalStatus ForNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->initNode);

    CONTINUE:
    EVAL(ctx, this->condNode);
    if(typeAs<Boolean_Object>(ctx.pop())->getValue()) {
        EvalStatus status = this->blockNode->eval(ctx);
        switch(status) {
        case EvalStatus::BREAK:
            break;
        case EvalStatus::SUCCESS:
        case EvalStatus::CONTINUE:
            EVAL(ctx, this->iterNode);
            goto CONTINUE;
        default:
            return status;
        }
    }

    return EvalStatus::SUCCESS;
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

EvalStatus WhileNode::eval(RuntimeContext &ctx) {
    CONTINUE:
    EVAL(ctx, this->condNode);
    if(typeAs<Boolean_Object>(ctx.pop())->getValue()) {
        EvalStatus status = this->blockNode->eval(ctx);
        switch(status) {
        case EvalStatus::BREAK:
            break;
        case EvalStatus::SUCCESS:
        case EvalStatus::CONTINUE:
            goto CONTINUE;
        default:
            return status;
        }
    }

    return EvalStatus::SUCCESS;
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

EvalStatus DoWhileNode::eval(RuntimeContext &ctx) {
    CONTINUE:
    EvalStatus status = this->blockNode->eval(ctx);
    switch(status) {
    case EvalStatus::BREAK:
        goto BREAK;
    case EvalStatus::SUCCESS:
    case EvalStatus::CONTINUE:
        break;
    default:
        return status;
    }

    EVAL(ctx, this->condNode);
    if(typeAs<Boolean_Object>(ctx.pop())->getValue()) {
        goto CONTINUE;
    }

    BREAK:
    return EvalStatus::SUCCESS;
}

// ####################
// ##     IfNode     ##
// ####################

/**
 * if condNode is InstanceOfNode and targetNode is VarNode, insert VarDeclNode to blockNode.
 */
static void resolveIfIsStatement(Node *condNode, BlockNode *blockNode) {
    InstanceOfNode *isNode = dynamic_cast<InstanceOfNode *>(condNode);
    if(isNode == nullptr) {
        return;
    }

    VarNode *varNode = dynamic_cast<VarNode *>(isNode->getTargetNode());
    if(varNode == nullptr) {
        return;
    }

    VarNode *exprNode = new VarNode({isNode->getStartPos(), 1}, std::string(varNode->getVarName()));
    CastNode *castNode = new CastNode(exprNode, isNode->getTargetTypeNode(), true);
    VarDeclNode *declNode =
            new VarDeclNode(isNode->getStartPos(), std::string(varNode->getVarName()), castNode, true);
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

EvalStatus IfNode::eval(RuntimeContext &ctx) {
    // if cond
    EVAL(ctx, this->condNode);

    // then block
    if(typeAs<Boolean_Object>(ctx.pop())->getValue()) {
        return this->thenNode->eval(ctx);   // then block
    } else {
        return this->elseNode->eval(ctx);   // else
    }
}

// ########################
// ##     ReturnNode     ##
// ########################

ReturnNode::ReturnNode(Token token) :
        BlockEndNode(token), exprNode(new EmptyNode(token)) { }

ReturnNode::~ReturnNode() {
    delete this->exprNode;
}

void ReturnNode::dump(NodeDumper &dumper) const {
    DUMP_PTR(exprNode);
}

void ReturnNode::accept(NodeVisitor &visitor) {
    visitor.visitReturnNode(*this);
}

EvalStatus ReturnNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->exprNode);
    return EvalStatus::RETURN;
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

EvalStatus ThrowNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->exprNode);
    ctx.throwException();
    return EvalStatus::THROW;
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

EvalStatus CatchNode::eval(RuntimeContext &ctx) {
    ctx.storeLocal(this->varIndex);
    EVAL(ctx, this->blockNode);
    return EvalStatus::SUCCESS;
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
    DUMP_NODES(catchNodes);

    DUMP_PTR(finallyNode);
}

void TryNode::accept(NodeVisitor &visitor) {
    visitor.visitTryNode(*this);
}

EvalStatus TryNode::eval(RuntimeContext &ctx) {
    // eval try block
    EvalStatus status = this->blockNode->eval(ctx);

    if(status != EvalStatus::THROW) {  // eval finally
        if(this->finallyNode != nullptr) {
            EVAL(ctx, this->finallyNode);
        }
        return status;
    } else {   // eval catch
        DSType &thrownType = *ctx.getThrownObject()->getType();
        for(CatchNode *catchNode : this->catchNodes) {
            if(catchNode->getTypeNode()->getType().isSameOrBaseTypeOf(thrownType)) {
                ctx.loadThrownObject();
                status = catchNode->eval(ctx);
                break;
            }
        }
        // eval finally
        if(this->finallyNode != nullptr) {
            EVAL(ctx, this->finallyNode);
        }
    }
    return status;
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
    this->global = handle->isGlobal();
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

EvalStatus VarDeclNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->initValueNode);
    if(this->global) {
        ctx.storeGlobal(this->varIndex);
    } else {
        ctx.storeLocal(this->varIndex);
    }
    return EvalStatus::SUCCESS;
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

EvalStatus AssignNode::eval(RuntimeContext &ctx) {
    AssignableNode *assignableNode = static_cast<AssignableNode *>(this->leftNode);
    unsigned int index = assignableNode->getIndex();
    if(this->isFieldAssign()) {
        if(this->isSelfAssignment()) {
            EVAL(ctx, this->leftNode);
        } else {
            AccessNode *accessNode = static_cast<AccessNode *>(this->leftNode);
            EVAL(ctx, accessNode->getRecvNode());
        }
        EVAL(ctx, this->rightNode);

        if(assignableNode->withinInterface()) {
            AccessNode *accessNode = static_cast<AccessNode *>(this->leftNode);
            return ctx.storeField(&accessNode->getRecvNode()->getType(),
                                  accessNode->getFieldName(), &accessNode->getType());
        }
        ctx.storeField(index);
    } else {
        if(this->isSelfAssignment()) {
            EVAL(ctx, this->leftNode);
        }
        EVAL(ctx, this->rightNode);
        VarNode *varNode = static_cast<VarNode *>(this->leftNode);

        if(varNode->isEnv()) {
            ctx.storeEnv(index, varNode->isGlobal());
        } else {
            if(varNode->isGlobal()) {
                ctx.storeGlobal(index);
            } else {
                ctx.storeLocal(index);
            }
        }
    }
    return EvalStatus::SUCCESS;
}

std::pair<Node *, Node *> AssignNode::split(AssignNode *node) {
    Node *leftNode = node->leftNode;
    node->leftNode = nullptr;

    Node *rightNode = node->rightNode;
    node->rightNode = nullptr;

    delete node;
    return std::make_pair(leftNode, rightNode);
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
    this->getterNode = new MethodCallNode(new DummyNode(), std::string(OP_GET));
    this->getterNode->refArgNodes().push_back(new DummyNode());

    // init setter node
    this->setterNode = new MethodCallNode(new DummyNode(), std::string(OP_SET));
    this->setterNode->refArgNodes().push_back(new DummyNode());
    this->setterNode->refArgNodes().push_back(new DummyNode());
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

EvalStatus ElementSelfAssignNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->recvNode);
    EVAL(ctx, this->indexNode);
    ctx.dup2();

    EVAL(ctx, this->getterNode);
    EVAL(ctx, this->rightNode);

    return this->setterNode->eval(ctx);
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
    DUMP_NODES(paramNodes);
    DUMP_NODES(paramTypeNodes);

    DUMP_PTR(returnTypeNode);
    DUMP_PTR(blockNode);
    DUMP_PRIM(maxVarNum);
    DUMP_PRIM(varIndex);
    dumper.dump("sourceName", this->srcInfoPtr->getSourceName());
}

void FunctionNode::accept(NodeVisitor &visitor) {
    visitor.visitFunctionNode(*this);
}

EvalStatus FunctionNode::eval(RuntimeContext &ctx) {
    ctx.setGlobal(this->varIndex, DSValue::create<OldFuncObject>(this));
    return EvalStatus::REMOVE;
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
    DUMP_NODES(methodDeclNodes);
    DUMP_NODES(fieldDeclNodes);
    DUMP_NODES(fieldTypeNodes);
}

void InterfaceNode::accept(NodeVisitor &visitor) {
    visitor.visitInterfaceNode(*this);
}

EvalStatus InterfaceNode::eval(RuntimeContext &) {
    return EvalStatus::SUCCESS;    // do nothing
}

// ################################
// ##     UserDefinedCmdNode     ##
// ################################

UserDefinedCmdNode::~UserDefinedCmdNode() {
    delete this->blockNode;
}

void UserDefinedCmdNode::dump(NodeDumper &dumper) const {
    DUMP(name);
    DUMP_PTR(blockNode);
    DUMP_PRIM(maxVarNum);
    dumper.dump("sourceName", this->srcInfoPtr->getSourceName());
}

void UserDefinedCmdNode::accept(NodeVisitor &visitor) {
    visitor.visitUserDefinedCmdNode(*this);
}

EvalStatus UserDefinedCmdNode::eval(RuntimeContext &ctx) {
    ctx.addUserDefinedCommand(this);
    return EvalStatus::REMOVE;
}

// #########################
// ##     BindVarNode     ##
// #########################

void BindVarNode::setAttribute(FieldHandle *handle) {
    this->varIndex = handle->getFieldIndex();
}

void BindVarNode::dump(NodeDumper &dumper) const {
    DUMP(varName);
    DUMP_PRIM(varIndex);
    //FIXME: value
}

void BindVarNode::accept(NodeVisitor &visitor) {
    visitor.visitBindVarNode(*this);
}

EvalStatus BindVarNode::eval(RuntimeContext &ctx) {
    ctx.setGlobal(this->varIndex, this->value);
    return EvalStatus::SUCCESS;
}

// #######################
// ##     EmptyNode     ##
// #######################

void EmptyNode::dump(NodeDumper &) const {
} // do nothing

void EmptyNode::accept(NodeVisitor &visitor) {
    visitor.visitEmptyNode(*this);
}

EvalStatus EmptyNode::eval(RuntimeContext &) {
    return EvalStatus::SUCCESS; // do nothing
}

// #######################
// ##     DummyNode     ##
// #######################

void DummyNode::dump(NodeDumper &) const {
} // do nothing

void DummyNode::accept(NodeVisitor &visitor) {
    visitor.visitDummyNode(*this);
}

EvalStatus DummyNode::eval(RuntimeContext &) {
    return EvalStatus::SUCCESS; // do nothing
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

EvalStatus RootNode::eval(RuntimeContext &ctx) {
    ctx.resetState();

    ctx.pushFuncContext(this);
    ctx.reserveGlobalVar(this->maxGVarNum);
    ctx.reserveLocalVar(ctx.getLocalVarOffset() + this->maxVarNum);

    for(auto iter = this->nodeList.begin(); iter != this->nodeList.end();) {
        Node *node = *iter;
        EvalStatus status = node->eval(ctx);
        switch(status) {
        case EvalStatus::SUCCESS: {
            break;
        }
        case EvalStatus::THROW: {
            return status;
        }
        case EvalStatus::REMOVE: {
            iter = this->nodeList.erase(iter);
            continue;
        }
        default:
            fatal("illegal EvalStatus: %u\n", static_cast<unsigned int>(status));
            break;
        }
        ++iter;
    }
    return EvalStatus::SUCCESS;
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

ForNode *createForInNode(unsigned int startPos, VarNode *varNode, Node *exprNode, BlockNode *blockNode) {
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
    VarDeclNode *init_var = new VarDeclNode(startPos,
                                            VarNode::extractVarNameAndDelete(varNode), call_next, false);

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
    BinaryOpNode *opNode = new BinaryOpNode(new DummyNode(), resolveAssignOp(op), rightNode);
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

} // namespace ast
} // namespace ydsh