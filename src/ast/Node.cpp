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

#include <sys/wait.h>
#include <pwd.h>

#include <cassert>

#include "../core/system.h"
#include "../core/symbol.h"
#include "../core/DSObject.h"
#include "../core/RuntimeContext.h"
#include "../core/FieldHandle.h"
#include "../misc/debug.h"
#include "dump.h"
#include "Node.h"

// helper macro
#define EVAL(ctx, node) \
    do {\
        EvalStatus status = (node)->eval(ctx);\
        if(status != EvalStatus::SUCCESS) {\
            return status;\
        }\
    } while(0)

namespace ydsh {
namespace ast {

// ##################
// ##     Node     ##
// ##################

void Node::setType(DSType *type) {
    this->type = type;
}

void Node::inStringExprNode() { } // do nothing

void Node::inCmdArgNode() { } // do nothing

void Node::inCondition() { }  // do nothing

void Node::inRightHandleSide() { }   // do nothing

bool Node::isBlockEndNode() const {
    return false;
}

void Node::setSourceName(const char *sourceName) { } // do nothing

const char *Node::getSourceName() {
    static const char empty[] = "";
    return empty;
}

// ##########################
// ##     IntValueNode     ##
// ##########################

void IntValueNode::setType(DSType *type) {
    this->type = type;
    this->value = DSValue::create<Int_Object>(this->type, this->tempValue);
}

void IntValueNode::dump(Writer &writer) const {
    WRITE_PRIM(tempValue);
    if(this->type == 0) {
        writer.write(NAME(value), "(null)");
    } else {
        Int_Object *obj = TYPE_AS(Int_Object, this->value);
        writer.write(NAME(value), std::to_string(obj->getValue()));
    }
}

void IntValueNode::accept(NodeVisitor *visitor) {
    visitor->visitIntValueNode(this);
}

EvalStatus IntValueNode::eval(RuntimeContext &ctx) {
    ctx.push(this->value);
    return EvalStatus::SUCCESS;
}

// ###########################
// ##     LongValueNode     ##
// ###########################

void LongValueNode::setType(DSType *type) {
    this->type = type;
    this->value = DSValue::create<Long_Object>(this->type, this->tempValue);
}

void LongValueNode::dump(Writer &writer) const {
    WRITE_PRIM(tempValue);
    WRITE_PRIM(unsignedValue);
    if(this->type == 0) {
        writer.write(NAME(value), "(null)");
    } else {
        Long_Object *obj = TYPE_AS(Long_Object, this->value);
        writer.write(NAME(value), std::to_string(obj->getValue()));
    }
}

void LongValueNode::accept(NodeVisitor *visitor) {
    visitor->visitLongValueNode(this);
}

EvalStatus LongValueNode::eval(RuntimeContext &ctx) {
    ctx.push(this->value);
    return EvalStatus::SUCCESS;
}


// ############################
// ##     FloatValueNode     ##
// ############################

void FloatValueNode::setType(DSType *type) {
    this->type = type;
    this->value = DSValue::create<Float_Object>(this->type, this->tempValue);
}

void FloatValueNode::dump(Writer &writer) const {
    WRITE_PRIM(tempValue);
    if(this->type == 0) {
        writer.write(NAME(value), "(null)");
    } else {
        Float_Object *obj = TYPE_AS(Float_Object, this->value);
        writer.write(NAME(value), std::to_string(obj->getValue()));
    }
}

void FloatValueNode::accept(NodeVisitor *visitor) {
    visitor->visitFloatValueNode(this);
}

EvalStatus FloatValueNode::eval(RuntimeContext &ctx) {
    ctx.push(this->value);
    return EvalStatus::SUCCESS;
}

// ############################
// ##     StringValueNode    ##
// ############################

void StringValueNode::setType(DSType *type) {
    this->type = type;
    this->value = DSValue::create<String_Object>(this->type, std::move(this->tempValue));
}

void StringValueNode::dump(Writer &writer) const {
    if(this->type == 0) {
        writer.write(NAME(tempValue), this->tempValue);
        writer.write(NAME(value), "");

    } else {
        String_Object *obj = TYPE_AS(String_Object, this->value);
        writer.write(NAME(tempValue), "");
        writer.write(NAME(value), obj->getValue());
    }
}

void StringValueNode::accept(NodeVisitor *visitor) {
    visitor->visitStringValueNode(this);
}

EvalStatus StringValueNode::eval(RuntimeContext &ctx) {
    ctx.push(this->value);
    return EvalStatus::SUCCESS;
}

// ############################
// ##     ObjectPathNode     ##
// ############################

void ObjectPathNode::accept(NodeVisitor *visitor) {
    visitor->visitObjectPathNode(this);
}

// ############################
// ##     StringExprNode     ##
// ############################

StringExprNode::~StringExprNode() {
    for(Node *e : this->nodes) {
        delete e;
    }
    this->nodes.clear();
}

void StringExprNode::addExprNode(Node *node) {
    node->inStringExprNode();
    this->nodes.push_back(node);
}

void StringExprNode::dump(Writer &writer) const {
    WRITE(nodes);
}

void StringExprNode::accept(NodeVisitor *visitor) {
    visitor->visitStringExprNode(this);
}

EvalStatus StringExprNode::eval(RuntimeContext &ctx) {
    unsigned int size = this->nodes.size();
    if(size == 0) {
        ctx.push(DSValue::create<String_Object>(this->type));
    } else if(size == 1) {
        EVAL(ctx, this->nodes[0]);
        if(*this->nodes[0]->getType() != *ctx.getPool().getStringType()) {
            return ctx.toInterp(this->nodes[0]->getLineNum());
        }
    } else {
        std::string str;
        for(Node *node : this->nodes) {
            EVAL(ctx, node);
            if(*node->getType() != *ctx.getPool().getStringType()) {
                EvalStatus status = ctx.toInterp(node->getLineNum());
                if(status != EvalStatus::SUCCESS) {
                    return status;
                }
            }
            str += TYPE_AS(String_Object, ctx.pop())->getValue();
        }
        ctx.push(DSValue::create<String_Object>(this->type, std::move(str)));
    }
    return EvalStatus::SUCCESS;
}

// #######################
// ##     ArrayNode     ##
// #######################

ArrayNode::ArrayNode(unsigned int lineNum, Node *node) :
        Node(lineNum), nodes() {
    this->nodes.push_back(node);
}

ArrayNode::~ArrayNode() {
    for(Node *e : this->nodes) {
        delete e;
    }
    this->nodes.clear();
}

void ArrayNode::addExprNode(Node *node) {
    this->nodes.push_back(node);
}

void ArrayNode::setExprNode(unsigned int index, Node *node) {
    this->nodes[index] = node;
}

void ArrayNode::dump(Writer &writer) const {
    WRITE(nodes);
}

void ArrayNode::accept(NodeVisitor *visitor) {
    visitor->visitArrayNode(this);
}

EvalStatus ArrayNode::eval(RuntimeContext &ctx) {
    auto value = DSValue::create<Array_Object>(this->type);
    for(Node *node : this->nodes) {
        EVAL(ctx, node);
        TYPE_AS(Array_Object, value)->append(ctx.pop());
    }
    ctx.push(std::move(value));
    return EvalStatus::SUCCESS;
}

// #####################
// ##     MapNode     ##
// #####################

MapNode::MapNode(unsigned int lineNum, Node *keyNode, Node *valueNode) :
        Node(lineNum), keyNodes(), valueNodes() {
    this->keyNodes.push_back(keyNode);
    this->valueNodes.push_back(valueNode);
}

MapNode::~MapNode() {
    for(Node *e : this->keyNodes) {
        delete e;
    }
    this->keyNodes.clear();
    for(Node *e : this->valueNodes) {
        delete e;
    }
    this->valueNodes.clear();
}

void MapNode::addEntry(Node *keyNode, Node *valueNode) {
    this->keyNodes.push_back(keyNode);
    this->valueNodes.push_back(valueNode);
}

void MapNode::setKeyNode(unsigned int index, Node *keyNode) {
    this->keyNodes[index] = keyNode;
}

void MapNode::setValueNode(unsigned int index, Node *valueNode) {
    this->valueNodes[index] = valueNode;
}

void MapNode::dump(Writer &writer) const {
    WRITE(keyNodes);
    WRITE(valueNodes);
}

void MapNode::accept(NodeVisitor *visitor) {
    visitor->visitMapNode(this);
}

EvalStatus MapNode::eval(RuntimeContext &ctx) {
    auto map = DSValue::create<Map_Object>(this->type);
    unsigned int size = this->keyNodes.size();
    for(unsigned int i = 0; i < size; i++) {
        EVAL(ctx, this->keyNodes[i]);
        auto key = ctx.pop();
        EVAL(ctx, this->valueNodes[i]);
        auto value = ctx.pop();
        TYPE_AS(Map_Object, map)->set(key, value);
    }
    ctx.push(std::move(map));
    return EvalStatus::SUCCESS;
}

// #######################
// ##     TupleNode     ##
// #######################

TupleNode::TupleNode(unsigned int lineNum, Node *leftNode, Node *rightNode) :
        Node(lineNum), nodes(2) {
    this->nodes[0] = leftNode;
    this->nodes[1] = rightNode;
}

TupleNode::~TupleNode() {
    for(Node *node : this->nodes) {
        delete node;
    }
    this->nodes.clear();
}

void TupleNode::addNode(Node *node) {
    this->nodes.push_back(node);
}

void TupleNode::dump(Writer &writer) const {
    WRITE(nodes);
}

void TupleNode::accept(NodeVisitor *visitor) {
    visitor->visitTupleNode(this);
}

EvalStatus TupleNode::eval(RuntimeContext &ctx) {
    unsigned int size = this->nodes.size();
    auto value = DSValue::create<Tuple_Object>(this->type);
    for(unsigned int i = 0; i < size; i++) {
        EVAL(ctx, this->nodes[i]);
        TYPE_AS(Tuple_Object, value)->set(i, ctx.pop());
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

void AssignableNode::dump(Writer &writer) const {
    WRITE_PRIM(index);
    WRITE_PRIM(readOnly);
    WRITE_PRIM(global);
    WRITE_PRIM(env);
    WRITE_PRIM(interface);
}

// #####################
// ##     VarNode     ##
// #####################

void VarNode::dump(Writer &writer) const {
    WRITE(varName);
    AssignableNode::dump(writer);
}

void VarNode::accept(NodeVisitor *visitor) {
    visitor->visitVarNode(this);
}

EvalStatus VarNode::eval(RuntimeContext &ctx) {
    if(this->isGlobal()) {
        ctx.loadGlobal(this->getIndex());
    } else {
        ctx.loadLocal(this->getIndex());
    }
    if(this->type != 0 && this->type->isFuncType()) {
        ctx.peek()->setType(this->type);
    }
    return EvalStatus::SUCCESS;
}


// ########################
// ##     AccessNode     ##
// ########################

AccessNode::~AccessNode() {
    delete this->recvNode;
    this->recvNode = 0;
}

void AccessNode::dump(Writer &writer) const {
    WRITE_PTR(recvNode);
    WRITE(fieldName);
    AssignableNode::dump(writer);

#define EACH_ENUM(OP, out) \
    OP(NOP, out) \
    OP(DUP_RECV, out)

    std::string str;
    DECODE_ENUM(str, this->additionalOp, EACH_ENUM);
    writer.write(NAME(addtionalOp), str);
#undef EACH_ENUM
}

void AccessNode::accept(NodeVisitor *visitor) {
    visitor->visitAccessNode(this);
}

EvalStatus AccessNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->recvNode);

    switch(this->additionalOp) {
    case NOP: {
        if(this->withinInterface()) {
            return ctx.loadField(this->recvNode->getType(), this->fieldName, this->type);
        }

        ctx.loadField(this->getIndex());
        if(this->type != 0 && this->type->isFuncType()) {
            ctx.peek()->setType(this->type);
        }
        break;
    }
    case DUP_RECV: {
        if(this->withinInterface()) {
            return ctx.dupAndLoadField(this->recvNode->getType(), this->fieldName, this->type);
        }

        ctx.dupAndLoadField(this->getIndex());
        if(this->type != 0 && this->type->isFuncType()) {
            ctx.peek()->setType(this->type);
        }
        break;
    }
    }

    return EvalStatus::SUCCESS;
}

std::pair<Node *, std::string> AccessNode::split(AccessNode *accessNode) {
    Node *node = accessNode->recvNode;
    accessNode->recvNode = 0;

    std::pair<Node *, std::string> pair(node, std::move(accessNode->fieldName));
    delete accessNode;
    return pair;
}

// ######################
// ##     CastNode     ##
// ######################

CastNode::CastNode(Node *exprNode, TypeToken *type, bool dupTypeToken) :
        Node(exprNode->getLineNum()), exprNode(exprNode), targetTypeToken(0),
        opKind(NOP) {
    static const unsigned long tag = (unsigned long) 1L << 63;

    if(dupTypeToken) {
        TypeToken *tok = (TypeToken *) (tag | (unsigned long) type);
        this->targetTypeToken = tok;
    } else {
        this->targetTypeToken = type;
    }
}

CastNode::~CastNode() {
    delete this->exprNode;
    this->exprNode = 0;

    if((long) this->targetTypeToken >= 0) {
        delete this->targetTypeToken;
        this->targetTypeToken = 0;
    }
}

TypeToken *CastNode::getTargetTypeToken() const {
    static const unsigned long mask = ~(1L << 63);
    if((long) this->targetTypeToken < 0) {
        TypeToken *tok = (TypeToken *) (mask & (unsigned long) this->targetTypeToken);
        return tok;
    }
    return this->targetTypeToken;
}

void CastNode::dump(Writer &writer) const {
    WRITE_PTR(exprNode);
    TypeToken *targetTypeToken = this->getTargetTypeToken();
    WRITE_PTR(targetTypeToken);

#define EACH_ENUM(OP, out) \
    OP(NOP, out) \
    OP(INT_TO_FLOAT, out) \
    OP(FLOAT_TO_INT, out) \
    OP(INT_TO_LONG, out) \
    OP(LONG_TO_INT, out) \
    OP(LONG_TO_FLOAT, out) \
    OP(FLOAT_TO_LONG, out) \
    OP(COPY_INT, out) \
    OP(COPY_LONG, out) \
    OP(TO_STRING, out) \
    OP(CHECK_CAST, out)

    std::string str;
    DECODE_ENUM(str, this->opKind, EACH_ENUM);
    writer.write(NAME(opKind), str);
#undef EACH_ENUM
}

void CastNode::accept(NodeVisitor *visitor) {
    visitor->visitCastNode(this);
}

EvalStatus CastNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->exprNode);

