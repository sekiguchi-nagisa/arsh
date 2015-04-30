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

#include <core/symbol.h>
#include <ast/Node.h>
#include <misc/debug.h>
#include <ast/dump.h>

#include <assert.h>
#include <stdlib.h>
#include <utility>

#include <unistd.h>

// helper macro
#define EVAL(ctx, node) \
    do {\
        EvalStatus status = (node)->eval(ctx);\
        if(status != EVAL_SUCCESS) {\
            return status;\
        }\
    } while(0)

namespace ydsh {
namespace ast {

// ##################
// ##     Node     ##
// ##################

Node::Node(unsigned int lineNum) :
        lineNum(lineNum), type() {
}

Node::~Node() {
}

unsigned int Node::getLineNum() const {
    return this->lineNum;
}

void Node::setType(DSType *type) {
    this->type = type;
}

DSType *Node::getType() const {
    return this->type;
}

void Node::inStringExprNode() { // do nothing
}

void Node::inCmdArgNode() { // do nothing
}

void Node::inCondition() {  // do nothing
}

void Node::inRightHandleSide() {    // do nothing
}

bool Node::isBlockEndNode() {
    return false;
}

void Node::setSourceName(const char *sourceName) {  // do nothing
}

const char *Node::getSourceName() {
    static const char empty[] = "";
    return empty;
}

// ##########################
// ##     IntValueNode     ##
// ##########################

IntValueNode::IntValueNode(unsigned int lineNum, IntKind kind, int value) :
        Node(lineNum), kind(kind), tempValue(value), value() {
}

IntValueNode::IntValueNode(unsigned int lineNum, int value) :
        IntValueNode(lineNum, INT32, value) {
}

IntValueNode *IntValueNode::newByte(unsigned int lineNum, unsigned char value) {
    return new IntValueNode(lineNum, BYTE, (int) value);
}

IntValueNode *IntValueNode::newInt16(unsigned int lineNum, short value) {
    return new IntValueNode(lineNum, INT16, (int) value);
}

IntValueNode *IntValueNode::newUint16(unsigned int lineNum, unsigned short value) {
    return new IntValueNode(lineNum, UINT16, (int) value);
}

IntValueNode *IntValueNode::newInt32(unsigned int lineNum, int value) {
    return new IntValueNode(lineNum, INT32, value);
}

IntValueNode *IntValueNode::newUint32(unsigned int lineNum, unsigned int value) {
    return new IntValueNode(lineNum, UINT32, (int) value);
}

IntValueNode::IntKind IntValueNode::getKind() {
    return this->kind;
}

std::shared_ptr<DSObject> IntValueNode::getValue() {
    return this->value;
}

void IntValueNode::setType(DSType *type) {
    this->type = type;
    this->value.reset(new Int_Object(this->type, this->tempValue));
}

void IntValueNode::dump(Writer &writer) const {
    WRITE_PRIM(tempValue);
    if (this->type == 0) {
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
    return EVAL_SUCCESS;
}

// ###########################
// ##     LongValueNode     ##
// ###########################

LongValueNode::LongValueNode(unsigned int lineNum, long value, bool unsignedValue) :
        Node(lineNum), tempValue(value), unsignedValue(unsignedValue), value() {
}

LongValueNode *LongValueNode::newInt64(unsigned int lineNum, long value) {
    return new LongValueNode(lineNum, value, false);
}

LongValueNode *LongValueNode::newUint64(unsigned int lineNum, unsigned long value) {
    return new LongValueNode(lineNum, (long) value, true);
}

const std::shared_ptr<DSObject> &LongValueNode::getValue() {
    return this->value;
}

bool LongValueNode::isUnsignedValue() {
    return this->unsignedValue;
}

void LongValueNode::setType(DSType *type) {
    this->type = type;
    this->value.reset(new Long_Object(this->type, this->tempValue));
}

void LongValueNode::dump(Writer &writer) const {
    WRITE_PRIM(tempValue);
    WRITE_PRIM(unsignedValue);
    if (this->type == 0) {
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
    return EVAL_SUCCESS;
}


// ############################
// ##     FloatValueNode     ##
// ############################

FloatValueNode::FloatValueNode(unsigned int lineNum, double value) :
        Node(lineNum), tempValue(value), value() {
}

std::shared_ptr<DSObject> FloatValueNode::getValue() {
    return this->value;
}

void FloatValueNode::setType(DSType *type) {
    this->type = type;
    this->value.reset(new Float_Object(this->type, this->tempValue));
}

void FloatValueNode::dump(Writer &writer) const {
    WRITE_PRIM(tempValue);
    if (this->type == 0) {
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
    return EVAL_SUCCESS;
}

// ############################
// ##     StringValueNode    ##
// ############################

StringValueNode::StringValueNode(std::string &&value) :
        StringValueNode(0, std::move(value)) {
}

StringValueNode::StringValueNode(unsigned int lineNum, std::string &&value) :
        Node(lineNum), tempValue(std::move(value)), value() {
}

StringValueNode::~StringValueNode() {
}

std::shared_ptr<DSObject> StringValueNode::getValue() {
    return this->value;
}

void StringValueNode::setType(DSType *type) {
    this->type = type;
    this->value.reset(
            new String_Object(this->type, std::move(this->tempValue)));
}

void StringValueNode::dump(Writer &writer) const {
    if (this->type == 0) {
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
    return EVAL_SUCCESS;
}

// ############################
// ##     ObjectPathNode     ##
// ############################

ObjectPathNode::ObjectPathNode(unsigned int lineNum, std::string &&value) :
        StringValueNode(lineNum, std::move(value)) {
}

void ObjectPathNode::accept(NodeVisitor *visitor) {
    visitor->visitObjectPathNode(this);
}

// ############################
// ##     StringExprNode     ##
// ############################

StringExprNode::StringExprNode(unsigned int lineNum) :
        Node(lineNum), nodes() {
}

StringExprNode::~StringExprNode() {
    for (Node *e : this->nodes) {
        delete e;
    }
    this->nodes.clear();
}

void StringExprNode::addExprNode(Node *node) {
    node->inStringExprNode();
    this->nodes.push_back(node);
}

const std::vector<Node *> &StringExprNode::getExprNodes() {
    return this->nodes;
}

void StringExprNode::dump(Writer &writer) const {
    WRITE(nodes);
}

void StringExprNode::accept(NodeVisitor *visitor) {
    visitor->visitStringExprNode(this);
}

EvalStatus StringExprNode::eval(RuntimeContext &ctx) {
    unsigned int size = this->nodes.size();
    if (size == 0) {
        ctx.push(std::make_shared<String_Object>(this->type));
    } else if (size == 1) {
        EVAL(ctx, this->nodes[0]);
        if(*this->nodes[0]->getType() != *ctx.pool.getStringType()) {
            return ctx.toInterp(this->nodes[0]->getLineNum());
        }
    } else {
        auto value = std::make_shared<String_Object>(this->type);
        for (Node *node : this->nodes) {
            EVAL(ctx, node);
            if(*node->getType() != *ctx.pool.getStringType()) {
                EvalStatus status = ctx.toInterp(node->getLineNum());
                if(status != EVAL_SUCCESS) {
                    return status;
                }
            }
            value->append(*TYPE_AS(String_Object, ctx.pop()));
        }
        ctx.push(value);
    }
    return EVAL_SUCCESS;
}

// #######################
// ##     ArrayNode     ##
// #######################

ArrayNode::ArrayNode(unsigned int lineNum, Node *node) :
        Node(lineNum), nodes() {
    this->nodes.push_back(node);
}

ArrayNode::~ArrayNode() {
    for (Node *e : this->nodes) {
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

const std::vector<Node *> &ArrayNode::getExprNodes() {
    return this->nodes;
}

void ArrayNode::dump(Writer &writer) const {
    WRITE(nodes);
}

void ArrayNode::accept(NodeVisitor *visitor) {
    visitor->visitArrayNode(this);
}

EvalStatus ArrayNode::eval(RuntimeContext &ctx) {
    auto value = std::make_shared<Array_Object>(this->type);
    for (Node *node : this->nodes) {
        EVAL(ctx, node);
        value->append(ctx.pop());
    }
    ctx.push(value);
    return EVAL_SUCCESS;
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
    for (Node *e : this->keyNodes) {
        delete e;
    }
    this->keyNodes.clear();
    for (Node *e : this->valueNodes) {
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

const std::vector<Node *> &MapNode::getKeyNodes() {
    return this->keyNodes;
}

void MapNode::setValueNode(unsigned int index, Node *valueNode) {
    this->valueNodes[index] = valueNode;
}

const std::vector<Node *> &MapNode::getValueNodes() {
    return this->valueNodes;
}

void MapNode::dump(Writer &writer) const {
    WRITE(keyNodes);
    WRITE(valueNodes);
}

void MapNode::accept(NodeVisitor *visitor) {
    visitor->visitMapNode(this);
}

EvalStatus MapNode::eval(RuntimeContext &ctx) {
    auto map = std::make_shared<Map_Object>(this->type);
    unsigned int size = this->keyNodes.size();
    for (unsigned int i = 0; i < size; i++) {
        EVAL(ctx, this->keyNodes[i]);
        auto key = ctx.pop();
        EVAL(ctx, this->valueNodes[i]);
        auto value = ctx.pop();
        map->set(key, value);
    }
    ctx.push(map);
    return EVAL_SUCCESS;
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
    for (Node *node : this->nodes) {
        delete node;
    }
    this->nodes.clear();
}

void TupleNode::addNode(Node *node) {
    this->nodes.push_back(node);
}

const std::vector<Node *> &TupleNode::getNodes() {
    return this->nodes;
}

void TupleNode::dump(Writer &writer) const {
    WRITE(nodes);
}

void TupleNode::accept(NodeVisitor *visitor) {
    visitor->visitTupleNode(this);
}

EvalStatus TupleNode::eval(RuntimeContext &ctx) {
    unsigned int size = this->nodes.size();
    auto value = std::make_shared<Tuple_Object>(this->type);
    for (unsigned int i = 0; i < size; i++) {
        EVAL(ctx, this->nodes[i]);
        value->set(i, ctx.pop());
    }
    ctx.push(value);
    return EVAL_SUCCESS;
}

// ############################
// ##     AssignableNode     ##
// ############################

AssignableNode::AssignableNode(unsigned int lineNum) :
        Node(lineNum), readOnly(false), index(-1) {
}

AssignableNode::~AssignableNode() {
}

bool AssignableNode::isReadOnly() {
    return this->readOnly;
}

int AssignableNode::getIndex() {
    return this->index;
}

// #####################
// ##     VarNode     ##
// #####################

VarNode::VarNode(unsigned int lineNum, std::string &&varName) :
        AssignableNode(lineNum), varName(std::move(varName)),
        global(false), env(false) {
}

const std::string &VarNode::getVarName() {
    return this->varName;
}

void VarNode::setAttribute(FieldHandle *handle) {
    this->readOnly = handle->isReadOnly();
    this->global = handle->isGlobal();
    this->env = handle->isEnv();
    this->index = handle->getFieldIndex();
}

void VarNode::dump(Writer &writer) const {
    WRITE_PRIM(readOnly);
    WRITE_PRIM(index);
    WRITE(varName);
    WRITE_PRIM(global);
    WRITE_PRIM(env);
}

void VarNode::accept(NodeVisitor *visitor) {
    visitor->visitVarNode(this);
}

bool VarNode::isGlobal() {
    return this->global;
}

bool VarNode::isEnv() {
    return this->env;
}

int VarNode::getVarIndex() {
    return this->index;
}

EvalStatus VarNode::eval(RuntimeContext &ctx) {
    if (this->global) {
        ctx.getGlobal(this->index);
    } else {
        ctx.getLocal(this->index);
    }
    if (this->type != 0 && this->type->isFuncType()) {
        ctx.peek()->setType(this->type);
    }
    return EVAL_SUCCESS;
}

std::string VarNode::extractVarNameAndDelete(VarNode *node) {
    std::string name(std::move(node->varName));
    delete node;
    return name;
}

// ########################
// ##     AccessNode     ##
// ########################

AccessNode::AccessNode(Node *recvNode, std::string &&fieldName) :
        AssignableNode(recvNode->getLineNum()), recvNode(recvNode),
        fieldName(std::move(fieldName)), additionalOp(NOP) {
}

AccessNode::~AccessNode() {
    delete this->recvNode;
    this->recvNode = 0;
}

Node *AccessNode::getRecvNode() {
    return this->recvNode;
}

void AccessNode::setFieldName(const std::string &fieldName) {
    this->fieldName = fieldName;
}

const std::string &AccessNode::getFieldName() {
    return this->fieldName;
}

void AccessNode::setAttribute(FieldHandle *handle) {
    this->readOnly = handle->isReadOnly();
    this->index = handle->getFieldIndex();
}

int AccessNode::getFieldIndex() {
    return this->index;
}

void AccessNode::setAdditionalOp(AccessNode::AdditionalOp op) {
    this->additionalOp = op;
}

AccessNode::AdditionalOp AccessNode::getAdditionnalOp() {
    return this->additionalOp;
}

void AccessNode::dump(Writer &writer) const {
    WRITE_PRIM(readOnly);
    WRITE_PRIM(index);
    WRITE_PTR(recvNode);
    WRITE(fieldName);

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

    switch (this->additionalOp) {
    case NOP: {
        ctx.getField(this->index);
        if (this->type != 0 && this->type->isFuncType()) {
            ctx.peek()->setType(this->type);
        }
        break;
    }
    case DUP_RECV: {
        ctx.dupAndGetField(this->index);
        if (this->type != 0 && this->type->isFuncType()) {
            ctx.peek()->setType(this->type);
        }
        break;
    }
    }

    return EVAL_SUCCESS;
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
    static const unsigned long tag = 1L << 63;

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

Node *CastNode::getExprNode() {
    return this->exprNode;
}

TypeToken *CastNode::getTargetTypeToken() const {
    static const unsigned long mask = ~(1L << 63);
    if((long) this->targetTypeToken < 0) {
        TypeToken *tok = (TypeToken *) (mask & (unsigned long) this->targetTypeToken);
        return tok;
    }
    return this->targetTypeToken;
}

void CastNode::setOpKind(CastNode::CastOp opKind) {
    this->opKind = opKind;
}

CastNode::CastOp CastNode::getOpKind() {
    return this->opKind;
}

void CastNode::dump(Writer &writer) const {
    WRITE_PTR(exprNode);
    TypeToken *targetTypeToken = this->getTargetTypeToken();
    WRITE_PTR(targetTypeToken);

#define EACH_ENUM(OP, out) \
    OP(NOP, out) \
    OP(INT_TO_FLOAT, out) \
    OP(FLOAT_TO_INT, out) \
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

    switch (this->opKind) {
    case NOP:
        break;
    case INT_TO_FLOAT: {
        int value = TYPE_AS(Int_Object, ctx.pop())->getValue();
        ctx.push(std::make_shared<Float_Object>(this->type, (double) value));
        break;
    }
    case FLOAT_TO_INT: {
        double value = TYPE_AS(Float_Object, ctx.pop())->getValue();
        ctx.push(std::make_shared<Int_Object>(this->type, (int) value));
        break;
    }
    case TO_STRING: {
        return ctx.toString(this->getLineNum());
    }
    case CHECK_CAST: {
        return ctx.checkCast(this->lineNum, this->type) ? EVAL_SUCCESS : EVAL_THROW;
    }
    }

    return EVAL_SUCCESS;
}

// ############################
// ##     InstanceOfNode     ##
// ############################

InstanceOfNode::InstanceOfNode(Node *targetNode, TypeToken *type) :
        Node(targetNode->getLineNum()), targetNode(targetNode), targetTypeToken(type),
        targetType(0), opKind(ALWAYS_FALSE) {
}

InstanceOfNode::~InstanceOfNode() {
    delete this->targetNode;
    this->targetNode = 0;

    delete this->targetTypeToken;
    this->targetTypeToken = 0;
}

Node *InstanceOfNode::getTargetNode() {
    return this->targetNode;
}

TypeToken *InstanceOfNode::getTargetTypeToken() {
    return this->targetTypeToken;
}

void InstanceOfNode::setTargetType(DSType *targetType) {
    this->targetType = targetType;
}

DSType *InstanceOfNode::getTargetType() {
    return this->targetType;
}

void InstanceOfNode::setOpKind(InstanceOfNode::InstanceOfOp opKind) {
    this->opKind = opKind;
}

InstanceOfNode::InstanceOfOp InstanceOfNode::getOpKind() {
    return this->opKind;
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

    switch (this->opKind) {
    case INSTANCEOF:
        ctx.instanceOf(this->targetType);
        break;
    case ALWAYS_TRUE:
        ctx.pop();
        ctx.push(ctx.trueObj);
        break;
    case ALWAYS_FALSE:
        ctx.pop();
        ctx.push(ctx.falseObj);
        break;
    }
    return EVAL_SUCCESS;
}

// ######################
// ##     ArgsNode     ##
// ######################

ArgsNode::ArgsNode() :
        Node(0), nodes() {
}

ArgsNode::~ArgsNode() {
    for (Node *node : this->nodes) {
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

const std::vector<Node *> &ArgsNode::getNodes() {
    return this->nodes;
}

void ArgsNode::dump(Writer &writer) const {
    WRITE(nodes);
}

void ArgsNode::accept(NodeVisitor *visitor) {
    visitor->visitArgsNode(this);
}

EvalStatus ArgsNode::eval(RuntimeContext &ctx) {
    for (Node *node : this->nodes) {
        EVAL(ctx, node);
    }
    return EVAL_SUCCESS;
}

// ######################
// ##     CallNode     ##
// ######################

CallNode::CallNode(unsigned int lineNum, ArgsNode *argsNode) :
        Node(lineNum), argsNode(argsNode) {
}

CallNode::~CallNode() {
    delete this->argsNode;
    this->argsNode = 0;
}

ArgsNode *CallNode::getArgsNode() {
    return this->argsNode;
}

// #######################
// ##     ApplyNode     ##
// #######################

ApplyNode::ApplyNode(Node *exprNode, ArgsNode *argsNode) :
        CallNode(exprNode->getLineNum(), argsNode), exprNode(exprNode) {
}

ApplyNode::~ApplyNode() {
    delete this->exprNode;
}

Node *ApplyNode::getExprNode() {
    return this->exprNode;
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

MethodCallNode::MethodCallNode(Node *recvNode, std::string &&methodName) :
        MethodCallNode(recvNode, std::move(methodName), new ArgsNode()) {
}

MethodCallNode::MethodCallNode(Node *recvNode, std::string &&methodName, ArgsNode *argsNode) :
        CallNode(recvNode->getLineNum(), argsNode),
        recvNode(recvNode), methodName(methodName), methodIndex(), attributeSet() {
}

MethodCallNode::~MethodCallNode() {
    delete this->recvNode;
    this->recvNode = 0;
}

void MethodCallNode::setRecvNode(Node *node) {
    this->recvNode = node;
}

Node *MethodCallNode::getRecvNode() {
    return this->recvNode;
}

void MethodCallNode::setMethodName(std::string &&methodName) {
    this->methodName = methodName;
}

const std::string &MethodCallNode::getMethodName() {
    return this->methodName;
}

void MethodCallNode::setAttribute(flag8_t attribute) {
    setFlag(this->attributeSet, attribute);
}

bool MethodCallNode::hasAttribute(flag8_t attribute) {
    return hasFlag(this->attributeSet, attribute);
}

void MethodCallNode::setMethodIndex(unsigned int index) {
    this->methodIndex = index;
}

unsigned int MethodCallNode::getMethodIndex() {
    return this->methodIndex;
}

void MethodCallNode::dump(Writer &writer) const {
    WRITE_PTR(recvNode);
    WRITE(methodName);
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

    /**
     * include receiver
     */
    unsigned int paramSize = this->argsNode->getNodes().size() + 1;

    return ctx.callMethod(this->lineNum, this->type->isVoidType(), this->methodIndex, paramSize);
}

// #####################
// ##     NewNode     ##
// #####################

NewNode::NewNode(unsigned int lineNum, TypeToken *targetTypeToken, ArgsNode *argsNode) :
        Node(lineNum), targetTypeToken(targetTypeToken),
        argsNode(argsNode) {
}

NewNode::~NewNode() {
    delete this->targetTypeToken;
    this->targetTypeToken = 0;
}

TypeToken *NewNode::getTargetTypeToken() {
    return this->targetTypeToken;
}

ArgsNode *NewNode::getArgsNode() {
    return this->argsNode;
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

// ##########################
// ##     BinaryOpNode     ##
// ##########################

BinaryOpNode::BinaryOpNode(Node *leftNode, TokenKind op, Node *rightNode) :
        Node(leftNode->getLineNum()),
        leftNode(leftNode), rightNode(rightNode), op(op), methodCallNode(0) {
}

BinaryOpNode::~BinaryOpNode() {
    delete this->leftNode;
    this->leftNode = 0;

    delete this->rightNode;
    this->rightNode = 0;

    delete this->methodCallNode;
    this->methodCallNode = 0;
}

Node *BinaryOpNode::getLeftNode() {
    return this->leftNode;
}

void BinaryOpNode::setLeftNode(Node *leftNode) {
    this->leftNode = leftNode;
}

Node *BinaryOpNode::getRightNode() {
    return this->rightNode;
}

void BinaryOpNode::setRightNode(Node *rightNode) {
    this->rightNode = rightNode;
}

MethodCallNode *BinaryOpNode::creatApplyNode() {
    this->methodCallNode = new MethodCallNode(this->leftNode, resolveBinaryOpName(this->op));
    this->methodCallNode->getArgsNode()->addArg(this->rightNode);

    // assign null to prevent double free.
    this->leftNode = 0;
    this->rightNode = 0;

    return this->methodCallNode;
}

MethodCallNode *BinaryOpNode::getApplyNode() {
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

GroupNode::GroupNode(unsigned int lineNum, Node *exprNode) :
        Node(lineNum), exprNode(exprNode) {
}

GroupNode::~GroupNode() {
    delete this->exprNode;
    this->exprNode = 0;
}

Node *GroupNode::getExprNode() {
    return this->exprNode;
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

Node *CondOpNode::getLeftNode() {
    return this->leftNode;
}

Node *CondOpNode::getRightNode() {
    return this->rightNode;
}

bool CondOpNode::isAndOp() {
    return this->andOp;
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

    if (this->andOp) {   // and
        if (TYPE_AS(Boolean_Object, ctx.peek())->getValue()) {
            ctx.pop();
            return this->rightNode->eval(ctx);
        } else {
            return EVAL_SUCCESS;
        }
    } else {    // or
        if (TYPE_AS(Boolean_Object, ctx.peek())->getValue()) {
            return EVAL_SUCCESS;
        } else {
            ctx.pop();
            return this->rightNode->eval(ctx);
        }
    }
}

// ########################
// ##     CmdArgNode     ##
// ########################

CmdArgNode::CmdArgNode(Node *segmentNode) :
        Node(segmentNode->getLineNum()), segmentNodes() {
    this->addSegmentNode(segmentNode);
}

CmdArgNode::~CmdArgNode() {
    for (Node *e : this->segmentNodes) {
        delete e;
    }
    this->segmentNodes.clear();
}

void CmdArgNode::addSegmentNode(Node *node) {
    node->inCmdArgNode();
    this->segmentNodes.push_back(node);
}

const std::vector<Node *> &CmdArgNode::getSegmentNodes() {
    return this->segmentNodes;
}

void CmdArgNode::dump(Writer &writer) const {
    WRITE(segmentNodes);
}

void CmdArgNode::accept(NodeVisitor *visitor) {
    visitor->visitCmdArgNode(this);
}

EvalStatus CmdArgNode::eval(RuntimeContext &ctx) {
    //FIXME: concate segment node
    EVAL(ctx, this->segmentNodes[0]);
    DSType *type = this->segmentNodes[0]->getType();
    if(*type != *ctx.pool.getStringType() && *type != *ctx.pool.getStringArrayType()) {
        return ctx.toCmdArg(this->lineNum);
    }
    return EVAL_SUCCESS;
}

// #####################
// ##     CmdNode     ##
// #####################

CmdNode::CmdNode(unsigned int lineNum, std::string &&commandName) :
        Node(lineNum), commandName(std::move(commandName)), argNodes(), redirOptions() {
}

CmdNode::~CmdNode() {
    for (CmdArgNode *e : this->argNodes) {
        delete e;
    }
    this->argNodes.clear();

    for (const std::pair<int, Node *> &pair : this->redirOptions) {
        delete pair.second;
    }
    this->redirOptions.clear();
}

const std::string &CmdNode::getCommandName() {
    return this->commandName;
}

void CmdNode::addArgNode(CmdArgNode *node) {
    this->argNodes.push_back(node);
}

const std::vector<CmdArgNode *> &CmdNode::getArgNodes() {
    return this->argNodes;
}

void CmdNode::addRedirOption(std::pair<int, Node *> &&optionPair) {
    this->redirOptions.push_back(std::move(optionPair));
}

const std::vector<std::pair<int, Node *>> &CmdNode::getRedirOptions() {
    return this->redirOptions;
}

void CmdNode::dump(Writer &writer) const {
    WRITE(commandName);

    std::vector<Node *> argNodes;
    for (CmdArgNode *node : this->argNodes) {
        argNodes.push_back(node);
    }
    WRITE(argNodes);
    //FIXME: redirOption
}

void CmdNode::accept(NodeVisitor *visitor) {
    visitor->visitCmdNode(this);
}

EvalStatus CmdNode::eval(RuntimeContext &ctx) { //FIXME: redirect
    std::shared_ptr<ProcContext> proc(new ProcContext(ctx, this->commandName));
    for (Node *node : this->argNodes) {
        EVAL(ctx, node);
        proc->addParam(ctx.pop());
    }
    ctx.push(proc);
    return EVAL_SUCCESS;
}

// ##########################
// ##     PipedCmdNode     ##
// ##########################

PipedCmdNode::PipedCmdNode(CmdNode *node) :
        Node(node->getLineNum()), cmdNodes(), asBool(false) {
    this->cmdNodes.push_back(node);
}

PipedCmdNode::~PipedCmdNode() {
    for (CmdNode *p : this->cmdNodes) {
        delete p;
    }
    this->cmdNodes.clear();
}

void PipedCmdNode::addCmdNodes(CmdNode *node) {
    this->cmdNodes.push_back(node);
}

const std::vector<CmdNode *> &PipedCmdNode::getCmdNodes() {
    return this->cmdNodes;
}

void PipedCmdNode::inCondition() {
    this->asBool = true;
}

bool PipedCmdNode::treatAsBool() {
    return this->asBool;
}

void PipedCmdNode::dump(Writer &writer) const {
    std::vector<Node *> cmdNodes;
    for (CmdNode *node : this->cmdNodes) {
        cmdNodes.push_back(node);
    }

    WRITE(cmdNodes);
}

void PipedCmdNode::accept(NodeVisitor *visitor) {
    visitor->visitPipedCmdNode(this);
}

EvalStatus PipedCmdNode::eval(RuntimeContext &ctx) {
    unsigned int size = this->cmdNodes.size();
    ProcGroup group(size);

    for (unsigned int i = 0; i < size; i++) {
        EVAL(ctx, this->cmdNodes[i]);
        group.addProc(i, std::dynamic_pointer_cast<ProcContext>(ctx.pop()));
    }
    group.execProcs();

    if(*this->type == *ctx.pool.getBooleanType()) {
        if(ctx.exitStatus->value == 0) {
            ctx.push(ctx.trueObj);
        } else {
            ctx.push(ctx.falseObj);
        }
    }

    return EVAL_SUCCESS;
}

// ###########################
// ##     CmdContextNode    ##
// ###########################

CmdContextNode::CmdContextNode(Node *exprNode) :
        Node(exprNode->getLineNum()),
        exprNode(exprNode), attributeSet(0) {
    if(dynamic_cast<CondOpNode*>(exprNode) != 0) {
        this->setAttribute(CONDITION);
    }
}

CmdContextNode::~CmdContextNode() {
    delete this->exprNode;
    this->exprNode = 0;
}

Node *CmdContextNode::getExprNode() {
    return this->exprNode;
}

void CmdContextNode::setAttribute(flag8_t attribute) {
    setFlag(this->attributeSet, attribute);
}

void CmdContextNode::unsetAttribute(flag8_t attribute) {
    unsetFlag(this->attributeSet, attribute);
}

bool CmdContextNode::hasAttribute(flag8_t attribute) {
    return hasFlag(this->attributeSet, attribute);
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
            exit(RUNTIME_ERROR);    //FIXME: throw exception
        }

        pid_t pid = fork();
        if(pid > 0) {   // parent process
            close(pipefds[WRITE_PIPE]);

            std::shared_ptr<DSObject> obj;

            if(*this->type == *ctx.pool.getStringType()) {  // capture stdout as String
                static const int bufSize = 256;
                char buf[bufSize + 1];
                int readSize = 0;
                std::string str;
                while((readSize = read(pipefds[READ_PIPE], &buf, bufSize)) > 0) {
                    if(readSize == bufSize) {
                        buf[bufSize] = '\0';
                        str += buf;
                    } else {
                        // find last index of no newline
                        int endIndex = readSize - 1;
                        for(; endIndex > -1; endIndex--) {
                            if(buf[endIndex] != '\n') {
                                endIndex++;
                                break;
                            }
                        }

                        // copy to str
                        for(unsigned int i = 0; i < endIndex; i++) {
                            str += (unsigned char)buf[i];
                        }
                    }
                }

                obj.reset(new String_Object(this->type, std::move(str)));
            } else {    // capture stdout as String Array
                static const int bufSize = 256;
                char buf[bufSize];
                int readSize;
                std::string str;
                Array_Object *array = new Array_Object(this->type);
                while((readSize = read(pipefds[READ_PIPE], &buf, bufSize))> 0) {
                    for(unsigned int i = 0; i < readSize; i++) {
                        char ch = buf[i];
                        switch(ch) {
                        case ' ':
                        case '\t':
                        case '\n': {
                            if(!str.empty()) {
                                array->append(std::make_shared<String_Object>(
                                        ctx.pool.getStringType(), std::move(str)));
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
                    array->append(std::make_shared<String_Object>(
                            ctx.pool.getStringType(), std::move(str)));
                }

                obj.reset(array);
            }
            close(pipefds[READ_PIPE]);

            // wait exit
            int status;
            waitpid(pid, &status, 0);

            // push object
            ctx.push(obj);
            return EVAL_SUCCESS;
        } else if(pid == 0) {   // child process
            dup2(pipefds[WRITE_PIPE], STDOUT_FILENO);
            close(pipefds[READ_PIPE]);
            close(pipefds[WRITE_PIPE]);

            this->exprNode->eval(ctx);  //FIXME: error reporting
            exit(0);
        } else {
            perror("fork failed");
            exit(RUNTIME_ERROR);    //FIXME: throw exception
        }
    }


    return this->exprNode->eval(ctx);
}

// ########################
// ##     AssertNode     ##
// ########################

AssertNode::AssertNode(unsigned int lineNum, Node *condNode) :
        Node(lineNum), condNode(condNode) {
    this->condNode->inCondition();
}

AssertNode::~AssertNode() {
    delete this->condNode;
    this->condNode = 0;
}

Node *AssertNode::getCondNode() {
    return this->condNode;
}

void AssertNode::dump(Writer &writer) const {
    WRITE_PTR(condNode);
}

void AssertNode::accept(NodeVisitor *visitor) {
    visitor->visitAssertNode(this);
}

EvalStatus AssertNode::eval(RuntimeContext &ctx) {
    if (ctx.assertion) {
        EVAL(ctx, this->condNode);
        ctx.checkAssertion(this->condNode->getLineNum());
    }
    return EVAL_SUCCESS;
}

// #######################
// ##     BlockNode     ##
// #######################

BlockNode::BlockNode(unsigned int lineNum) :
        Node(lineNum), nodeList() {
}

BlockNode::~BlockNode() {
    for (Node *n : this->nodeList) {
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

const std::list<Node *> &BlockNode::getNodeList() {
    return this->nodeList;
}

void BlockNode::dump(Writer &writer) const {
    WRITE(nodeList);
}

void BlockNode::accept(NodeVisitor *visitor) {
    visitor->visitBlockNode(this);
}

EvalStatus BlockNode::eval(RuntimeContext &ctx) {
    for (Node *node : this->nodeList) {
        EvalStatus status = node->eval(ctx);
        if (!node->getType()->isVoidType()) {
            ctx.pop();
        }
        if (status != EVAL_SUCCESS) {
            return status;
        }
    }
    return EVAL_SUCCESS;
}

// ######################
// ##     BlockEnd     ##
// ######################

BlockEndNode::BlockEndNode(unsigned int lineNum) :
        Node(lineNum) {
}

BlockEndNode::~BlockEndNode() {
}

bool BlockEndNode::isBlockEndNode() {
    return true;
}

// #######################
// ##     BreakNode     ##
// #######################

BreakNode::BreakNode(unsigned int lineNum) :
        BlockEndNode(lineNum) {
}

void BreakNode::dump(Writer &writer) const {
    // do nothing
}

void BreakNode::accept(NodeVisitor *visitor) {
    visitor->visitBreakNode(this);
}

EvalStatus BreakNode::eval(RuntimeContext &ctx) {
    return EVAL_BREAK;
}

// ##########################
// ##     ContinueNode     ##
// ##########################

ContinueNode::ContinueNode(unsigned int lineNum) :
        BlockEndNode(lineNum) {
}

void ContinueNode::dump(Writer &writer) const {
    // do nothing
}

void ContinueNode::accept(NodeVisitor *visitor) {
    visitor->visitContinueNode(this);
}

EvalStatus ContinueNode::eval(RuntimeContext &ctx) {
    return EVAL_CONTINUE;
}

// ###########################
// ##     ExportEnvNode     ##
// ###########################

ExportEnvNode::ExportEnvNode(unsigned int lineNum, std::string &&envName, Node *exprNode) :
        Node(lineNum), envName(std::move(envName)), exprNode(exprNode),
        global(false), varIndex(0) {
}

ExportEnvNode::~ExportEnvNode() {
    delete this->exprNode;
    this->exprNode = 0;
}

const std::string &ExportEnvNode::getEnvName() {
    return this->envName;
}

Node *ExportEnvNode::getExprNode() {
    return this->exprNode;
}

void ExportEnvNode::setAttribute(FieldHandle *handle) {
    this->global = handle->isGlobal();
    this->varIndex = handle->getFieldIndex();
}

bool ExportEnvNode::isGlobal() {
    return this->global;
}

int ExportEnvNode::getVarIndex() {
    return this->varIndex;
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
    return EVAL_SUCCESS;
}

// ###########################
// ##     ImportEnvNode     ##
// ###########################

ImportEnvNode::ImportEnvNode(unsigned int lineNum, std::string &&envName) :
        Node(lineNum), envName(std::move(envName)), global(false), varIndex(-1) {
}

const std::string &ImportEnvNode::getEnvName() {
    return this->envName;
}

void ImportEnvNode::setAttribute(FieldHandle *handle) {
    this->global = handle->isGlobal();
    this->varIndex = handle->getFieldIndex();
}

bool ImportEnvNode::isGlobal() {
    return this->global;
}

int ImportEnvNode::getVarIndex() {
    return this->varIndex;
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
    return EVAL_SUCCESS;
}

// ###########################
// ##     TypeAliasNode     ##
// ###########################

TypeAliasNode::TypeAliasNode(unsigned int lineNum, std::string &&alias, TypeToken *targetTypeToken) :
        Node(lineNum), alias(alias), targetTypeToken(targetTypeToken) {
}

TypeAliasNode::TypeAliasNode(const char *alias, const char *targetTypeName) :
        Node(0), alias(std::string(alias)),
        targetTypeToken(new ClassTypeToken(0, std::string(targetTypeName))) {
}


TypeAliasNode::~TypeAliasNode() {
    delete this->targetTypeToken;
    this->targetTypeToken = 0;
}

const std::string &TypeAliasNode::getAlias() {
    return this->alias;
}

TypeToken *TypeAliasNode::getTargetTypeToken() {
    return this->targetTypeToken;
}

void TypeAliasNode::dump(Writer &writer) const {
    WRITE(alias);
    WRITE_PTR(targetTypeToken);
}

void TypeAliasNode::accept(NodeVisitor *visitor) {
    visitor->visitTypeAliasNode(this);
}

EvalStatus TypeAliasNode::eval(RuntimeContext &ctx) {
    return EVAL_SUCCESS;    // do nothing.
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
        this->condNode = new VarNode(lineNum, std::string(TRUE));
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

Node *ForNode::getInitNode() {
    return this->initNode;
}

Node *ForNode::getCondNode() {
    return this->condNode;
}

Node *ForNode::getIterNode() {
    return this->iterNode;
}

BlockNode *ForNode::getBlockNode() {
    return this->blockNode;
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
    if (TYPE_AS(Boolean_Object, ctx.pop())->getValue()) {
        EvalStatus status = this->blockNode->eval(ctx);
        switch (status) {
        case EVAL_BREAK:
            break;
        case EVAL_SUCCESS:
        case EVAL_CONTINUE:
            EVAL(ctx, this->iterNode);
            goto CONTINUE;
        default:
            return status;
        }
    }

    return EVAL_SUCCESS;
}

// #######################
// ##     WhileNode     ##
// #######################

WhileNode::WhileNode(unsigned int lineNum, Node *condNode, BlockNode *blockNode) :
        Node(lineNum), condNode(condNode), blockNode(blockNode) {
    this->condNode->inCondition();
}

WhileNode::~WhileNode() {
    delete this->condNode;
    this->condNode = 0;

    delete this->blockNode;
    this->blockNode = 0;
}

Node *WhileNode::getCondNode() {
    return this->condNode;
}

BlockNode *WhileNode::getBlockNode() {
    return this->blockNode;
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
    if (TYPE_AS(Boolean_Object, ctx.pop())->getValue()) {
        EvalStatus status = this->blockNode->eval(ctx);
        switch (status) {
        case EVAL_BREAK:
            break;
        case EVAL_SUCCESS:
        case EVAL_CONTINUE:
            goto CONTINUE;
        default:
            return status;
        }
    }

    return EVAL_SUCCESS;
}

// #########################
// ##     DoWhileNode     ##
// #########################

DoWhileNode::DoWhileNode(unsigned int lineNum, BlockNode *blockNode, Node *condNode) :
        Node(lineNum), blockNode(blockNode), condNode(condNode) {
    this->condNode->inCondition();
}

DoWhileNode::~DoWhileNode() {
}

BlockNode *DoWhileNode::getBlockNode() {
    return this->blockNode;
}

Node *DoWhileNode::getCondNode() {
    return this->condNode;
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
    switch (status) {
    case EVAL_BREAK:
        goto BREAK;
    case EVAL_SUCCESS:
    case EVAL_CONTINUE:
        break;
    default:
        return status;
    }

    EVAL(ctx, this->condNode);
    if (TYPE_AS(Boolean_Object, ctx.pop())->getValue()) {
        goto CONTINUE;
    }

    BREAK:
    return EVAL_SUCCESS;
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
    for (unsigned int i = 0; i < size; i++) {
        delete this->elifCondNodes[i];
        delete this->elifThenNodes[i];
    }
    this->elifCondNodes.clear();
    this->elifThenNodes.clear();

    delete this->elseNode;
    this->elseNode = 0;
}

Node *IfNode::getCondNode() {
    return this->condNode;
}

BlockNode *IfNode::getThenNode() {
    return this->thenNode;
}

void IfNode::addElifNode(Node *condNode, BlockNode *thenNode) {
    condNode->inCondition();
    this->elifCondNodes.push_back(condNode);
    this->elifThenNodes.push_back(thenNode);

    resolveIfIsStatement(condNode, thenNode);
}

const std::vector<Node *> &IfNode::getElifCondNodes() {
    return this->elifCondNodes;
}

const std::vector<BlockNode *> &IfNode::getElifThenNodes() {
    return this->elifThenNodes;
}

void IfNode::addElseNode(BlockNode *elseNode) {
    this->elseNode = elseNode;
}

BlockNode *IfNode::getElseNode() {
    if (this->elseNode == 0) {
        this->elseNode = new BlockNode(0);
    }
    return this->elseNode;
}

void IfNode::dump(Writer &writer) const {
    WRITE_PTR(condNode);
    WRITE_PTR(thenNode);
    WRITE(elifCondNodes);

    std::vector<Node *> elifThenNodes;
    for (BlockNode *elifThenNode : this->elifThenNodes) {
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
    if (TYPE_AS(Boolean_Object, ctx.pop())->getValue()) {
        return this->thenNode->eval(ctx);
    }

    unsigned int size = this->elifCondNodes.size();
    for (unsigned i = 0; i < size; i++) {    // elif
        EVAL(ctx, this->elifCondNodes[i]);
        if (TYPE_AS(Boolean_Object, ctx.pop())->getValue()) {
            return this->elifThenNodes[i]->eval(ctx);
        }
    }

    // else
    return this->elseNode->eval(ctx);
}

// ########################
// ##     ReturnNode     ##
// ########################

ReturnNode::ReturnNode(unsigned int lineNum, Node *exprNode) :
        BlockEndNode(lineNum), exprNode(exprNode) {
}

ReturnNode::ReturnNode(unsigned int lineNum) :
        BlockEndNode(lineNum), exprNode() {
}

ReturnNode::~ReturnNode() {
    delete this->exprNode;
    this->exprNode = 0;
}

Node *ReturnNode::getExprNode() {
    return this->exprNode;
}

void ReturnNode::dump(Writer &writer) const {
    WRITE_PTR(exprNode);
}

void ReturnNode::accept(NodeVisitor *visitor) {
    visitor->visitReturnNode(this);
}

EvalStatus ReturnNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->exprNode);
    if (!this->exprNode->getType()->isVoidType()) {
        ctx.setReturnObject();
    }
    return EVAL_RETURN;
}

// #######################
// ##     ThrowNode     ##
// #######################

ThrowNode::ThrowNode(unsigned int lineNum, Node *exprNode) :
        BlockEndNode(lineNum), exprNode(exprNode) {
}

ThrowNode::~ThrowNode() {
    delete this->exprNode;
    this->exprNode = 0;
}

Node *ThrowNode::getExprNode() {
    return this->exprNode;
}

void ThrowNode::dump(Writer &writer) const {
    WRITE_PTR(exprNode);
}

void ThrowNode::accept(NodeVisitor *visitor) {
    visitor->visitThrowNode(this);
}

EvalStatus ThrowNode::eval(RuntimeContext &ctx) {
    EVAL(ctx, this->exprNode);
    ctx.setThrowObject();
    return EVAL_THROW;
}

// #######################
// ##     CatchNode     ##
// #######################

CatchNode::CatchNode(unsigned int lineNum, std::string &&exceptionName,
                     BlockNode *blockNode) :
        CatchNode(lineNum, std::move(exceptionName),
                  newAnyTypeToken(lineNum), blockNode) {
}

CatchNode::CatchNode(unsigned int lineNum,
                     std::string &&exceptionName, TypeToken *type, BlockNode *blockNode) :
        Node(lineNum), exceptionName(std::move(exceptionName)),
        typeToken(type), exceptionType(0), varIndex(0), blockNode(blockNode) {
}

CatchNode::~CatchNode() {
    delete this->typeToken;
    this->typeToken = 0;

    delete this->blockNode;
    this->blockNode = 0;
}

const std::string &CatchNode::getExceptionName() {
    return this->exceptionName;
}

TypeToken *CatchNode::getTypeToken() {
    return this->typeToken;
}

void CatchNode::setExceptionType(DSType *type) {
    this->exceptionType = type;
}

DSType *CatchNode::getExceptionType() {
    return this->exceptionType;
}

void CatchNode::setAttribute(FieldHandle *handle) {
    this->varIndex = handle->getFieldIndex();
}

int CatchNode::getVarIndex() {
    return this->varIndex;
}

BlockNode *CatchNode::getBlockNode() {
    return this->blockNode;
}

void CatchNode::dump(Writer &writer) const {
    WRITE(exceptionName);
    WRITE_PTR(typeToken);
    WRITE_PTR(exceptionType);
    WRITE_PTR(blockNode);
}

void CatchNode::accept(NodeVisitor *visitor) {
    visitor->visitCatchNode(this);
}

EvalStatus CatchNode::eval(RuntimeContext &ctx) {
    ctx.setLocal(this->varIndex);
    EVAL(ctx, this->blockNode);
    return EVAL_SUCCESS;
}

// #####################
// ##     TryNode     ##
// #####################

TryNode::TryNode(unsigned int lineNum, BlockNode *blockNode) :
        Node(lineNum), blockNode(blockNode), catchNodes(), finallyNode() {
}

TryNode::~TryNode() {
    delete this->blockNode;
    this->blockNode = 0;

    for (CatchNode *n : this->catchNodes) {
        delete n;
    }
    this->catchNodes.clear();

    delete this->finallyNode;
    this->finallyNode = 0;
}

BlockNode *TryNode::getBlockNode() {
    return this->blockNode;
}

void TryNode::addCatchNode(CatchNode *catchNode) {
    this->catchNodes.push_back(catchNode);
}

const std::vector<CatchNode *> &TryNode::getCatchNodes() {
    return this->catchNodes;
}

void TryNode::addFinallyNode(BlockNode *finallyNode) {
    if (this->finallyNode != 0) {
        delete this->finallyNode;
    }
    this->finallyNode = finallyNode;
}

BlockNode *TryNode::getFinallyNode() {
    if (this->finallyNode == 0) {
        this->finallyNode = new BlockNode(0);
    }
    return this->finallyNode;
}

void TryNode::dump(Writer &writer) const {
    WRITE_PTR(blockNode);

    std::vector<Node *> catchNodes;
    for (CatchNode *node : this->catchNodes) {
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

    if (status != EVAL_THROW) {  // eval finally
        EVAL(ctx, this->finallyNode);
        return status;
    } else {   // eval catch
        DSType *thrownType = ctx.thrownObject->getType();
        for (CatchNode *catchNode : this->catchNodes) {
            if (catchNode->getExceptionType()->isAssignableFrom(thrownType)) {
                ctx.getThrownObject();
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
        varIndex(-1), initValueNode(initValueNode) {
    this->initValueNode->inRightHandleSide();
}

VarDeclNode::~VarDeclNode() {
    delete this->initValueNode;
    this->initValueNode = 0;
}

const std::string &VarDeclNode::getVarName() {
    return this->varName;
}

bool VarDeclNode::isReadOnly() {
    return this->readOnly;
}

void VarDeclNode::setAttribute(FieldHandle *handle) {
    this->global = handle->isGlobal();
    this->varIndex = handle->getFieldIndex();
}

bool VarDeclNode::isGlobal() {
    return this->global;
}

Node *VarDeclNode::getInitValueNode() {
    return this->initValueNode;
}

int VarDeclNode::getVarIndex() {
    return this->varIndex;
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
    if (this->global) {
        ctx.setGlobal(this->varIndex);
    } else {
        ctx.setLocal(this->varIndex);
    }
    return EVAL_SUCCESS;
}

// ########################
// ##     AssignNode     ##
// ########################

AssignNode::AssignNode(Node *leftNode, Node *rightNode, bool selfAssign) :
        Node(leftNode->getLineNum()),
        leftNode(leftNode), rightNode(rightNode), attributeSet(0) {
    if (selfAssign) {
        setFlag(this->attributeSet, SELF_ASSIGN);
    }
}

AssignNode::~AssignNode() {
    delete this->leftNode;
    this->leftNode = 0;

    delete this->rightNode;
    this->rightNode = 0;
}

Node *AssignNode::getLeftNode() {
    return this->leftNode;
}

void AssignNode::setRightNode(Node *rightNode) {
    this->rightNode = rightNode;
}

Node *AssignNode::getRightNode() {
    return this->rightNode;
}

void AssignNode::setAttribute(flag8_t flag) {
    setFlag(this->attributeSet, flag);
}

bool AssignNode::isSelfAssignment() {
    return hasFlag(this->attributeSet, SELF_ASSIGN);
}

bool AssignNode::isFieldAssign() {
    return hasFlag(this->attributeSet, FIELD_ASSIGN);
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
    int index = ((AssignableNode *) this->leftNode)->getIndex();
    if (this->isFieldAssign()) {
        if (this->isSelfAssignment()) {
            EVAL(ctx, this->leftNode);
        } else {
            AccessNode *accessNode = (AccessNode *) this->leftNode;
            EVAL(ctx, accessNode->getRecvNode());
        }
        EVAL(ctx, this->rightNode);
        std::shared_ptr<DSObject> value(ctx.pop());
        ctx.pop()->getFieldTable()[index] = value;
    } else {
        if (this->isSelfAssignment()) {
            EVAL(ctx, this->leftNode);
        }
        EVAL(ctx, this->rightNode);
        VarNode *varNode = (VarNode *) this->leftNode;

        if (varNode->isEnv()) {
            ctx.exportEnv(varNode->getVarName(), index, varNode->isGlobal());
        } else {
            if (varNode->isGlobal()) {
                ctx.setGlobal(index);
            } else {
                ctx.setLocal(index);
            }
        }
    }
    return EVAL_SUCCESS;
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

Node *ElementSelfAssignNode::getRecvNode() {
    return this->recvNode;
}

Node *ElementSelfAssignNode::getIndexNode() {
    return this->indexNode;
}

BinaryOpNode *ElementSelfAssignNode::getBinaryNode() {
    return this->binaryNode;
}

MethodCallNode *ElementSelfAssignNode::getGetterNode() {
    return this->getterNode;
}

MethodCallNode *ElementSelfAssignNode::getSetterNode() {
    return this->setterNode;
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

FunctionNode::FunctionNode(unsigned int lineNum, std::string &&funcName) :
        Node(lineNum), funcName(std::move(funcName)), paramNodes(), paramTypeTokens(), returnTypeToken(),
        returnType(0), blockNode(), maxVarNum(0), varIndex(0) {
}

FunctionNode::~FunctionNode() {
    for (VarNode *n : this->paramNodes) {
        delete n;
    }
    this->paramNodes.clear();

    for (TypeToken *t : this->paramTypeTokens) {
        delete t;
    }
    this->paramTypeTokens.clear();

    delete this->returnTypeToken;
    this->returnTypeToken = 0;

    delete this->blockNode;
    this->blockNode = 0;
}

const std::string &FunctionNode::getFuncName() {
    return this->funcName;
}

void FunctionNode::addParamNode(VarNode *node, TypeToken *paramType) {
    this->paramNodes.push_back(node);
    this->paramTypeTokens.push_back(paramType);
}

const std::vector<VarNode *> &FunctionNode::getParamNodes() {
    return this->paramNodes;
}

const std::vector<TypeToken *> &FunctionNode::getParamTypeTokens() {
    return this->paramTypeTokens;
}

void FunctionNode::setReturnTypeToken(TypeToken *typeToken) {
    this->returnTypeToken = typeToken;
}

TypeToken *FunctionNode::getReturnTypeToken() {
    if (this->returnTypeToken == 0) {
        this->returnTypeToken = newVoidTypeToken();
    }
    return this->returnTypeToken;
}

void FunctionNode::setReturnType(DSType *returnType) {
    this->returnType = returnType;
}

DSType *FunctionNode::getReturnType() {
    return this->returnType;
}

void FunctionNode::setBlockNode(BlockNode *blockNode) {
    this->blockNode = blockNode;
}

BlockNode *FunctionNode::getBlockNode() {
    return this->blockNode;
}

void FunctionNode::setMaxVarNum(unsigned int maxVarNum) {
    this->maxVarNum = maxVarNum;
}

unsigned int FunctionNode::getMaxVarNum() {
    return this->maxVarNum;
}

void FunctionNode::setVarIndex(int varIndex) {
    this->varIndex = varIndex;
}

int FunctionNode::getVarIndex() {
    return this->varIndex;
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
    for (VarNode *node : this->paramNodes) {
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
    ctx.setGlobal(this->varIndex, std::shared_ptr<DSObject>(new UserFuncObject(this)));
    return EVAL_REMOVE;
}

// ###########################
// ##     InterfaceNode     ##
// ###########################

InterfaceNode::InterfaceNode(unsigned int lineNum, std::string &&interfaceName) :
        Node(lineNum), interfaceName(interfaceName), methodDeclNodes(),
        fieldDeclNodes(), fieldTypeTokens() {
}

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

const std::string &InterfaceNode::getInterfaceName() {
    return this->interfaceName;
}

void InterfaceNode::addMethodDeclNode(FunctionNode *methodDeclNode) {
    this->methodDeclNodes.push_back(methodDeclNode);
}

const std::vector<FunctionNode *> &InterfaceNode::getMethodDeclNodes() {
    return this->methodDeclNodes;
}

void InterfaceNode::addFieldDecl(VarDeclNode *node, TypeToken *typeToken) {
    this->fieldDeclNodes.push_back(node);
    this->fieldTypeTokens.push_back(typeToken);
}

const std::vector<VarDeclNode *> &InterfaceNode::getFieldDeclNodes() {
    return this->fieldDeclNodes;
}

const std::vector<TypeToken *> &InterfaceNode::getFieldTypeTokens() {
    return this->fieldTypeTokens;
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
    return EVAL_SUCCESS;    // do nothing
}

// ###########################
// ##     BindVarNode     ##
// ###########################

BindVarNode::BindVarNode(const char *name, const std::shared_ptr<DSObject> &value) :
        Node(0), varName(std::string(name)), varIndex(-1), value(value) {
}

const std::string &BindVarNode::getVarName() {
    return this->varName;
}

void BindVarNode::setAttribute(FieldHandle *handle) {
    this->varIndex = handle->getFieldIndex();
}

int BindVarNode::getVarIndex() {
    return this->varIndex;
}

const std::shared_ptr<DSObject> &BindVarNode::getValue() {
    return this->value;
}

void BindVarNode::dump(Writer &writer) const {
    WRITE(varName);
    WRITE_PRIM(varIndex);
    // FIXME: value
}

void BindVarNode::accept(NodeVisitor *visitor) {
    visitor->visitBindVarNode(this);
}

EvalStatus BindVarNode::eval(RuntimeContext &ctx) {
    ctx.setGlobal(this->varIndex, this->value);
    return EVAL_SUCCESS;
}

// #######################
// ##     EmptyNode     ##
// #######################

EmptyNode::EmptyNode() :
        EmptyNode(0) {
}

EmptyNode::EmptyNode(unsigned int lineNum) :
        Node(lineNum) {
}

void EmptyNode::dump(Writer &writer) const {
    // do nothing
}

void EmptyNode::accept(NodeVisitor *visitor) {
    visitor->visitEmptyNode(this);
}

EvalStatus EmptyNode::eval(RuntimeContext &ctx) {
    return EVAL_SUCCESS; // do nothing
}

// #######################
// ##     DummyNode     ##
// #######################

DummyNode::DummyNode() :
        Node(0) {
}

void DummyNode::dump(Writer &writer) const {
    // do nothing
}

void DummyNode::accept(NodeVisitor *visitor) {
    visitor->visitDummyNode(this);
}

EvalStatus DummyNode::eval(RuntimeContext &ctx) {
    return EVAL_SUCCESS; // do nothing
}

// ######################
// ##     RootNode     ##
// ######################

RootNode::RootNode() :
        RootNode(0) {
    static const char empty[] = "";
    this->sourceName = empty;
}

RootNode::RootNode(const char *sourceName) :
        Node(0), sourceName(sourceName), nodeList(), maxVarNum(0), maxGVarNum(0) {
}

RootNode::~RootNode() {
    for (Node *n : this->nodeList) {
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

const std::list<Node *> &RootNode::getNodeList() {
    return this->nodeList;
}

void RootNode::setMaxVarNum(unsigned int maxVarNum) {
    this->maxVarNum = maxVarNum;
}

unsigned int RootNode::getMaxVarNum() const {
    return this->maxVarNum;
}

void RootNode::setMaxGVarNum(unsigned int maxGVarNum) {
    this->maxGVarNum = maxGVarNum;
}

unsigned int RootNode::getMaxGVarNum() const {
    return this->maxGVarNum;
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
    ctx.funcContextStack.clear();
    ctx.callStack.clear();

    ctx.funcContextStack.push_back(this);
    ctx.reserveGlobalVar(this->maxGVarNum);
    ctx.stackTopIndex = this->maxVarNum;

    for (auto iter = this->nodeList.begin(); iter != this->nodeList.end();) {
        Node *node = *iter;
        EvalStatus status = node->eval(ctx);
        if (status == EVAL_SUCCESS) {
            if (ctx.repl) {
                ctx.printStackTop(node->getType());
            } else if (!node->getType()->isVoidType()) {
                ctx.pop();
            }
        } else if (status == EVAL_THROW) {
            return EVAL_THROW;
        } else if (status == EVAL_REMOVE) {
            iter = this->nodeList.erase(iter);
            continue;
        } else {
            fatal("illegal EvalStatus: %d\n", status);
        }
        ++iter;
    }
    return EVAL_SUCCESS;
}

// for node creation

std::string resolveUnaryOpName(TokenKind op) {
    if (op == PLUS) {    // +
        return std::string(OP_PLUS);
    } else if (op == MINUS) {    // -
        return std::string(OP_MINUS);
    } else if (op == NOT) {  // not
        return std::string(OP_NOT);
    } else {
        fatal("unsupported unary op: %s\n", TO_NAME(op));
        return std::string("");
    }
}

std::string resolveBinaryOpName(TokenKind op) {
    switch (op) {
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
    switch (op) {
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
    if (op == ASSIGN) {
        // assign to element(actually call SET)
        MethodCallNode *indexNode = dynamic_cast<MethodCallNode *>(leftNode);
        if (indexNode != 0 && indexNode->hasAttribute(MethodCallNode::INDEX)) {
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
    if (indexNode != 0 && indexNode->hasAttribute(MethodCallNode::INDEX)) {
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

Node *createUnaryOpNode(TokenKind op, Node *recvNode) {
    return new MethodCallNode(recvNode, resolveUnaryOpName(op));
}

Node *createBinaryOpNode(Node *leftNode, TokenKind op, Node *rightNode) {
    switch (op) {
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

// #########################
// ##     NodeVisitor     ##
// #########################

NodeVisitor::NodeVisitor() {
}

NodeVisitor::~NodeVisitor() {
}

void NodeVisitor::visitIntValueNode(IntValueNode *node)                   { this->visitDefault(node); }
void NodeVisitor::visitLongValueNode(LongValueNode *node)                 { this->visitDefault(node); }
void NodeVisitor::visitFloatValueNode(FloatValueNode *node)               { this->visitDefault(node); }
void NodeVisitor::visitStringValueNode(StringValueNode *node)             { this->visitDefault(node); }
void NodeVisitor::visitObjectPathNode(ObjectPathNode *node)               { this->visitDefault(node); }
void NodeVisitor::visitStringExprNode(StringExprNode *node)               { this->visitDefault(node); }
void NodeVisitor::visitArrayNode(ArrayNode *node)                         { this->visitDefault(node); }
void NodeVisitor::visitMapNode(MapNode *node)                             { this->visitDefault(node); }
void NodeVisitor::visitTupleNode(TupleNode *node)                         { this->visitDefault(node); }
void NodeVisitor::visitVarNode(VarNode *node)                             { this->visitDefault(node); }
void NodeVisitor::visitAccessNode(AccessNode *node)                       { this->visitDefault(node); }
void NodeVisitor::visitCastNode(CastNode *node)                           { this->visitDefault(node); }
void NodeVisitor::visitInstanceOfNode(InstanceOfNode *node)               { this->visitDefault(node); }
void NodeVisitor::visitBinaryOpNode(BinaryOpNode *node)                   { this->visitDefault(node); }
void NodeVisitor::visitArgsNode(ArgsNode *node)                           { this->visitDefault(node); }
void NodeVisitor::visitApplyNode(ApplyNode *node)                         { this->visitDefault(node); }
void NodeVisitor::visitMethodCallNode(MethodCallNode *node)               { this->visitDefault(node); }
void NodeVisitor::visitNewNode(NewNode *node)                             { this->visitDefault(node); }
void NodeVisitor::visitGroupNode(GroupNode *node)                         { this->visitDefault(node); }
void NodeVisitor::visitCondOpNode(CondOpNode *node)                       { this->visitDefault(node); }
void NodeVisitor::visitCmdNode(CmdNode *node)                             { this->visitDefault(node); }
void NodeVisitor::visitCmdArgNode(CmdArgNode *node)                       { this->visitDefault(node); }
void NodeVisitor::visitPipedCmdNode(PipedCmdNode *node)                   { this->visitDefault(node); }
void NodeVisitor::visitCmdContextNode(CmdContextNode *node)               { this->visitDefault(node); }
void NodeVisitor::visitAssertNode(AssertNode *node)                       { this->visitDefault(node); }
void NodeVisitor::visitBlockNode(BlockNode *node)                         { this->visitDefault(node); }
void NodeVisitor::visitBreakNode(BreakNode *node)                         { this->visitDefault(node); }
void NodeVisitor::visitContinueNode(ContinueNode *node)                   { this->visitDefault(node); }
void NodeVisitor::visitExportEnvNode(ExportEnvNode *node)                 { this->visitDefault(node); }
void NodeVisitor::visitImportEnvNode(ImportEnvNode *node)                 { this->visitDefault(node); }
void NodeVisitor::visitTypeAliasNode(TypeAliasNode *node)                 { this->visitDefault(node); }
void NodeVisitor::visitForNode(ForNode *node)                             { this->visitDefault(node); }
void NodeVisitor::visitWhileNode(WhileNode *node)                         { this->visitDefault(node); }
void NodeVisitor::visitDoWhileNode(DoWhileNode *node)                     { this->visitDefault(node); }
void NodeVisitor::visitIfNode(IfNode *node)                               { this->visitDefault(node); }
void NodeVisitor::visitReturnNode(ReturnNode *node)                       { this->visitDefault(node); }
void NodeVisitor::visitThrowNode(ThrowNode *node)                         { this->visitDefault(node); }
void NodeVisitor::visitCatchNode(CatchNode *node)                         { this->visitDefault(node); }
void NodeVisitor::visitTryNode(TryNode *node)                             { this->visitDefault(node); }
void NodeVisitor::visitVarDeclNode(VarDeclNode *node)                     { this->visitDefault(node); }
void NodeVisitor::visitAssignNode(AssignNode *node)                       { this->visitDefault(node); }
void NodeVisitor::visitElementSelfAssignNode(ElementSelfAssignNode *node) { this->visitDefault(node); }
void NodeVisitor::visitFunctionNode(FunctionNode *node)                   { this->visitDefault(node); }
void NodeVisitor::visitInterfaceNode(InterfaceNode *node)                 { this->visitDefault(node); }
void NodeVisitor::visitBindVarNode(BindVarNode *node)                     { this->visitDefault(node); }
void NodeVisitor::visitEmptyNode(EmptyNode *node)                         { this->visitDefault(node); }
void NodeVisitor::visitDummyNode(DummyNode *node)                         { this->visitDefault(node); }
void NodeVisitor::visitRootNode(RootNode *node)                           { this->visitDefault(node); }

} // namespace ast
} // namespace ydsh