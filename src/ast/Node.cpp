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

#include <core/builtin_variable.h>
#include <core/magic_method.h>
#include <ast/Node.h>
#include <util/debug.h>
#include <ast/dump.h>

#include <assert.h>
#include <stdlib.h>
#include <utility>

// helper macro
#define EVAL(ctx, node) \
    do {\
        EvalStatus status = (node)->eval(ctx);\
        if(status != EVAL_SUCCESS) {\
            return status;\
        }\
    } while(0)


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

Node *Node::convertToStringNode() {
    return createApplyNode(this, std::string(OP_INTERP));
}

Node *Node::convertToCmdArg() {
    return createApplyNode(this, std::string(OP_TO_CMD_ARG));
}

// ##########################
// ##     IntValueNode     ##
// ##########################

IntValueNode::IntValueNode(unsigned int lineNum, int value) :
        Node(lineNum), tempValue(value), value() {
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

std::shared_ptr<DSObject> StringValueNode::getValue() {
    return this->value;
}

void StringValueNode::setType(DSType *type) {
    this->type = type;
    this->value.reset(
            new String_Object(this->type, std::move(this->tempValue)));
}

Node *StringValueNode::convertToStringNode() {
    return this;
}

Node *StringValueNode::convertToCmdArg() {
    return this;
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
    return EVAL_SUCCESS;
}

// ############################
// ##     StringExprNode     ##
// ############################

StringExprNode::StringExprNode(unsigned int lineNum) :
        Node(lineNum), nodes() {
}

StringExprNode::~StringExprNode() {
    for(Node *e : this->nodes) {
        delete e;
    }
    this->nodes.clear();
}

void StringExprNode::addExprNode(Node *node) {
    this->nodes.push_back(node->convertToStringNode());
}

const std::vector<Node*> &StringExprNode::getExprNodes() {
    return this->nodes;
}

Node *StringExprNode::convertToStringNode() {
    return this;
}

Node *StringExprNode::convertToCmdArg() {
    return this;
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
        ctx.push(std::make_shared<String_Object>(this->type, ""));
    } else if(size == 1) {
        this->nodes[0]->eval(ctx);
    } else {
        auto value = std::make_shared<String_Object>(this->type, "");
        for(Node *node : this->nodes) {
            EVAL(ctx, node);
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

const std::vector<Node*> &ArrayNode::getExprNodes() {
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
    for(Node *node : this->nodes) {
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

const std::vector<Node*> &MapNode::getKeyNodes() {
    return this->keyNodes;
}

void MapNode::setValueNode(unsigned int index, Node *valueNode) {
    this->valueNodes[index] = valueNode;
}

const std::vector<Node*> &MapNode::getValueNodes() {
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
    fatal("unimplemented eval\n");  //TODO
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
    for(Node *node : this->nodes) {
        delete node;
    }
    this->nodes.clear();
}

void TupleNode::addNode(Node *node) {
    this->nodes.push_back(node);
}

const std::vector<Node*> &TupleNode::getNodes() {
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
    auto value = std::make_shared<Tuple_Object>(this->type, size);
    for(unsigned int i = 0; i < size; i++) {
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

// #####################
// ##     VarNode     ##
// #####################

VarNode::VarNode(unsigned int lineNum, std::string &&varName) :
        AssignableNode(lineNum), varName(std::move(varName)), global(false) {
}

const std::string &VarNode::getVarName() {
    return this->varName;
}

void VarNode::setAttribute(FieldHandle *handle) {
    this->readOnly = handle->isReadOnly();
    this->global = handle->isGlobal();
    this->index = handle->getFieldIndex();
}

void VarNode::dump(Writer &writer) const {
    WRITE_PRIM(readOnly);
    WRITE_PRIM(index);
    WRITE(varName);
    WRITE_PRIM(global);
}

void VarNode::accept(NodeVisitor *visitor) {
    visitor->visitVarNode(this);
}

bool VarNode::isGlobal() {
    return this->global;
}

int VarNode::getVarIndex() {
    return this->index;
}

EvalStatus VarNode::eval(RuntimeContext &ctx) {
    if(this->global) {
        ctx.getGlobal(this->index);
    } else {
        ctx.getLocal(this->index);
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
    OP(DUP_RECV, out) \
    OP(DUP_RECV_AND_SWAP, out)

    std::string str;
    DECODE_ENUM(str, this->additionalOp, EACH_ENUM);
    writer.write(NAME(addtionalOp), str);
#undef EACH_ENUM
}

void AccessNode::accept(NodeVisitor *visitor) {
    visitor->visitAccessNode(this);
}

EvalStatus AccessNode::eval(RuntimeContext &ctx) {
    fatal("unimplemented eval\n");
    return EVAL_SUCCESS;    //TODO
}

// ######################
// ##     CastNode     ##
// ######################

CastNode::CastNode(Node *exprNode, TypeToken *type) :
        Node(exprNode->getLineNum()), exprNode(exprNode), targetTypeToken(type),
        opKind(NOP), fieldIndex(-1) {
}

CastNode::~CastNode() {
    delete this->exprNode;
    this->exprNode = 0;

    delete this->targetTypeToken;
    this->targetTypeToken = 0;
}

Node *CastNode::getExprNode() {
    return this->exprNode;
}

TypeToken *CastNode::getTargetTypeToken() {
    return this->targetTypeToken;
}

TypeToken *CastNode::removeTargetTypeToken() {
    TypeToken *t = this->targetTypeToken;
    this->targetTypeToken = 0;
    return t;
}

void CastNode::setOpKind(CastNode::CastOp opKind) {
    this->opKind = opKind;
}

CastNode::CastOp CastNode::getOpKind() {
    return this->opKind;
}

void CastNode::setFieldIndex(int index) {
    this->fieldIndex = index;
}

int CastNode::getFieldIndex() {
    return this->fieldIndex;
}

void CastNode::dump(Writer &writer) const {
    WRITE_PTR(exprNode);
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

    WRITE_PRIM(fieldIndex);
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
        ctx.push(std::make_shared<Float_Object>(this->type, (double) value));
        break;
    }
    case FLOAT_TO_INT: {
        double value = TYPE_AS(Float_Object, ctx.pop())->getValue();
        ctx.push(std::make_shared<Int_Object>(this->type, (int) value));
        break;
    }
    case TO_STRING: {
        fatal("unimplemented eval\n");
        break;
    }
    case CHECK_CAST: {
        ctx.checkCast(this->type);
        break;
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

TypeToken *InstanceOfNode::removeTargetTypeToken() {
    TypeToken *t = this->targetTypeToken;
    this->targetTypeToken = 0;
    return t;
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

    switch(this->opKind) {
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
        Node(0), argPairs(), paramIndexMap(0), paramSize(0) {
}

ArgsNode::~ArgsNode() {
    delete this->paramIndexMap;
    this->paramIndexMap = 0;
}

void ArgsNode::addArg(Node *argNode) {
    AssignNode *assignNode = dynamic_cast<AssignNode*>(argNode);
    if(assignNode != 0 && dynamic_cast<VarNode*>(assignNode->getLeftNode()) != 0) {
        std::pair<Node*, Node*> pair = AssignNode::split(assignNode);
        VarNode *leftNode = dynamic_cast<VarNode*>(pair.first);
        this->argPairs.push_back(
                std::make_pair(VarNode::extractVarNameAndDelete(leftNode), pair.second));
    } else {
        this->argPairs.push_back(std::make_pair("", argNode));
    }
}

void ArgsNode::setArg(unsigned int index, Node *argNode) {
    this->argPairs[index].second = argNode;
}

void ArgsNode::initIndexMap() {
    this->paramIndexMap = new unsigned int[this->argPairs.size()];
}

void ArgsNode::addParamIndex(unsigned int index, unsigned int value) {
    this->paramIndexMap[index] = value;
}

unsigned int *ArgsNode::getParamIndexMap() {
    return this->paramIndexMap;
}

void ArgsNode::setParamSize(unsigned int size) {
    this->paramSize = size;
}

unsigned int ArgsNode::getParamSize() {
    return this->paramSize;
}
const std::vector<std::pair<std::string, Node*>> &ArgsNode::getArgPairs() {
    return this->argPairs;
}

void ArgsNode::dump(Writer &writer) const {
    //FIXME: argPairs
}

void ArgsNode::accept(NodeVisitor *visitor) {
    visitor->visitArgsNode(this);
}

EvalStatus ArgsNode::eval(RuntimeContext &ctx) {    //TODO: named argument
    for(const std::pair<std::string, Node*> &pair : this->argPairs) {
        EVAL(ctx, pair.second);
    }
    return EVAL_SUCCESS;
}

// #######################
// ##     ApplyNode     ##
// #######################

ApplyNode::ApplyNode(Node *recvNode, ArgsNode *argsNode) :
        Node(recvNode->getLineNum()), recvNode(recvNode),
        argsNode(argsNode), attributeSet(0) {
}

ApplyNode::~ApplyNode() {
    delete this->recvNode;
    this->recvNode = 0;

    delete this->argsNode;
    this->argsNode = 0;
}

Node *ApplyNode::getRecvNode() {
    return this->recvNode;
}

ArgsNode *ApplyNode::getArgsNode() {
    return this->argsNode;
}

void ApplyNode::setAttribute(flag8_t attribute) {
    setFlag(this->attributeSet, attribute);
}

void ApplyNode::unsetAttribute(flag8_t attribute) {
    unsetFlag(this->attributeSet, attribute);
}

bool ApplyNode::hasAttribute(flag8_t attribute) {
    return hasFlag(this->attributeSet, attribute);
}

void ApplyNode::setFuncCall(bool asFuncCall) {
    if(asFuncCall) {
        this->setAttribute(FUNC_CALL);
    } else {
        this->unsetAttribute(FUNC_CALL);
    }
}

bool ApplyNode::isFuncCall() {
    return this->hasAttribute(FUNC_CALL);
}

void ApplyNode::dump(Writer &writer) const {
    WRITE_PTR(recvNode);
    WRITE_PTR(argsNode);

#define EACH_FLAG(OP, out, set) \
    OP(FUNC_CALL, out, set) \
    OP(INDEX, out, set)

    std::string str;
    DECODE_BITSET(str, this->attributeSet, EACH_FLAG);
    writer.write(NAME(attributeSet), str);
#undef EACH_FLAG
}

void ApplyNode::accept(NodeVisitor *visitor) {
    visitor->visitApplyNode(this);
}

/**
 * stack state in function apply    stack grow ===>
 *
 * +-----------+---------+------------------+   +--------+
 * | stack top | funcObj | param1(receiver) | ~ | paramN |
 * +-----------+---------+------------------+   +--------+
 *                       |    new offset    |   |        |
 */
EvalStatus ApplyNode::eval(RuntimeContext &ctx) {
    unsigned int curStackTopIndex = ctx.stackTopIndex;

    // push func object
    EVAL(ctx, this->recvNode);

    // push arguments.
    EVAL(ctx, this->argsNode);

    // call function
    ctx.saveAndSetOffset(curStackTopIndex + 2);
    bool status = TYPE_AS(FuncObject,
            ctx.localStack[curStackTopIndex + 1])->invoke(ctx);

    // restore stack state
    ctx.restoreOffset();
    for(unsigned int i = ctx.stackTopIndex; i > curStackTopIndex; i--) {
        ctx.pop();
    }

    if(status) {
        if(!this->type->isVoidType()) {
            ctx.getReturnObject(); // push return value
        }
        return EVAL_SUCCESS;
    } else {
        return EVAL_THROW;
    }
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

TypeToken *NewNode::removeTargetTypeToken() {
    TypeToken *t = this->targetTypeToken;
    this->targetTypeToken = 0;
    return t;
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
    fatal("unimplemented eval\n");  //TODO
    return EVAL_SUCCESS;
}

// ##########################
// ##     BinaryOpNode     ##
// ##########################

BinaryOpNode::BinaryOpNode(Node *leftNode, TokenKind op, Node *rightNode) :
        Node(leftNode->getLineNum()),
        leftNode(leftNode), rightNode(rightNode), op(op), applyNode(0) {
}

BinaryOpNode::~BinaryOpNode() {
    delete this->leftNode;
    this->leftNode = 0;

    delete this->rightNode;
    this->rightNode = 0;

    delete this->applyNode;
    this->applyNode = 0;
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

ApplyNode *BinaryOpNode::creatApplyNode() {
    this->applyNode = createApplyNode(this->leftNode, resolveBinaryOpName(this->op));
    this->applyNode->getArgsNode()->addArg(this->rightNode);

    // assign null to prevent double free.
    this->leftNode = 0;
    this->rightNode = 0;

    return this->applyNode;
}

ApplyNode *BinaryOpNode::getApplyNode() {
    return this->applyNode;
}

void BinaryOpNode::dump(Writer &writer) const {
    WRITE_PTR(leftNode);
    WRITE_PTR(rightNode);
    writer.write(NAME(op), TO_NAME(op));
    WRITE_PTR(applyNode);
}

void BinaryOpNode::accept(NodeVisitor *visitor) {
    visitor->visitBinaryOpNode(this);
}

EvalStatus BinaryOpNode::eval(RuntimeContext &ctx) {
    return this->applyNode->eval(ctx);
}

// ########################
// ##     CondOpNode     ##
// ########################

CondOpNode::CondOpNode(Node *leftNode, Node *rightNode, bool isAndOp) :
        Node(leftNode->getLineNum()), leftNode(leftNode), rightNode(rightNode), andOp(isAndOp) {
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

    if(this->andOp) {   // and
        if(TYPE_AS(Boolean_Object, ctx.peek())->getValue()) {
            ctx.pop();
            return this->rightNode->eval(ctx);
        } else {
            return EVAL_SUCCESS;
        }
    } else {    // or
        if(TYPE_AS(Boolean_Object, ctx.peek())->getValue()) {
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
    for(Node *e : this->segmentNodes) {
        delete e;
    }
    this->segmentNodes.clear();
}

void CmdArgNode::addSegmentNode(Node *node) {
    this->segmentNodes.push_back(node->convertToCmdArg());
}

const std::vector<Node*> &CmdArgNode::getSegmentNodes() {
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
    return EVAL_SUCCESS;
}

// #####################
// ##     CmdNode     ##
// #####################

CmdNode::CmdNode(unsigned int lineNum, std::string &&commandName) :
        Node(lineNum), commandName(std::move(commandName)), argNodes(), redirOptions() {
}

CmdNode::~CmdNode() {
    for(CmdArgNode *e : this->argNodes) {
        delete e;
    }
    this->argNodes.clear();

    for(const std::pair<int, Node*> &pair : this->redirOptions) {
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

const std::vector<CmdArgNode*> &CmdNode::getArgNodes() {
    return this->argNodes;
}

void CmdNode::addRedirOption(std::pair<int, Node*> &&optionPair) {
    this->redirOptions.push_back(std::move(optionPair));
}

const std::vector<std::pair<int, Node*>> &CmdNode::getRedirOptions() {
    return this->redirOptions;
}

void CmdNode::dump(Writer &writer) const {
    WRITE(commandName);

    std::vector<Node*> argNodes;
    for(CmdArgNode *node : this->argNodes) {
        argNodes.push_back(node);
    }
    WRITE(argNodes);
    //FIXME: redirOption
}

void CmdNode::accept(NodeVisitor *visitor) {
    visitor->visitCmdNode(this);
}

EvalStatus CmdNode::eval(RuntimeContext &ctx) { //FIXME: redirect
    std::shared_ptr<ProcContext> proc(new ProcContext(this->commandName));
    for(Node *node : this->argNodes) {
        EVAL(ctx, node);
        proc->addParam(ctx.pop());
    }
    ctx.push(proc);
    return EVAL_SUCCESS;
}

// #############################
// ##     SpecialCharNode     ##
// #############################

SpecialCharNode::SpecialCharNode(unsigned int lineNum, std::string &&name) :
        Node(lineNum), name(std::move(name)) {
}

SpecialCharNode::~SpecialCharNode() {
}

const std::string &SpecialCharNode::getName() {
    return this->name;
}

void SpecialCharNode::dump(Writer &writer) const {
    WRITE(name);
}

void SpecialCharNode::accept(NodeVisitor *visitor) {
    visitor->visitSpecialCharNode(this);
}

EvalStatus SpecialCharNode::eval(RuntimeContext &ctx) {
    fatal("unimplemented eval\n");  //TODO
    return EVAL_SUCCESS;
}

// ##########################
// ##     PipedCmdNode     ##
// ##########################

PipedCmdNode::PipedCmdNode(CmdNode *node) :
        Node(node->getLineNum()), cmdNodes() {
    this->cmdNodes.push_back(node);
}

PipedCmdNode::~PipedCmdNode() {
    for(CmdNode *p : this->cmdNodes) {
        delete p;
    }
    this->cmdNodes.clear();
}

void PipedCmdNode::addCmdNodes(CmdNode *node) {
    this->cmdNodes.push_back(node);
}

const std::vector<CmdNode*> &PipedCmdNode::getCmdNodes() {
    return this->cmdNodes;
}

void PipedCmdNode::dump(Writer &writer) const {
    std::vector<Node*> cmdNodes;
    for(CmdNode *node : this->cmdNodes) {
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

    for(unsigned int i = 0; i < size; i++) {
        EVAL(ctx, this->cmdNodes[i]);
        group.addProc(i, std::dynamic_pointer_cast<ProcContext>(ctx.pop()));
    }
    group.execProcs();

    return EVAL_SUCCESS;
}

// ###########################
// ##     CmdContextNode    ##
// ###########################

CmdContextNode::CmdContextNode(Node *exprNode) :
        Node(exprNode->getLineNum()),
        exprNode(exprNode), retKind(VOID), attributeSet(0) {
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
void CmdContextNode::setRetKind(CmdRetKind kind) {
    this->retKind = kind;
}

CmdContextNode::CmdRetKind CmdContextNode::getRetKind() {
    return this->retKind;
}

Node *CmdContextNode::convertToStringNode() {
    this->setRetKind(STR);
    return this;
}

Node *CmdContextNode::convertToCmdArg() {
    this->setRetKind(ARRAY);
    return this;
}

void CmdContextNode::dump(Writer &writer) const {
    WRITE_PTR(exprNode);

#define EACH_ENUM(OP, out) \
    OP(VOID, out) \
    OP(BOOL, out) \
    OP(STR, out) \
    OP(ARRAY, out) \
    OP(TASK, out)

    std::string str;
    DECODE_ENUM(str, this->retKind, EACH_ENUM);
    writer.write(NAME(retKind), str);

#undef EACH_ENUM

#define EACH_FLAG(OP, out, set) \
    OP(BACKGROUND, out, set) \
    OP(FORK, out, set)

    std::string value;
    DECODE_BITSET(value, this->attributeSet, EACH_FLAG);
    writer.write(NAME(attributeSet), value);

#undef EACH_FLAG
}

void CmdContextNode::accept(NodeVisitor *visitor) {
    visitor->visitCmdContextNode(this);
}

EvalStatus CmdContextNode::eval(RuntimeContext &ctx) {
    return this->exprNode->eval(ctx);   //FIXME:
}

// ########################
// ##     AssertNode     ##
// ########################

AssertNode::AssertNode(unsigned int lineNum, Node *exprNode) :
        Node(lineNum), exprNode(exprNode) {
}

AssertNode::~AssertNode() {
    delete this->exprNode;
    this->exprNode = 0;
}

Node *AssertNode::getExprNode() {
    return this->exprNode;
}

void AssertNode::dump(Writer &writer) const {
    WRITE_PTR(exprNode);
}

void AssertNode::accept(NodeVisitor *visitor) {
    visitor->visitAssertNode(this);
}

EvalStatus AssertNode::eval(RuntimeContext &ctx) {
    if(ctx.assertion) {
        EVAL(ctx, this->exprNode);
        ctx.checkAssertion();
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

const std::list<Node*> &BlockNode::getNodeList() {
    return this->nodeList;
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
            ctx.pop();
        }
        if(status != EVAL_SUCCESS) {
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
        Node(lineNum), envName(std::move(envName)), exprNode(exprNode) {
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

void ExportEnvNode::dump(Writer &writer) const {
    WRITE(envName);
    WRITE_PTR(exprNode);
}

void ExportEnvNode::accept(NodeVisitor *visitor) {
    visitor->visitExportEnvNode(this);
}

EvalStatus ExportEnvNode::eval(RuntimeContext &ctx) {
    fatal("unimplemented eval\n");  //TODO:
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
    fatal("unimplemented eval\n");  //TODO:
    return EVAL_SUCCESS;
}

// #####################
// ##     ForNode     ##
// #####################

ForNode::ForNode(unsigned int lineNum, Node *initNode, Node *condNode, Node *iterNode, BlockNode *blockNode) :
        Node(lineNum), initNode(initNode != 0 ? initNode : new EmptyNode()),
        condNode(condNode != 0 ? condNode : new VarNode(lineNum, std::string(TRUE))),
        iterNode(iterNode != 0 ? iterNode : new EmptyNode()), blockNode(blockNode) {
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
    if(TYPE_AS(Boolean_Object, ctx.pop())->getValue()) {
        EvalStatus status = this->blockNode->eval(ctx);
        switch(status) {
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
    if(TYPE_AS(Boolean_Object, ctx.pop())->getValue()) {
        EvalStatus status = this->blockNode->eval(ctx);
        switch(status) {
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
    switch(status) {
    case EVAL_BREAK:
        goto BREAK;
    case EVAL_SUCCESS:
    case EVAL_CONTINUE:
        break;
    default:
        return status;
    }

    EVAL(ctx, this->condNode);
    if(TYPE_AS(Boolean_Object, ctx.pop())->getValue()) {
        goto CONTINUE;
    }

BREAK:
    return EVAL_SUCCESS;
}

// ####################
// ##     IfNode     ##
// ####################

IfNode::IfNode(unsigned int lineNum, Node *condNode, BlockNode *thenNode) :
        Node(lineNum), condNode(condNode), thenNode(thenNode),
        elifCondNodes(), elifThenNodes(), elseNode(0) {
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

Node *IfNode::getCondNode() {
    return this->condNode;
}

BlockNode *IfNode::getThenNode() {
    return this->thenNode;
}

void IfNode::addElifNode(Node *condNode, BlockNode *thenNode) {
    this->elifCondNodes.push_back(condNode);
    this->elifThenNodes.push_back(thenNode);
}

const std::vector<Node*> &IfNode::getElifCondNodes() {
    return this->elifCondNodes;
}

const std::vector<BlockNode*> &IfNode::getElifThenNodes() {
    return this->elifThenNodes;
}

void IfNode::addElseNode(BlockNode *elseNode) {
    this->elseNode = elseNode;
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

    std::vector<Node*> elifThenNodes;
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
    if(!this->exprNode->getType()->isVoidType()) {
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
    fatal("unimplemented eval\n");  //TODO:
    return EVAL_SUCCESS;
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
        typeToken(type), exceptionType(0), blockNode(blockNode) {
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

TypeToken *CatchNode::removeTypeToken() {
    TypeToken *t = this->typeToken;
    this->typeToken = 0;
    return t;
}

void CatchNode::setExceptionType(DSType *type) {
    this->exceptionType = type;
}

DSType *CatchNode::getExceptionType() {
    return this->exceptionType;
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
    fatal("unimplemented eval\n");  //TODO:
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

    for(CatchNode *n : this->catchNodes) {
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

const std::vector<CatchNode*> &TryNode::getCatchNodes() {
    return this->catchNodes;
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

    std::vector<Node*> catchNodes;
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
    fatal("unimplemented eval\n");  //TODO:
    return EVAL_SUCCESS;
}

// #########################
// ##     VarDeclNode     ##
// #########################

VarDeclNode::VarDeclNode(unsigned int lineNum, std::string &&varName, Node *initValueNode, bool readOnly) :
        Node(lineNum), varName(std::move(varName)), readOnly(readOnly), global(false),
        varIndex(-1), initValueNode(initValueNode) {
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
    if(this->global) {
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
        Node(leftNode->getLineNum()), leftNode(leftNode), rightNode(rightNode), selfAssign(selfAssign) {
}

AssignNode::~AssignNode() {
    delete this->leftNode;
    this->leftNode = 0;

    delete this->rightNode;
    this->rightNode  = 0;
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

bool AssignNode::isSelfAssignment() {
    return this->selfAssign;
}

void AssignNode::dump(Writer &writer) const {
    WRITE_PTR(leftNode);
    WRITE_PTR(rightNode);
    WRITE_PRIM(selfAssign);
}

void AssignNode::accept(NodeVisitor *visitor) {
    visitor->visitAssignNode(this);
}

EvalStatus AssignNode::eval(RuntimeContext &ctx) {
    fatal("unimplemented eval\n");  //TODO:
    return EVAL_SUCCESS;
}

std::pair<Node*, Node*> AssignNode::split(AssignNode *node) {
    Node *leftNode = node->leftNode;
    node->leftNode = 0;

    Node *rightNode = node->rightNode;
    node->rightNode = 0;

    delete node;
    return std::make_pair(leftNode, rightNode);
}

// ##########################
// ##     FunctionNode     ##
// ##########################

FunctionNode::FunctionNode(unsigned int lineNum, std::string &&funcName) :
        Node(lineNum), funcName(std::move(funcName)), paramNodes(), paramTypeTokens(), returnTypeToken(),
        returnType(0), blockNode(), maxVarNum(0), varIndex(0) {
}

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

const std::string &FunctionNode::getFuncName() {
    return this->funcName;
}

void FunctionNode::addParamNode(VarNode *node, TypeToken *paramType) {
    this->paramNodes.push_back(node);
    this->paramTypeTokens.push_back(paramType);
}

const std::vector<VarNode*> &FunctionNode::getParamNodes() {
    return this->paramNodes;
}

const std::vector<TypeToken*> &FunctionNode::getParamTypeTokens() {
    return this->paramTypeTokens;
}

TypeToken *FunctionNode::removeParamTypeToken(unsigned int index) {
    TypeToken *t = this->paramTypeTokens[index];
    this->paramTypeTokens[index] = 0;
    return t;
}

void FunctionNode::setReturnTypeToken(TypeToken *typeToken) {
    this->returnTypeToken = typeToken;
}

TypeToken *FunctionNode::getReturnTypeToken() {
    if(this->returnTypeToken == 0) {
        this->returnTypeToken = newVoidTypeToken();
    }
    return this->returnTypeToken;
}

TypeToken *FunctionNode::removeReturnTypeToken() {
    TypeToken *t = this->getReturnTypeToken();
    this->returnTypeToken = 0;
    return t;
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

void FunctionNode::dump(Writer &writer) const {
    WRITE(funcName);

    std::vector<Node*> paramNodes;
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
}

void FunctionNode::accept(NodeVisitor *visitor) {
    visitor->visitFunctionNode(this);
}

EvalStatus FunctionNode::eval(RuntimeContext &ctx) {
    ctx.setGlobal(this->varIndex, std::shared_ptr<DSObject>(new UserFuncObject(this)));
    return EVAL_REMOVE;
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

DummyNode::DummyNode():
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
        Node(0), nodeList(), maxVarNum(0), maxGVarNum(0) {
}

RootNode::~RootNode() {
    for(Node *n : this->nodeList) {
        delete n;
    }
    this->nodeList.clear();
}

void RootNode::addNode(Node *node) {
    this->nodeList.push_back(node);
}

const std::list<Node*> &RootNode::getNodeList() {
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
    WRITE_PRIM(maxVarNum);
    WRITE_PRIM(maxGVarNum);
}

void RootNode::accept(NodeVisitor *visitor) {
    visitor->visitRootNode(this);
}

EvalStatus RootNode::eval(RuntimeContext &ctx) {
    ctx.reserveGlobalVar(this->maxGVarNum);
    ctx.localVarOffset = this->maxVarNum;

    for(auto iter = this->nodeList.begin(); iter != this->nodeList.end();) {
        Node *node = *iter;
        EvalStatus status = node->eval(ctx);
        if(status == EVAL_SUCCESS) {
            if(ctx.repl) {
                ctx.printStackTop(node->getType());
            } else if(!node->getType()->isVoidType()){
                ctx.pop();
            }
        } else if(status == EVAL_THROW) {
            fatal("unimplemted EvalStatus: %d\n", status);  //FIXME:
        } else if(status == EVAL_REMOVE) {
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

ApplyNode *createApplyNode(Node *recvNode, std::string &&methodName) {
    AccessNode *a = new AccessNode(recvNode, std::move(methodName));
    return new ApplyNode(a, new ArgsNode());
}

ForNode *createForInNode(unsigned int lineNum, VarNode *varNode, Node *exprNode, BlockNode *blockNode) {
    // create for-init
    ApplyNode *apply_reset = createApplyNode(exprNode, std::string(OP_RESET));
    std::string reset_var_name(std::to_string(rand()));
    VarDeclNode *reset_varDecl = new VarDeclNode(lineNum, std::string(reset_var_name), apply_reset, true);

    // create for-cond
    VarNode *reset_var = new VarNode(lineNum, std::string(reset_var_name));
    ApplyNode *apply_hasNext = createApplyNode(reset_var, std::string(OP_HAS_NEXT));

    // create forIn-init
    reset_var = new VarNode(lineNum, std::string(reset_var_name));
    ApplyNode *apply_next = createApplyNode(reset_var, std::string(OP_NEXT));
    VarDeclNode *init_var = new VarDeclNode(lineNum,
            VarNode::extractVarNameAndDelete(varNode), apply_next, false);

    // insert init to block
    blockNode->insertNodeToFirst(init_var);

    return new ForNode(lineNum, reset_varDecl, apply_hasNext, 0, blockNode);
}

Node *createSuffixNode(Node *leftNode, TokenKind op) {
    return createAssignNode(leftNode, op, new IntValueNode(leftNode->getLineNum(), 1));
}

Node *createAssignNode(Node *leftNode, TokenKind op, Node *rightNode) {
    /*
     * basic assignment
     */
    if(op == ASSIGN) {
        // assign to element(actually call SET)
        ApplyNode *indexNode = dynamic_cast<ApplyNode*>(leftNode);
        if(indexNode != 0 && indexNode->hasAttribute(ApplyNode::INDEX)) {
            AccessNode *accessNode = dynamic_cast<AccessNode*>(indexNode->getRecvNode());
            accessNode->setFieldName(std::string(OP_SET));
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
    ApplyNode *indexNode = dynamic_cast<ApplyNode*>(leftNode);
    if(indexNode != 0 && indexNode->hasAttribute(ApplyNode::INDEX)) {
        fatal("unimplemented assignment to element");   //FIXME:
        return 0;
    } else {
        // assign to variable or field
        BinaryOpNode *opNode = new BinaryOpNode(new DummyNode(), resolveAssignOp(op), rightNode);
        return new AssignNode(leftNode, opNode, true);
    }
}

Node *createIndexNode(Node *recvNode, Node *indexNode) {
    ApplyNode *applyNode = createApplyNode(recvNode, std::string(OP_GET));
    applyNode->setAttribute(ApplyNode::INDEX);
    applyNode->getArgsNode()->addArg(indexNode);
    return applyNode;
}

Node *createUnaryOpNode(TokenKind op, Node *recvNode) {
    return createApplyNode(recvNode, resolveUnaryOpName(op));
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

// #########################
// ##     NodeVisitor     ##
// #########################

NodeVisitor::NodeVisitor() {
}

NodeVisitor::~NodeVisitor() {
}