    switch(this->opKind) {
    case NOP:
        break;
    case INT_TO_FLOAT: {
        int value = TYPE_AS(Int_Object, ctx.pop())->getValue();
        double afterValue = value;
        if(*this->exprNode->getType() != *ctx.getPool().getInt32Type()) {
            afterValue = (unsigned int) value;
        }
        ctx.push(DSValue::create<Float_Object>(this->type, afterValue));
        break;
    }
    case FLOAT_TO_INT: {
        double value = TYPE_AS(Float_Object, ctx.pop())->getValue();
        int afterValue = value;
        if(*this->type == *ctx.getPool().getUint32Type()) {
            unsigned int temp = value;
            afterValue = temp;
        }
        ctx.push(DSValue::create<Int_Object>(this->type, afterValue));
        break;
    }
    case INT_TO_LONG: {
        int value = TYPE_AS(Int_Object, ctx.pop())->getValue();
        long afterValue = (long) value;
        if(*this->exprNode->getType() != *ctx.getPool().getInt32Type()) {
            afterValue = (unsigned int) value;
        }
        ctx.push(DSValue::create<Long_Object>(this->type, afterValue));
        break;
    };
    case LONG_TO_INT: {
        long value = TYPE_AS(Long_Object, ctx.pop())->getValue();
        int afterValue = value;
        if(*this->type == *ctx.getPool().getUint32Type()) {
            unsigned int temp = value;
            afterValue = temp;
        }
        ctx.push(DSValue::create<Int_Object>(this->type, afterValue));
        break;
    };
    case LONG_TO_FLOAT: {
        long value = TYPE_AS(Long_Object, ctx.pop())->getValue();
        double afterValue = value;
        if(*this->exprNode->getType() == *ctx.getPool().getUint64Type()) {
            afterValue = (unsigned long) value;
        }
        ctx.push(DSValue::create<Float_Object>(this->type, afterValue));
        break;
    };
    case FLOAT_TO_LONG: {
        double value = TYPE_AS(Float_Object, ctx.pop())->getValue();
        long afterValue = (long) value;
        if(*this->type == *ctx.getPool().getUint64Type()) {
            unsigned long temp = (unsigned long) value;
            afterValue = temp;
        }
        ctx.push(DSValue::create<Long_Object>(this->type, afterValue));
        break;
    };
    case COPY_INT: {
        int value = TYPE_AS(Int_Object, ctx.pop())->getValue();
        ctx.push(DSValue::create<Int_Object>(this->type, value));
        break;
    };
    case COPY_LONG: {
        long value = TYPE_AS(Long_Object, ctx.pop())->getValue();
        ctx.push(DSValue::create<Long_Object>(this->type, value));
        break;
    };
    case TO_STRING: {
        return ctx.toString(this->getLineNum());
    };
    case CHECK_CAST: {
        return ctx.checkCast(this->lineNum, this->type) ? EvalStatus::SUCCESS : EvalStatus::THROW;
    };
    default:
        fatal("unsupported cast op\n");
    }
    return EvalStatus::SUCCESS;
}

CastNode *CastNode::newTypedCastNode(Node *targetNode, DSType *type, CastNode::CastOp op) {
    assert(targetNode->getType() != nullptr);
    CastNode *castNode = new CastNode(targetNode, 0);
    castNode->setOpKind(op);
    castNode->setType(type);
    return castNode;
}

// ############################
// ##     InstanceOfNode     ##
// ############################

InstanceOfNode::~InstanceOfNode() {
    delete this->targetNode;
    this->targetNode = 0;

    delete this->targetTypeToken;
    this->targetTypeToken = 0;
}

void InstanceOfNode::dump(Writer &writer) const {
    WRITE_PTR(targetNode);
    WRITE_PTR(targetTypeToken);
    WRITE_PTR(targetType);

#define EACH_ENUM(OP, out) \
    OP(ALWAYS_FALSE, out) \
    OP(ALWAYS_TRUE, out) \
    OP(INSTANCEOF, out)

    std::string val;
    DECODE_ENUM(val, this->opKind, EACH_ENUM);
    writer.write(NAME(opKind), val);
#undef EACH_ENUM
}

void InstanceOfNode::accept(NodeVisitor *visitor) {
    visitor->visitInstanceOfNode(this);
}

EvalStatus InstanceOfNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->targetNode);

    switch(this->opKind) {
    case INSTANCEOF:
        ctx.instanceOf(this->targetType);
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

// ######################
// ##     ArgsNode     ##
// ######################

ArgsNode::~ArgsNode() {
    for(Node *node : this->nodes) {
        delete node;
    }
    this->nodes.clear();
}

void ArgsNode::addArg(Node *argNode) {  //TODO: named argument
    this->nodes.push_back(argNode);
}

void ArgsNode::setArg(unsigned int index, Node *argNode) {
    this->nodes[index] = argNode;
}

void ArgsNode::dump(Writer &writer) const {
    WRITE(nodes);
}

void ArgsNode::accept(NodeVisitor *visitor) {
    visitor->visitArgsNode(this);
}

EvalStatus ArgsNode::eval(RuntimeContext &ctx) {
    for(Node *node : this->nodes) {
        EVAL(ctx, node);
    }
    return EvalStatus::SUCCESS;
}

// ######################
// ##     CallNode     ##
// ######################

CallNode::~CallNode() {
    delete this->argsNode;
    this->argsNode = 0;
}

// #######################
// ##     ApplyNode     ##
// #######################

ApplyNode::~ApplyNode() {
    delete this->exprNode;
}

void ApplyNode::dump(Writer &writer) const {
    WRITE_PTR(exprNode);
    WRITE_PTR(argsNode);
}

void ApplyNode::accept(NodeVisitor *visitor) {
    visitor->visitApplyNode(this);
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
    unsigned int actualParamSize = this->argsNode->getNodes().size();

    // push func object
    EVAL(ctx, this->exprNode);

    // push arguments.
    EVAL(ctx, this->argsNode);

    // call function
    return ctx.applyFuncObject(this->lineNum, this->type->isVoidType(), actualParamSize);
}

// ############################
// ##     MethodCallNode     ##
// ############################

MethodCallNode::~MethodCallNode() {
    delete this->recvNode;
    this->recvNode = 0;
}

void MethodCallNode::dump(Writer &writer) const {
    WRITE_PTR(recvNode);
    WRITE(methodName);
    unsigned int methodIndex =
            this->handle != nullptr ? this->handle->getMethodIndex() : 0;
    WRITE_PRIM(methodIndex);
    WRITE_PTR(argsNode);

#define EACH_FLAG(OP, out, set) \
    OP(INDEX, out, set) \
    OP(ICALL, out, set)

    std::string str;
    DECODE_BITSET(str, this->attributeSet, EACH_FLAG);
    writer.write(NAME(attributeSet), str);
#undef EACH_FLAG
}

void MethodCallNode::accept(NodeVisitor *visitor) {
    visitor->visitMethodCallNode(this);
}

EvalStatus MethodCallNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->recvNode);
    EVAL(ctx, this->argsNode);

    return ctx.callMethod(this->lineNum, this->methodName, this->handle);
}

// #####################
// ##     NewNode     ##
// #####################

NewNode::~NewNode() {
    delete this->targetTypeToken;
    this->targetTypeToken = 0;

    delete this->argsNode;
    this->argsNode = 0;
}

void NewNode::dump(Writer &writer) const {
    WRITE_PTR(targetTypeToken);
    WRITE_PTR(argsNode);
}

void NewNode::accept(NodeVisitor *visitor) {
    visitor->visitNewNode(this);
}

EvalStatus NewNode::eval(RuntimeContext &ctx) {
    unsigned int paramSize = this->argsNode->getNodes().size();

    ctx.newDSObject(this->type);

    // push param
    EVAL(ctx, this->argsNode);

    // call constructor
    return ctx.callConstructor(this->lineNum, paramSize);
}

// #########################
// ##     UnaryOpNode     ##
// #########################

UnaryOpNode::~UnaryOpNode() {
    delete this->exprNode;
    this->exprNode = 0;

    delete this->methodCallNode;
    this->methodCallNode = 0;
}

MethodCallNode *UnaryOpNode::createApplyNode() {
    this->methodCallNode = new MethodCallNode(this->exprNode, resolveUnaryOpName(this->op));

    // assign null to prevent double free
    this->exprNode = 0;

    return this->methodCallNode;
}

void UnaryOpNode::dump(Writer &writer) const {
    writer.write(NAME(op), TO_NAME(op));
    WRITE_PTR(exprNode);
    WRITE_PTR(methodCallNode);
}

void UnaryOpNode::accept(NodeVisitor *visitor) {
    visitor->visitUnaryOpNode(this);
}

EvalStatus UnaryOpNode::eval(RuntimeContext &ctx) {
    return this->methodCallNode->eval(ctx);
}


// ##########################
// ##     BinaryOpNode     ##
// ##########################

BinaryOpNode::~BinaryOpNode() {
    delete this->leftNode;
    this->leftNode = 0;

    delete this->rightNode;
    this->rightNode = 0;

    delete this->methodCallNode;
    this->methodCallNode = 0;
}

MethodCallNode *BinaryOpNode::createApplyNode() {
    this->methodCallNode = new MethodCallNode(this->leftNode, resolveBinaryOpName(this->op));
    this->methodCallNode->getArgsNode()->addArg(this->rightNode);

    // assign null to prevent double free.
    this->leftNode = 0;
    this->rightNode = 0;

    return this->methodCallNode;
}

void BinaryOpNode::dump(Writer &writer) const {
    WRITE_PTR(leftNode);
    WRITE_PTR(rightNode);
    writer.write(NAME(op), TO_NAME(op));
    WRITE_PTR(methodCallNode);
}

void BinaryOpNode::accept(NodeVisitor *visitor) {
    visitor->visitBinaryOpNode(this);
}

EvalStatus BinaryOpNode::eval(RuntimeContext &ctx) {
    return this->methodCallNode->eval(ctx);
}

// #######################
// ##     GroupNode     ##
// #######################

GroupNode::~GroupNode() {
    delete this->exprNode;
    this->exprNode = 0;
}

void GroupNode::dump(Writer &writer) const {
    WRITE_PTR(exprNode);
}

void GroupNode::accept(NodeVisitor *visitor) {
    visitor->visitGroupNode(this);
}

EvalStatus GroupNode::eval(RuntimeContext &ctx) {
    return this->exprNode->eval(ctx);
}


// ########################
// ##     CondOpNode     ##
// ########################

CondOpNode::CondOpNode(Node *leftNode, Node *rightNode, bool isAndOp) :
        Node(leftNode->getLineNum()), leftNode(leftNode), rightNode(rightNode), andOp(isAndOp) {
    this->leftNode->inCondition();
    this->rightNode->inCondition();
}

CondOpNode::~CondOpNode() {
    delete this->leftNode;
    this->leftNode = 0;

    delete this->rightNode;
    this->rightNode = 0;
}

void CondOpNode::dump(Writer &writer) const {
    WRITE_PTR(leftNode);
    WRITE_PTR(rightNode);
    WRITE_PRIM(andOp);
}

void CondOpNode::accept(NodeVisitor *visitor) {
    visitor->visitCondOpNode(this);
}

EvalStatus CondOpNode::eval(RuntimeContext &ctx) {
    // eval left node
    EVAL(ctx, this->leftNode);

    if(this->andOp) {   // and
        if(TYPE_AS(Boolean_Object, ctx.peek())->getValue()) {
            ctx.popNoReturn();
            return this->rightNode->eval(ctx);
        } else {
            return EvalStatus::SUCCESS;
        }
    } else {    // or
        if(TYPE_AS(Boolean_Object, ctx.peek())->getValue()) {
            return EvalStatus::SUCCESS;
        } else {
            ctx.popNoReturn();
            return this->rightNode->eval(ctx);
        }
    }
}


// ########################
// ##     CmdArgNode     ##
// ########################

CmdArgNode::~CmdArgNode() {
    for(Node *e : this->segmentNodes) {
        delete e;
    }
    this->segmentNodes.clear();
}

void CmdArgNode::addSegmentNode(Node *node) {
    node->inCmdArgNode();
    this->segmentNodes.push_back(node);
}

void CmdArgNode::dump(Writer &writer) const {
    WRITE(segmentNodes);
}

void CmdArgNode::accept(NodeVisitor *visitor) {
    visitor->visitCmdArgNode(this);
}

EvalStatus CmdArgNode::eval(RuntimeContext &ctx) {
    EvalStatus s = this->evalImpl(ctx);
    if(s != EvalStatus::SUCCESS) {
        return s;
    }

    ctx.getProcInvoker().addArg(ctx.pop(), this->isIgnorableEmptyString());
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
        str += TYPE_AS(String_Object, ctx.pop())->getValue();
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
        Node(0), op(RedirectOP::DUMMY), targetNode(node) {
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

void RedirNode::dump(Writer &writer) const {
    static const char *redirOpStr[] = {
#define GEN_STR(ENUM, STR) #ENUM,
            EACH_RedirectOP(GEN_STR)
#undef GEN_STR
    };

    writer.write(NAME(op), redirOpStr[this->op]);
    WRITE_PTR(targetNode);
}

void RedirNode::accept(NodeVisitor *visitor) {
    visitor->visitRedirNode(this);
}

EvalStatus RedirNode::eval(RuntimeContext &ctx) {
    EvalStatus s = this->targetNode->evalImpl(ctx);
    if(s != EvalStatus::SUCCESS) {
        return s;
    }
    ctx.getProcInvoker().addRedirOption(this->op, ctx.pop());
    return EvalStatus::SUCCESS;
}

// #######################
// ##     TildeNode     ##
// #######################

void TildeNode::dump(Writer &writer) const {
    WRITE(value);
}

void TildeNode::accept(NodeVisitor *visitor) {
    visitor->visitTildeNode(this);
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
    this->nameNode = 0;

    for(auto *e : this->argNodes) {
        delete e;
    }
    this->argNodes.clear();
}

void CmdNode::addArgNode(CmdArgNode *node) {
    this->argNodes.push_back(node);
}

void CmdNode::addRedirOption(TokenKind kind, CmdArgNode *node) {
    this->argNodes.push_back(new RedirNode(kind, node));
}

void CmdNode::addRedirOption(TokenKind kind) {
    this->addRedirOption(kind, new CmdArgNode(new StringValueNode(std::string(""))));
}

void CmdNode::dump(Writer &writer) const {
    WRITE_PTR(nameNode);
    WRITE(argNodes);
}

void CmdNode::accept(NodeVisitor *visitor) {
    visitor->visitCmdNode(this);
}

EvalStatus CmdNode::eval(RuntimeContext &ctx) {
    ctx.getProcInvoker().openProc();

    EVAL(ctx, this->nameNode);

    ctx.getProcInvoker().addCommandName(ctx.pop());

    for(Node *node : this->argNodes) {
        EVAL(ctx, node);
    }

    ctx.getProcInvoker().closeProc();
    return EvalStatus::SUCCESS;
}

// ##########################
// ##     PipedCmdNode     ##
// ##########################

PipedCmdNode::~PipedCmdNode() {
    for(CmdNode *p : this->cmdNodes) {
        delete p;
    }
    this->cmdNodes.clear();
}

void PipedCmdNode::addCmdNodes(CmdNode *node) {
    this->cmdNodes.push_back(node);
}

void PipedCmdNode::inCondition() {
    this->asBool = true;
}

void PipedCmdNode::dump(Writer &writer) const {
    std::vector<Node *> cmdNodes;
    for(CmdNode *node : this->cmdNodes) {
        cmdNodes.push_back(node);
    }

    WRITE(cmdNodes);
}

void PipedCmdNode::accept(NodeVisitor *visitor) {
    visitor->visitPipedCmdNode(this);
}

EvalStatus PipedCmdNode::eval(RuntimeContext &ctx) {
    /**
     * clear remained arguments of parent process.
     * in following case, if we execute echo command in subshell,
     * ProcInvoker still contains `cat', an argument pushed by parent shell.
     *
     * cat $(echo a b c)
     *
     * so, need to clear them before push arguments.
     */
    ctx.getProcInvoker().clear();

    for(auto &node : this->cmdNodes) {
        EVAL(ctx, node);
    }

    ctx.pushCallFrame(this->lineNum);
    EvalStatus status = ctx.getProcInvoker().invoke();
    ctx.popCallFrame();
    ctx.getProcInvoker().clear();

    // push exit status
    if(*this->type == *ctx.getPool().getBooleanType()) {
        if(TYPE_AS(Int_Object, ctx.getExitStatus())->getValue() == 0) {
            ctx.push(ctx.getTrueObj());
        } else {
            ctx.push(ctx.getFalseObj());
        }
    }

    return status;
}

// ###########################
// ##     CmdContextNode    ##
// ###########################

CmdContextNode::~CmdContextNode() {
    delete this->exprNode;
    this->exprNode = nullptr;
}

void CmdContextNode::inStringExprNode() {
    this->setAttribute(STR_CAP);
    this->setAttribute(FORK);
}

void CmdContextNode::inCmdArgNode() {
    this->setAttribute(ARRAY_CAP);
    this->setAttribute(FORK);
}

void CmdContextNode::inCondition() {
    this->setAttribute(CONDITION);
}

void CmdContextNode::inRightHandleSide() {
    this->setAttribute(CONDITION);
}

void CmdContextNode::dump(Writer &writer) const {
    WRITE_PTR(exprNode);

#define EACH_FLAG(OP, out, set) \
    OP(BACKGROUND, out, set) \
    OP(FORK, out, set) \
    OP(STR_CAP, out, set) \
    OP(ARRAY_CAP, out, set) \
    OP(CONDITION, out, set)

    std::string value;
    DECODE_BITSET(value, this->attributeSet, EACH_FLAG);
    writer.write(NAME(attributeSet), value);

#undef EACH_FLAG
}

void CmdContextNode::accept(NodeVisitor *visitor) {
    visitor->visitCmdContextNode(this);
}

EvalStatus CmdContextNode::eval(RuntimeContext &ctx) {
    if(this->hasAttribute(FORK) &&
       (this->hasAttribute(STR_CAP) || this->hasAttribute(ARRAY_CAP))) {
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

            if(*this->type == *ctx.getPool().getStringType()) {  // capture stdout as String
                static const int bufSize = 256;
                char buf[bufSize + 1];
                int readSize = 0;
                std::string str;
                while((readSize = read(pipefds[READ_PIPE], buf, bufSize)) > 0) {
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

                obj = DSValue::create<String_Object>(this->type, std::move(str));
            } else {    // capture stdout as String Array
                static const int bufSize = 256;
                char buf[bufSize];
                int readSize;
                std::string str;
                Array_Object *array = new Array_Object(this->type);
                while((readSize = read(pipefds[READ_PIPE], buf, bufSize)) > 0) {
                    for(int i = 0; i < readSize; i++) {
                        char ch = buf[i];
                        switch(ch) {
                        case ' ':
                        case '\t':
                        case '\n': {
                            if(!str.empty()) {
                                array->append(DSValue::create<String_Object>(
                                        ctx.getPool().getStringType(), std::move(str)));
                                str = "";
                            }
                            break;
                        }
                        default: {
                            str += ch;
                            break;
                        }
                        }
                    }
                }
                if(!str.empty()) {
                    array->append(DSValue::create<String_Object>(
                            ctx.getPool().getStringType(), std::move(str)));
                }

                obj = DSValue(array);
            }
            close(pipefds[READ_PIPE]);

            // wait exit
            int status;
            waitpid(pid, &status, 0);

            // push object
            ctx.push(std::move(obj));
            return EvalStatus::SUCCESS;
        } else if(pid == 0) {   // child process
            dup2(pipefds[WRITE_PIPE], STDOUT_FILENO);
            close(pipefds[READ_PIPE]);
            close(pipefds[WRITE_PIPE]);

            if(this->exprNode->eval(ctx) == EvalStatus::THROW) {
                if(ctx.getPool().getErrorType()->isSameOrBaseTypeOf(ctx.getThrownObject()->getType())) {
                    ctx.reportError();
                }
                exit(1);
            } //FIXME: propagate error

            exit(0);
        } else {
            perror("fork failed");
            exit(1);    //FIXME: throw exception
        }
    }


    return this->exprNode->eval(ctx);
}

// ########################
// ##     AssertNode     ##
// ########################

AssertNode::~AssertNode() {
    delete this->condNode;
    this->condNode = 0;
}

void AssertNode::dump(Writer &writer) const {
    WRITE_PTR(condNode);
}

void AssertNode::accept(NodeVisitor *visitor) {
    visitor->visitAssertNode(this);
}

EvalStatus AssertNode::eval(RuntimeContext &ctx) {
    if(ctx.isAssertion()) {
        EVAL(ctx, this->condNode);
        return ctx.checkAssertion(this->condNode->getLineNum());
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
    this->nodeList.clear();
}

void BlockNode::addNode(Node *node) {
    this->nodeList.push_back(node);
}

void BlockNode::insertNodeToFirst(Node *node) {
    this->nodeList.push_front(node);
}

void BlockNode::dump(Writer &writer) const {
    WRITE(nodeList);
}

void BlockNode::accept(NodeVisitor *visitor) {
    visitor->visitBlockNode(this);
}

EvalStatus BlockNode::eval(RuntimeContext &ctx) {
    for(Node *node : this->nodeList) {
        EvalStatus status = node->eval(ctx);
        if(!node->getType()->isVoidType()) {
            ctx.popNoReturn();
        }
        if(status != EvalStatus::SUCCESS) {
            return status;
        }
    }
    return EvalStatus::SUCCESS;
}

// #######################
// ##     BreakNode     ##
// #######################

void BreakNode::dump(Writer &writer) const { } // do nothing

void BreakNode::accept(NodeVisitor *visitor) {
    visitor->visitBreakNode(this);
}

EvalStatus BreakNode::eval(RuntimeContext &ctx) {
    return EvalStatus::BREAK;
}

// ##########################
// ##     ContinueNode     ##
// ##########################

void ContinueNode::dump(Writer &writer) const { } // do nothing

void ContinueNode::accept(NodeVisitor *visitor) {
    visitor->visitContinueNode(this);
}

EvalStatus ContinueNode::eval(RuntimeContext &ctx) {
    return EvalStatus::CONTINUE;
}

// ###########################
// ##     ExportEnvNode     ##
// ###########################

ExportEnvNode::~ExportEnvNode() {
    delete this->exprNode;
    this->exprNode = 0;
}

void ExportEnvNode::setAttribute(FieldHandle *handle) {
    this->global = handle->isGlobal();
    this->varIndex = handle->getFieldIndex();
}

void ExportEnvNode::dump(Writer &writer) const {
    WRITE(envName);
    WRITE_PTR(exprNode);
    WRITE_PRIM(global);
    WRITE_PRIM(varIndex);
}

void ExportEnvNode::accept(NodeVisitor *visitor) {
    visitor->visitExportEnvNode(this);
}

EvalStatus ExportEnvNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->exprNode);
    ctx.exportEnv(this->envName, this->varIndex, this->global);
    return EvalStatus::SUCCESS;
}

// ###########################
// ##     ImportEnvNode     ##
// ###########################

void ImportEnvNode::setAttribute(FieldHandle *handle) {
    this->global = handle->isGlobal();
    this->varIndex = handle->getFieldIndex();
}

void ImportEnvNode::dump(Writer &writer) const {
    WRITE(envName);
    WRITE_PRIM(global);
    WRITE_PRIM(varIndex);
}

void ImportEnvNode::accept(NodeVisitor *visitor) {
    visitor->visitImportEnvNode(this);
}

EvalStatus ImportEnvNode::eval(RuntimeContext &ctx) {
    ctx.importEnv(this->envName, this->varIndex, this->global);
    return EvalStatus::SUCCESS;
}

// ###########################
// ##     TypeAliasNode     ##
// ###########################

TypeAliasNode::~TypeAliasNode() {
    delete this->targetTypeToken;
    this->targetTypeToken = 0;
}

void TypeAliasNode::dump(Writer &writer) const {
    WRITE(alias);
    WRITE_PTR(targetTypeToken);
}

void TypeAliasNode::accept(NodeVisitor *visitor) {
    visitor->visitTypeAliasNode(this);
}

EvalStatus TypeAliasNode::eval(RuntimeContext &ctx) {
    return EvalStatus::SUCCESS;    // do nothing.
}

// #####################
// ##     ForNode     ##
// #####################

ForNode::ForNode(unsigned int lineNum, Node *initNode, Node *condNode, Node *iterNode, BlockNode *blockNode) :
        Node(lineNum), initNode(initNode), condNode(condNode),
        iterNode(iterNode), blockNode(blockNode) {
    if(this->initNode == 0) {
        this->initNode = new EmptyNode();
    }

    if(this->condNode == 0) {
        this->condNode = new VarNode(lineNum, std::string(VAR_TRUE));
    }
    this->condNode->inCondition();

    if(this->iterNode == 0) {
        this->iterNode = new EmptyNode();
    }
}

ForNode::~ForNode() {
    delete this->initNode;
    this->initNode = 0;

    delete this->condNode;
    this->condNode = 0;

    delete this->iterNode;
    this->iterNode = 0;

    delete this->blockNode;
    this->blockNode = 0;
}

void ForNode::dump(Writer &writer) const {
    WRITE_PTR(initNode);
    WRITE_PTR(condNode);
    WRITE_PTR(iterNode);
    WRITE_PTR(blockNode);
}

void ForNode::accept(NodeVisitor *visitor) {
    visitor->visitForNode(this);
}

EvalStatus ForNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->initNode);

    CONTINUE:
    EVAL(ctx, this->condNode);
    if(TYPE_AS(Boolean_Object, ctx.pop())->getValue()) {
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
    this->condNode = 0;

    delete this->blockNode;
    this->blockNode = 0;
}

void WhileNode::dump(Writer &writer) const {
    WRITE_PTR(condNode);
    WRITE_PTR(blockNode);
}

void WhileNode::accept(NodeVisitor *visitor) {
    visitor->visitWhileNode(this);
}

EvalStatus WhileNode::eval(RuntimeContext &ctx) {
    CONTINUE:
    EVAL(ctx, this->condNode);
    if(TYPE_AS(Boolean_Object, ctx.pop())->getValue()) {
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
    this->blockNode = 0;

    delete this->condNode;
    this->condNode = 0;
}

void DoWhileNode::dump(Writer &writer) const {
    WRITE_PTR(blockNode);
    WRITE_PTR(condNode);
}

void DoWhileNode::accept(NodeVisitor *visitor) {
    visitor->visitDoWhileNode(this);
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
    if(TYPE_AS(Boolean_Object, ctx.pop())->getValue()) {
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
    if(isNode == 0) {
        return;
    }

    VarNode *varNode = dynamic_cast<VarNode *>(isNode->getTargetNode());
    if(varNode == 0) {
        return;
    }

    VarNode *exprNode = new VarNode(isNode->getLineNum(), std::string(varNode->getVarName()));
    CastNode *castNode = new CastNode(exprNode, isNode->getTargetTypeToken(), true);
    VarDeclNode *declNode =
            new VarDeclNode(isNode->getLineNum(), std::string(varNode->getVarName()), castNode, true);
    blockNode->insertNodeToFirst(declNode);
}

IfNode::IfNode(unsigned int lineNum, Node *condNode, BlockNode *thenNode) :
        Node(lineNum), condNode(condNode), thenNode(thenNode),
        elifCondNodes(), elifThenNodes(), elseNode(0) {
    this->condNode->inCondition();

    resolveIfIsStatement(this->condNode, this->thenNode);
}

IfNode::~IfNode() {
    delete this->condNode;
    this->condNode = 0;

    delete this->thenNode;
    this->thenNode = 0;

    unsigned int size = this->elifCondNodes.size();
    for(unsigned int i = 0; i < size; i++) {
        delete this->elifCondNodes[i];
        delete this->elifThenNodes[i];
    }
    this->elifCondNodes.clear();
    this->elifThenNodes.clear();

    delete this->elseNode;
    this->elseNode = 0;
}

void IfNode::addElifNode(Node *condNode, BlockNode *thenNode) {
    condNode->inCondition();
    this->elifCondNodes.push_back(condNode);
    this->elifThenNodes.push_back(thenNode);

    resolveIfIsStatement(condNode, thenNode);
}

BlockNode *IfNode::getElseNode() {
    if(this->elseNode == 0) {
        this->elseNode = new BlockNode(0);
    }
    return this->elseNode;
}

void IfNode::dump(Writer &writer) const {
    WRITE_PTR(condNode);
    WRITE_PTR(thenNode);
    WRITE(elifCondNodes);

    std::vector<Node *> elifThenNodes;
    for(BlockNode *elifThenNode : this->elifThenNodes) {
        elifThenNodes.push_back(elifThenNode);
    }
    WRITE(elifThenNodes);

    WRITE_PTR(elseNode);
}

void IfNode::accept(NodeVisitor *visitor) {
    visitor->visitIfNode(this);
}

EvalStatus IfNode::eval(RuntimeContext &ctx) {
    // if cond
    EVAL(ctx, this->condNode);

    // then block
    if(TYPE_AS(Boolean_Object, ctx.pop())->getValue()) {
        return this->thenNode->eval(ctx);
    }

    unsigned int size = this->elifCondNodes.size();
    for(unsigned i = 0; i < size; i++) {    // elif
        EVAL(ctx, this->elifCondNodes[i]);
        if(TYPE_AS(Boolean_Object, ctx.pop())->getValue()) {
            return this->elifThenNodes[i]->eval(ctx);
        }
    }

    // else
    return this->elseNode->eval(ctx);
}

// ########################
// ##     ReturnNode     ##
// ########################

ReturnNode::ReturnNode(unsigned int lineNum) :
        ReturnNode(lineNum, new EmptyNode(lineNum)) {
}

ReturnNode::~ReturnNode() {
    delete this->exprNode;
    this->exprNode = 0;
}

void ReturnNode::dump(Writer &writer) const {
    WRITE_PTR(exprNode);
}

void ReturnNode::accept(NodeVisitor *visitor) {
    visitor->visitReturnNode(this);
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
    this->exprNode = 0;
}

void ThrowNode::dump(Writer &writer) const {
    WRITE_PTR(exprNode);
}

void ThrowNode::accept(NodeVisitor *visitor) {
    visitor->visitThrowNode(this);
}

EvalStatus ThrowNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->exprNode);
    ctx.storeThrowObject();
    return EvalStatus::THROW;
}

// #######################
// ##     CatchNode     ##
// #######################

CatchNode::~CatchNode() {
    delete this->typeToken;
    this->typeToken = 0;

    delete this->blockNode;
    this->blockNode = 0;
}

void CatchNode::setAttribute(FieldHandle *handle) {
    this->varIndex = handle->getFieldIndex();
}

void CatchNode::dump(Writer &writer) const {
    WRITE(exceptionName);
    WRITE_PTR(typeToken);
    WRITE_PTR(exceptionType);
    WRITE_PTR(blockNode);
    WRITE_PRIM(varIndex);
}

void CatchNode::accept(NodeVisitor *visitor) {
    visitor->visitCatchNode(this);
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
    this->blockNode = 0;

    for(CatchNode *n : this->catchNodes) {
        delete n;
    }
    this->catchNodes.clear();

    delete this->finallyNode;
    this->finallyNode = 0;
}

void TryNode::addCatchNode(CatchNode *catchNode) {
    this->catchNodes.push_back(catchNode);
}

void TryNode::addFinallyNode(BlockNode *finallyNode) {
    if(this->finallyNode != 0) {
        delete this->finallyNode;
    }
    this->finallyNode = finallyNode;
}

BlockNode *TryNode::getFinallyNode() {
    if(this->finallyNode == 0) {
        this->finallyNode = new BlockNode(0);
    }
    return this->finallyNode;
}

void TryNode::dump(Writer &writer) const {
    WRITE_PTR(blockNode);

    std::vector<Node *> catchNodes;
    for(CatchNode *node : this->catchNodes) {
        catchNodes.push_back(node);
    }
    WRITE(catchNodes);

    WRITE_PTR(finallyNode);
}

void TryNode::accept(NodeVisitor *visitor) {
    visitor->visitTryNode(this);
}

EvalStatus TryNode::eval(RuntimeContext &ctx) {
    // eval try block
    EvalStatus status = this->blockNode->eval(ctx);

    if(status != EvalStatus::THROW) {  // eval finally
        EVAL(ctx, this->finallyNode);
        return status;
    } else {   // eval catch
        DSType *thrownType = ctx.getThrownObject()->getType();
        for(CatchNode *catchNode : this->catchNodes) {
            if(catchNode->getExceptionType()->isSameOrBaseTypeOf(thrownType)) {
                ctx.loadThrownObject();
                status = catchNode->eval(ctx);
                // eval finally
                EVAL(ctx, this->finallyNode);
                return status;
            }
        }
    }
    return status;
}

// #########################
// ##     VarDeclNode     ##
// #########################

VarDeclNode::VarDeclNode(unsigned int lineNum, std::string &&varName, Node *initValueNode, bool readOnly) :
        Node(lineNum), varName(std::move(varName)), readOnly(readOnly), global(false),
        varIndex(0), initValueNode(initValueNode) {
    if(this->initValueNode != nullptr) {
        this->initValueNode->inRightHandleSide();
    }
}

VarDeclNode::~VarDeclNode() {
    delete this->initValueNode;
    this->initValueNode = 0;
}

void VarDeclNode::setAttribute(FieldHandle *handle) {
    this->global = handle->isGlobal();
    this->varIndex = handle->getFieldIndex();
}

void VarDeclNode::dump(Writer &writer) const {
    WRITE(varName);
    WRITE_PRIM(readOnly);
    WRITE_PRIM(global);
    WRITE_PRIM(varIndex);
    WRITE_PTR(initValueNode);
}

void VarDeclNode::accept(NodeVisitor *visitor) {
    visitor->visitVarDeclNode(this);
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
    this->leftNode = 0;

    delete this->rightNode;
    this->rightNode = 0;
}

void AssignNode::dump(Writer &writer) const {
    WRITE_PTR(leftNode);
    WRITE_PTR(rightNode);

#define EACH_FLAG(OP, out, set) \
    OP(SELF_ASSIGN, out, set) \
    OP(FIELD_ASSIGN, out, set)

    std::string value;
    DECODE_BITSET(value, this->attributeSet, EACH_FLAG);
    writer.write(NAME(attributeSet), value);

#undef EACH_FLAG
}

void AssignNode::accept(NodeVisitor *visitor) {
    visitor->visitAssignNode(this);
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
            return ctx.storeField(accessNode->getRecvNode()->getType(),
                                  accessNode->getFieldName(), accessNode->getType());
        }
        ctx.storeField(index);
    } else {
        if(this->isSelfAssignment()) {
            EVAL(ctx, this->leftNode);
        }
        EVAL(ctx, this->rightNode);
        VarNode *varNode = static_cast<VarNode *>(this->leftNode);

        if(varNode->isEnv()) {
            ctx.exportEnv(varNode->getVarName(), index, varNode->isGlobal());
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
    node->leftNode = 0;

    Node *rightNode = node->rightNode;
    node->rightNode = 0;

    delete node;
    return std::make_pair(leftNode, rightNode);
}

// ###################################
// ##     ElementSelfAssignNode     ##
// ###################################

ElementSelfAssignNode::ElementSelfAssignNode(MethodCallNode *leftNode, BinaryOpNode *binaryNode) :
        Node(leftNode->getLineNum()),
        recvNode(), indexNode(),
        getterNode(), setterNode(), binaryNode(binaryNode) {
    // init recv, indexNode
    this->recvNode = leftNode->getRecvNode();
    leftNode->setRecvNode(nullptr);
    this->indexNode = leftNode->getArgsNode()->getNodes()[0];
    leftNode->getArgsNode()->setArg(0, nullptr);
    delete leftNode;

    // init getter node
    this->getterNode = new MethodCallNode(new DummyNode(), std::string(OP_GET));
    this->getterNode->getArgsNode()->addArg(new DummyNode());

    // init setter node
    this->setterNode = new MethodCallNode(new DummyNode(), std::string(OP_SET));
    this->setterNode->getArgsNode()->addArg(new DummyNode());
    this->setterNode->getArgsNode()->addArg(new DummyNode());
}

ElementSelfAssignNode::~ElementSelfAssignNode() {
    delete this->recvNode;
    this->recvNode = 0;

    delete this->indexNode;
    this->indexNode = 0;

    delete this->getterNode;
    this->getterNode = 0;

    delete this->setterNode;
    this->setterNode = 0;

    delete this->binaryNode;
    this->binaryNode = 0;
}

void ElementSelfAssignNode::setRecvType(DSType *type) {
    this->getterNode->getRecvNode()->setType(type);
    this->setterNode->getRecvNode()->setType(type);
}

void ElementSelfAssignNode::setIndexType(DSType *type) {
    this->getterNode->getArgsNode()->getNodes()[0]->setType(type);
    this->setterNode->getArgsNode()->getNodes()[0]->setType(type);
}

void ElementSelfAssignNode::dump(Writer &writer) const {
    WRITE_PTR(recvNode);
    WRITE_PTR(indexNode);
    WRITE_PTR(getterNode);
    WRITE_PTR(setterNode);
    WRITE_PTR(binaryNode);
}

void ElementSelfAssignNode::accept(NodeVisitor *visitor) {
    visitor->visitElementSelfAssignNode(this);
}

EvalStatus ElementSelfAssignNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->recvNode);
    EVAL(ctx, this->indexNode);
    ctx.dup2();

    EVAL(ctx, this->getterNode);
    EVAL(ctx, this->binaryNode);

    return this->setterNode->eval(ctx);
}


// ##########################
// ##     FunctionNode     ##
// ##########################

FunctionNode::~FunctionNode() {
    for(VarNode *n : this->paramNodes) {
        delete n;
    }
    this->paramNodes.clear();

    for(TypeToken *t : this->paramTypeTokens) {
        delete t;
    }
    this->paramTypeTokens.clear();

    delete this->returnTypeToken;
    this->returnTypeToken = 0;

    delete this->blockNode;
    this->blockNode = 0;
}

void FunctionNode::addParamNode(VarNode *node, TypeToken *paramType) {
    this->paramNodes.push_back(node);
    this->paramTypeTokens.push_back(paramType);
}

TypeToken *FunctionNode::getReturnTypeToken() {
    if(this->returnTypeToken == 0) {
        this->returnTypeToken = newVoidTypeToken();
    }
    return this->returnTypeToken;
}

void FunctionNode::setSourceName(const char *sourceName) {
    this->sourceName = sourceName;
}

const char *FunctionNode::getSourceName() {
    return this->sourceName;
}

void FunctionNode::dump(Writer &writer) const {
    WRITE(funcName);

    std::vector<Node *> paramNodes;
    for(VarNode *node : this->paramNodes) {
        paramNodes.push_back(node);
    }

    WRITE(paramNodes);
    WRITE(paramTypeTokens);
    WRITE_PTR(returnTypeToken);
    WRITE_PTR(returnType);
    WRITE_PTR(blockNode);
    WRITE_PRIM(maxVarNum);
    WRITE_PRIM(varIndex);
    WRITE(sourceName);
}

void FunctionNode::accept(NodeVisitor *visitor) {
    visitor->visitFunctionNode(this);
}

EvalStatus FunctionNode::eval(RuntimeContext &ctx) {
    ctx.setGlobal(this->varIndex, DSValue::create<UserFuncObject>(this));
    return EvalStatus::REMOVE;
}

// ###########################
// ##     InterfaceNode     ##
// ###########################

InterfaceNode::~InterfaceNode() {
    for(FunctionNode *node : this->methodDeclNodes) {
        delete node;
    }
    this->methodDeclNodes.clear();

    for(VarDeclNode *node : this->fieldDeclNodes) {
        delete node;
    }
    this->fieldDeclNodes.clear();

    for(TypeToken *t : this->fieldTypeTokens) {
        delete t;
    }
    this->fieldTypeTokens.clear();
}

void InterfaceNode::addMethodDeclNode(FunctionNode *methodDeclNode) {
    this->methodDeclNodes.push_back(methodDeclNode);
}

void InterfaceNode::addFieldDecl(VarDeclNode *node, TypeToken *typeToken) {
    this->fieldDeclNodes.push_back(node);
    this->fieldTypeTokens.push_back(typeToken);
}

void InterfaceNode::dump(Writer &writer) const {
    WRITE(interfaceName);

    std::vector<Node *> methodDeclNodes;
    for(FunctionNode *funcNode : this->methodDeclNodes) {
        methodDeclNodes.push_back(funcNode);
    }
    WRITE(methodDeclNodes);

    std::vector<Node *> fieldDeclNodes;
    for(VarDeclNode *node : this->fieldDeclNodes) {
        fieldDeclNodes.push_back(node);
    }
    WRITE(fieldDeclNodes);
    WRITE(fieldTypeTokens);
}

void InterfaceNode::accept(NodeVisitor *visitor) {
    visitor->visitInterfaceNode(this);
}

EvalStatus InterfaceNode::eval(RuntimeContext &ctx) {
    return EvalStatus::SUCCESS;    // do nothing
}

// #########################
// ##     BindVarNode     ##
// #########################

void BindVarNode::setAttribute(FieldHandle *handle) {
    this->varIndex = handle->getFieldIndex();
}

void BindVarNode::dump(Writer &writer) const {
    WRITE(varName);
    WRITE_PRIM(varIndex);
    //FIXME: value
}

void BindVarNode::accept(NodeVisitor *visitor) {
    visitor->visitBindVarNode(this);
}

EvalStatus BindVarNode::eval(RuntimeContext &ctx) {
    ctx.setGlobal(this->varIndex, this->value);
    return EvalStatus::SUCCESS;
}

// #######################
// ##     EmptyNode     ##
// #######################

void EmptyNode::dump(Writer &writer) const { } // do nothing

void EmptyNode::accept(NodeVisitor *visitor) {
    visitor->visitEmptyNode(this);
}

EvalStatus EmptyNode::eval(RuntimeContext &ctx) {
    return EvalStatus::SUCCESS; // do nothing
}

// #######################
// ##     DummyNode     ##
// #######################

void DummyNode::dump(Writer &writer) const { } // do nothing

void DummyNode::accept(NodeVisitor *visitor) {
    visitor->visitDummyNode(this);
}

EvalStatus DummyNode::eval(RuntimeContext &ctx) {
    return EvalStatus::SUCCESS; // do nothing
}

// ######################
// ##     RootNode     ##
// ######################

RootNode::RootNode() :
        RootNode(0) {
    static const char empty[] = "";
    this->sourceName = empty;
}

RootNode::~RootNode() {
    for(Node *n : this->nodeList) {
        delete n;
    }
    this->nodeList.clear();
}

const char *RootNode::getSourceName() {
    return this->sourceName;
}

void RootNode::addNode(Node *node) {
    node->setSourceName(this->sourceName);
    this->nodeList.push_back(node);
}

void RootNode::dump(Writer &writer) const {
    WRITE(nodeList);
    WRITE(sourceName);
    WRITE_PRIM(maxVarNum);
    WRITE_PRIM(maxGVarNum);
}

void RootNode::accept(NodeVisitor *visitor) {
    visitor->visitRootNode(this);
}

EvalStatus RootNode::eval(RuntimeContext &ctx) {
    ctx.clearCallStack();

    ctx.pushFuncContext(this);
    ctx.reserveGlobalVar(this->maxGVarNum);
    ctx.reserveLocalVar(this->maxVarNum);

    for(auto iter = this->nodeList.begin(); iter != this->nodeList.end();) {
        Node *node = *iter;
        EvalStatus status = node->eval(ctx);
        switch(status) {
        case EvalStatus::SUCCESS: {
            if(ctx.isToplevelPrinting()) {
                ctx.printStackTop(node->getType());
            } else if(!node->getType()->isVoidType()) {
                ctx.popNoReturn();
            }
            break;
        };
        case EvalStatus::THROW: {
            return status;
        };
        case EvalStatus::REMOVE: {
            iter = this->nodeList.erase(iter);
            continue;
        };
        default:
            fatal("illegal EvalStatus: %d\n", status);
            break;
        }
        ++iter;
    }
    return EvalStatus::SUCCESS;
}

// for node creation

std::string resolveUnaryOpName(TokenKind op) {
    if(op == PLUS) {    // +
        return std::string(OP_PLUS);
    } else if(op == MINUS) {    // -
        return std::string(OP_MINUS);
    } else if(op == NOT) {  // not
        return std::string(OP_NOT);
    } else {
        fatal("unsupported unary op: %s\n", TO_NAME(op));
        return std::string("");
    }
}

std::string resolveBinaryOpName(TokenKind op) {
    switch(op) {
    case PLUS:
        return std::string(OP_ADD);
    case MINUS:
        return std::string(OP_SUB);
    case MUL:
        return std::string(OP_MUL);
    case DIV:
        return std::string(OP_DIV);
    case MOD:
        return std::string(OP_MOD);
    case EQ:
        return std::string(OP_EQ);
    case NE:
        return std::string(OP_NE);
    case LA:
        return std::string(OP_LT);
    case RA:
        return std::string(OP_GT);
    case LE:
        return std::string(OP_LE);
    case GE:
        return std::string(OP_GE);
    case AND:
        return std::string(OP_AND);
    case OR:
        return std::string(OP_OR);
    case XOR:
        return std::string(OP_XOR);
    case RE_MATCH:
        return std::string(OP_RE_EQ);
    case RE_UNMATCH:
        return std::string(OP_RE_NE);
    default:
        fatal("unsupported binary op: %s\n", TO_NAME(op));
        return std::string("");
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

CallNode *createCallNode(Node *recvNode, ArgsNode *argsNode) {
    AccessNode *accessNode = dynamic_cast<AccessNode *>(recvNode);
    if(accessNode != 0) { // treat as method call
        auto pair = AccessNode::split(accessNode);
        return new MethodCallNode(pair.first, std::move(pair.second), argsNode);
    }
    return new ApplyNode(recvNode, argsNode);
}

ForNode *createForInNode(unsigned int lineNum, VarNode *varNode, Node *exprNode, BlockNode *blockNode) {
    // create for-init
    MethodCallNode *call_iter = new MethodCallNode(exprNode, std::string(OP_ITER));
    std::string reset_var_name(std::to_string(rand()));
    VarDeclNode *reset_varDecl = new VarDeclNode(lineNum, std::string(reset_var_name), call_iter, true);

    // create for-cond
    VarNode *reset_var = new VarNode(lineNum, std::string(reset_var_name));
    MethodCallNode *call_hasNext = new MethodCallNode(reset_var, std::string(OP_HAS_NEXT));

    // create forIn-init
    reset_var = new VarNode(lineNum, std::string(reset_var_name));
    MethodCallNode *call_next = new MethodCallNode(reset_var, std::string(OP_NEXT));
    VarDeclNode *init_var = new VarDeclNode(lineNum,
                                            VarNode::extractVarNameAndDelete(varNode), call_next, false);

    // insert init to block
    blockNode->insertNodeToFirst(init_var);

    return new ForNode(lineNum, reset_varDecl, call_hasNext, 0, blockNode);
}

Node *createSuffixNode(Node *leftNode, TokenKind op) {
    return createAssignNode(leftNode, op, new IntValueNode(leftNode->getLineNum(), 1));
}

Node *createAssignNode(Node *leftNode, TokenKind op, Node *rightNode) {
    rightNode->inRightHandleSide();

    /*
     * basic assignment
     */
    if(op == ASSIGN) {
        // assign to element(actually call SET)
        MethodCallNode *indexNode = dynamic_cast<MethodCallNode *>(leftNode);
        if(indexNode != 0 && indexNode->hasAttribute(MethodCallNode::INDEX)) {
            indexNode->setMethodName(std::string(OP_SET));
            indexNode->getArgsNode()->addArg(rightNode);
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
    if(indexNode != 0 && indexNode->hasAttribute(MethodCallNode::INDEX)) {
        return new ElementSelfAssignNode(indexNode, opNode);
    } else {
        // assign to variable or field
        return new AssignNode(leftNode, opNode, true);
    }
}

Node *createIndexNode(Node *recvNode, Node *indexNode) {
    MethodCallNode *methodCallNode = new MethodCallNode(recvNode, std::string(OP_GET));
    methodCallNode->setAttribute(MethodCallNode::INDEX);
    methodCallNode->getArgsNode()->addArg(indexNode);
    return methodCallNode;
}

Node *createBinaryOpNode(Node *leftNode, TokenKind op, Node *rightNode) {
    switch(op) {
    case COND_OR:
        return new CondOpNode(leftNode, rightNode, false);
    case COND_AND:
        return new CondOpNode(leftNode, rightNode, true);
    case ASSIGN:
    case ADD_ASSIGN:
    case SUB_ASSIGN:
    case MUL_ASSIGN:
    case DIV_ASSIGN:
    case MOD_ASSIGN:
        return createAssignNode(leftNode, op, rightNode);
    default:
        return new BinaryOpNode(leftNode, op, rightNode);
    }
}

} // namespace ast
} // namespace ydsh