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

#include <assert.h>
#include <stdlib.h>
#include <utility>

// ##################
// ##     Node     ##
// ##################

Node::Node(int lineNum) :
        lineNum(lineNum), type() {
}

Node::~Node() {
}

int Node::getLineNum() {
    return this->lineNum;
}

void Node::setType(DSType *type) {
    this->type = type;
}

DSType *Node::getType() {
    return this->type;
}


// ##########################
// ##     IntValueNode     ##
// ##########################

IntValueNode::IntValueNode(int lineNum, long value) :
        Node(lineNum), value(value) {
}

long IntValueNode::getValue() {
    return this->value;
}

int IntValueNode::accept(NodeVisitor *visitor) {
    return visitor->visitIntValueNode(this);
}

// ############################
// ##     FloatValueNode     ##
// ############################

FloatValueNode::FloatValueNode(int lineNum, double value) :
        Node(lineNum), value(value) {
}

double FloatValueNode::getValue() {
    return this->value;
}

int FloatValueNode::accept(NodeVisitor *visitor) {
    return visitor->visitFloatValueNode(this);
}

// ##############################
// ##     BooleanValueNode     ##
// ##############################

BooleanValueNode::BooleanValueNode(int lineNum, bool value) :
        Node(lineNum), value(value) {
}

bool BooleanValueNode::getValue() {
    return this->value;
}

int BooleanValueNode::accept(NodeVisitor *visitor) {
    return visitor->visitBooleanValueNode(this);
}

// ############################
// ##     StringValueNode    ##
// ############################

StringValueNode::StringValueNode(std::string &&value) :
        Node(0), value(std::move(value)) {
}

StringValueNode::StringValueNode(int lineNum, char *value, bool isSingleQuoteStr) :	//TODO:
        Node(lineNum) {
    // parser original value.

    /*			StringBuilder sBuilder = new StringBuilder();
     String text = token.getText();
     int startIndex = isSingleQuoteStr ? 1 : 0;
     int endIndex = isSingleQuoteStr ? text.length() - 1 : text.length();
     for(int i = startIndex; i < endIndex; i++) {
     char ch = text.charAt(i);
     if(ch == '\\') {
     char nextCh = text.charAt(++i);
     switch(nextCh) {
     case 't' : ch = #include <ast/node_utils.h>'\t'; break;
     case 'b' : ch = '\b'; break;
     case 'n' : ch = '\n'; break;
     case 'r' : ch = '\r'; break;
     case 'f' : ch = '\f'; break;
     case '\'': ch = '\''; break;
     case '"' : ch = '"' ; break;
     case '\\': ch = '\\'; break;
     case '`' : ch = '`' ; break;
     case '$' : ch = '$' ; break;
     }
     }
     sBuilder.append(ch);
     }
     return sBuilder.toString();
     */
}

//StringValueNode::StringValueNode(int lineNum, char *value):	//FIXME:
//		StringValueNode(lineNum, value, false) {
//}

const std::string &StringValueNode::getValue() {
    return this->value;
}

int StringValueNode::accept(NodeVisitor *visitor) {
    return visitor->visitStringValueNode(this);
}

// ############################
// ##     StringExprNode     ##
// ############################

StringExprNode::StringExprNode(int lineNum) :
        Node(lineNum), nodes() {
}

StringExprNode::~StringExprNode() {
    for(Node *e : this->nodes) {
        delete e;
    }
    this->nodes.clear();
}

void StringExprNode::addExprNode(Node *node) {	//TODO:
    this->nodes.push_back(node);
}

const std::vector<Node*> &StringExprNode::getExprNodes() {
    return this->nodes;
}

int StringExprNode::accept(NodeVisitor *visitor) {
    return visitor->visitStringExprNode(this);
}

// #######################
// ##     ArrayNode     ##
// #######################

ArrayNode::ArrayNode(int lineNum) :
        Node(lineNum), nodes() {
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

int ArrayNode::accept(NodeVisitor *visitor) {
    return visitor->visitArrayNode(this);
}

// #####################
// ##     MapNode     ##
// #####################

MapNode::MapNode(int lineNum) :
        Node(lineNum), keyNodes(), valueNodes() {
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

int MapNode::accept(NodeVisitor *visitor) {
    return visitor->visitMapNode(this);
}

// #######################
// ##     TupleNode     ##
// #######################

TupleNode::TupleNode(int lineNum, Node *node) :
        Node(lineNum), nodes(2) {
    this->nodes.push_back(node);
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

int TupleNode::accept(NodeVisitor *visitor) {
    return visitor->visitTupleNode(this);
}

// ############################
// ##     AssignableNode     ##
// ############################

AssignableNode::AssignableNode(int lineNum) :
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

VarNode::VarNode(int lineNum, std::string &&varName) :
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

int VarNode::accept(NodeVisitor *visitor) {
    return visitor->visitVarNode(this);
}

bool VarNode::isGlobal() {
    return this->global;
}

int VarNode::getVarIndex() {
    return this->index;
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

int AccessNode::accept(NodeVisitor *visitor) {
    return visitor->visitAccessNode(this);
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

int CastNode::accept(NodeVisitor *visitor) {
    return visitor->visitCastNode(this);
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

int InstanceOfNode::accept(NodeVisitor *visitor) {
    return visitor->visitInstanceOfNode(this);
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

int BinaryOpNode::accept(NodeVisitor *visitor) {
    return visitor->visitBinaryOpNode(this);
}

// ######################
// ##     ArgsNode     ##
// ######################

ArgsNode::ArgsNode(int lineNum) :
        Node(lineNum), argPairs(), paramIndexMap(0), paramSize(0) {
}
ArgsNode::ArgsNode(std::string &&paramName, Node* argNode) :
        ArgsNode(argNode->getLineNum()) {
    this->argPairs.push_back(
            std::pair<std::string, Node*>(std::move(paramName), argNode));
}

ArgsNode::ArgsNode(Node *argNode) :
        ArgsNode(std::string(""), argNode) {
}

ArgsNode::~ArgsNode() {
    delete this->paramIndexMap;
    this->paramIndexMap = 0;
}

void ArgsNode::addArgPair(std::string &&paramName, Node *argNode) {
    this->argPairs.push_back(
            std::pair<std::string, Node*>(std::move(paramName), argNode));
}

void ArgsNode::addArg(Node *argNode) {
    this->addArgPair(std::string(""), argNode);
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

int ArgsNode::accept(NodeVisitor *visitor) {
    return visitor->visitArgsNode(this);
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

void ApplyNode::setAttribute(unsigned char attribute) {
    this->attributeSet |= attribute;
}

void ApplyNode::unsetAttribute(unsigned char attribute) {
    this->attributeSet &= ~attribute;
}

bool ApplyNode::hasAttribute(unsigned char attribute) {
    return (this->attributeSet & attribute) == attribute;
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

int ApplyNode::accept(NodeVisitor *visitor) {
    return visitor->visitApplyNode(this);
}

// #####################
// ##     NewNode     ##
// #####################

NewNode::NewNode(int lineNum, TypeToken *targetTypeToken, ArgsNode *argsNode) :
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

int NewNode::accept(NodeVisitor *visitor) {
    return visitor->visitNewNode(this);
}

// ########################
// ##     CondOpNode     ##
// ########################

CondOpNode::CondOpNode(int lineNum, Node *leftNode, Node *rightNode, bool isAndOp) :
        Node(lineNum), leftNode(leftNode), rightNode(rightNode), andOp(isAndOp) {
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

int CondOpNode::accept(NodeVisitor *visitor) {
    return visitor->visitCondOpNode(this);
}

// #########################
// ##     ProcessNode     ##
// #########################

ProcessNode::ProcessNode(int lineNum, std::string &&commandName) :
        Node(lineNum), commandName(std::move(commandName)), argNodes(), redirOptions() {
}

ProcessNode::~ProcessNode() {
    for(ProcArgNode *e : this->argNodes) {
        delete e;
    }
    this->argNodes.clear();

    for(const std::pair<int, Node*> &pair : this->redirOptions) {
        delete pair.second;
    }
    this->redirOptions.clear();
}

const std::string &ProcessNode::getCommandName() {
    return this->commandName;
}

void ProcessNode::addArgNode(ProcArgNode *node) {
    this->argNodes.push_back(node);
}

const std::vector<ProcArgNode*> &ProcessNode::getArgNodes() {
    return this->argNodes;
}

void ProcessNode::addRedirOption(std::pair<int, Node*> &&optionPair) {
    this->redirOptions.push_back(std::move(optionPair));
}

const std::vector<std::pair<int, Node*>> &ProcessNode::getRedirOptions() {
    return this->redirOptions;
}

int ProcessNode::accept(NodeVisitor *visitor) {
    return visitor->visitProcessNode(this);
}

// #########################
// ##     ProcArgNode     ##
// #########################

ProcArgNode::ProcArgNode(int lineNum) :
        Node(lineNum), segmentNodes() {
}

ProcArgNode::~ProcArgNode() {
    for(Node *e : this->segmentNodes) {
        delete e;
    }
    this->segmentNodes.clear();
}

void ProcArgNode::addSegmentNode(Node *node) {
    ProcArgNode *argNode = dynamic_cast<ProcArgNode*>(node);
    if(argNode != 0) {
        int size = argNode->getSegmentNodes().size();
        for(int i = 0; i < size; i++) {
            Node *s = argNode->segmentNodes[i];
            argNode->segmentNodes[i] = 0;
            this->segmentNodes.push_back(s);
        }
        delete argNode;
        return;
    }
    this->segmentNodes.push_back(node);
}

const std::vector<Node*> &ProcArgNode::getSegmentNodes() {
    return this->segmentNodes;
}

int ProcArgNode::accept(NodeVisitor *visitor) {
    return visitor->visitProcArgNode(this);
}

// #############################
// ##     SpecialCharNode     ##
// #############################

SpecialCharNode::SpecialCharNode(int lineNum) :
        Node(lineNum) {
}

SpecialCharNode::~SpecialCharNode() {
}

int SpecialCharNode::accept(NodeVisitor *visitor) {
    return visitor->visitSpecialCharNode(this);
}

// ######################
// ##     TaskNode     ##
// ######################

TaskNode::TaskNode() :
        Node(0), procNodes(), background(false) {
}

TaskNode::~TaskNode() {
    for(ProcessNode *p : this->procNodes) {
        delete p;
    }
    this->procNodes.clear();
}

void TaskNode::addProcNodes(ProcessNode *node) {
    this->procNodes.push_back(node);
}

const std::vector<ProcessNode*> &TaskNode::getProcNodes() {
    return this->procNodes;
}

bool TaskNode::isBackground() {
    return this->background;
}

int TaskNode::accept(NodeVisitor *visitor) {
    return visitor->visitTaskNode(this);
}

// ###########################
// ##     InnerTaskNode     ##
// ###########################

InnerTaskNode::InnerTaskNode(Node *exprNode) :
        Node(0), exprNode(exprNode) {
}

InnerTaskNode::~InnerTaskNode() {
    delete this->exprNode;
    this->exprNode = 0;
}

Node *InnerTaskNode::getExprNode() {
    return this->exprNode;
}

int InnerTaskNode::accept(NodeVisitor *visitor) {
    return visitor->visitInnerTaskNode(this);
}

// ########################
// ##     AssertNode     ##
// ########################

AssertNode::AssertNode(int lineNum, Node *exprNode) :
        Node(lineNum), exprNode(exprNode) {
}

AssertNode::~AssertNode() {
    delete this->exprNode;
    this->exprNode = 0;
}

Node *AssertNode::getExprNode() {
    return this->exprNode;
}

int AssertNode::accept(NodeVisitor *visitor) {
    return visitor->visitAssertNode(this);
}

// #######################
// ##     BlockNode     ##
// #######################

BlockNode::BlockNode() :
        Node(0), nodeList() {
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

int BlockNode::accept(NodeVisitor *visitor) {
    return visitor->visitBlockNode(this);
}

// ######################
// ##     BlockEnd     ##
// ######################

BlockEndNode::BlockEndNode(int lineNum) :
        Node(lineNum) {
}

// #######################
// ##     BreakNode     ##
// #######################

BreakNode::BreakNode(int lineNum) :
        BlockEndNode(lineNum) {
}

int BreakNode::accept(NodeVisitor *visitor) {
    return visitor->visitBreakNode(this);
}

// ##########################
// ##     ContinueNode     ##
// ##########################

ContinueNode::ContinueNode(int lineNum) :
        BlockEndNode(lineNum) {
}

int ContinueNode::accept(NodeVisitor *visitor) {
    return visitor->visitContinueNode(this);
}

// ###########################
// ##     ExportEnvNode     ##
// ###########################

ExportEnvNode::ExportEnvNode(int lineNum, std::string &&envName, Node *exprNode) :
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

int ExportEnvNode::accept(NodeVisitor *visitor) {
    return visitor->visitExportEnvNode(this);
}

// ###########################
// ##     ImportEnvNode     ##
// ###########################

ImportEnvNode::ImportEnvNode(int lineNum, std::string &&envName) :
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

int ImportEnvNode::accept(NodeVisitor *visitor) {
    return visitor->visitImportEnvNode(this);
}

// ######################
// ##     LoopNode     ##
// ######################

LoopNode::LoopNode(int lineNum) :
        Node(lineNum) {
}

LoopNode::~LoopNode() {
}

// #####################
// ##     ForNode     ##
// #####################

ForNode::ForNode(int lineNum, Node *initNode, Node *condNode, Node *iterNode, BlockNode *blockNode) :
        LoopNode(lineNum), initNode(initNode != 0 ? initNode : new EmptyNode()),
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

int ForNode::accept(NodeVisitor *visitor) {
    return visitor->visitForNode(this);
}

// #######################
// ##     WhileNode     ##
// #######################

WhileNode::WhileNode(int lineNum, Node *condNode, BlockNode *blockNode) :
        LoopNode(lineNum), condNode(condNode), blockNode(blockNode) {
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

int WhileNode::accept(NodeVisitor *visitor) {
    return visitor->visitWhileNode(this);
}

// #########################
// ##     DoWhileNode     ##
// #########################

DoWhileNode::DoWhileNode(int lineNum, BlockNode *blockNode, Node *condNode) :
        LoopNode(lineNum), blockNode(blockNode), condNode(condNode) {
}

DoWhileNode::~DoWhileNode() {
}

BlockNode *DoWhileNode::getBlockNode() {
    return this->blockNode;
}

Node *DoWhileNode::getCondNode() {
    return this->condNode;
}

int DoWhileNode::accept(NodeVisitor *visitor) {
    return visitor->visitDoWhileNode(this);
}

// ####################
// ##     IfNode     ##
// ####################

IfNode::IfNode(int lineNum, Node *condNode, BlockNode *thenNode, BlockNode *elseNode) :
        Node(lineNum), condNode(condNode), thenNode(thenNode),
        elseNode(elseNode != 0 ? elseNode : new BlockNode()) {
}

IfNode::~IfNode() {
    delete this->condNode;
    this->condNode = 0;

    delete this->thenNode;
    this->thenNode = 0;

    delete this->elseNode;
    this->elseNode = 0;
}

Node *IfNode::getCondNode() {
    return this->condNode;
}

BlockNode *IfNode::getThenNode() {
    return this->thenNode;
}

BlockNode *IfNode::getElseNode() {
    return this->elseNode;
}

int IfNode::accept(NodeVisitor *visitor) {
    return visitor->visitIfNode(this);
}

// ########################
// ##     ReturnNode     ##
// ########################

ReturnNode::ReturnNode(int lineNum, Node *exprNode) :
        BlockEndNode(lineNum), exprNode(exprNode) {
}

ReturnNode::ReturnNode(int lineNum) :
        BlockEndNode(lineNum), exprNode() {
}

ReturnNode::~ReturnNode() {
    delete this->exprNode;
    this->exprNode = 0;
}

Node *ReturnNode::getExprNode() {
    return this->exprNode;
}

int ReturnNode::accept(NodeVisitor *visitor) {
    return visitor->visitReturnNode(this);
}

// #######################
// ##     ThrowNode     ##
// #######################

ThrowNode::ThrowNode(int lineNum, Node *exprNode) :
        BlockEndNode(lineNum), exprNode(exprNode) {
}

ThrowNode::~ThrowNode() {
    delete this->exprNode;
    this->exprNode = 0;
}

Node *ThrowNode::getExprNode() {
    return this->exprNode;
}

int ThrowNode::accept(NodeVisitor *visitor) {
    return visitor->visitThrowNode(this);
}

// #######################
// ##     CatchNode     ##
// #######################

CatchNode::CatchNode(int lineNum, std::string &&exceptionName, TypeToken *type, BlockNode *blockNode) :
        Node(lineNum), exceptionName(std::move(exceptionName)), typeToken(type), exceptionType(0),
        blockNode(blockNode) {
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

int CatchNode::accept(NodeVisitor *visitor) {
    return visitor->visitCatchNode(this);
}

// #####################
// ##     TryNode     ##
// #####################

TryNode::TryNode(int lineNum, BlockNode *blockNode) :
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

void TryNode::addFinallyNode(Node *finallyNode) {
    if(this->finallyNode != 0) {
        delete this->finallyNode;
    }
    this->finallyNode = finallyNode;
}

Node *TryNode::getFinallyNode() {
    if(this->finallyNode == 0) {
        this->finallyNode = new EmptyNode();
    }
    return this->finallyNode;
}

int TryNode::accept(NodeVisitor *visitor) {
    return visitor->visitTryNode(this);
}

// #########################
// ##     FinallyNode     ##
// #########################

FinallyNode::FinallyNode(int lineNum, BlockNode *block) :
        Node(lineNum), blockNode(block) {
}

FinallyNode::~FinallyNode() {
    delete this->blockNode;
    this->blockNode = 0;
}

BlockNode *FinallyNode::getBlockNode() {
    return this->blockNode;
}

int FinallyNode::accept(NodeVisitor *visitor) {
    return visitor->visitFinallyNode(this);
}

// #########################
// ##     VarDeclNode     ##
// #########################

VarDeclNode::VarDeclNode(int lineNum, std::string &&varName, Node *initValueNode, bool readOnly) :
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

int VarDeclNode::accept(NodeVisitor *visitor) {
    return visitor->visitVarDeclNode(this);
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

int AssignNode::accept(NodeVisitor *visitor) {
    return visitor->visitAssignNode(this);
}

// ##########################
// ##     FunctionNode     ##
// ##########################

FunctionNode::FunctionNode(int lineNum, std::string &&funcName) :
        Node(lineNum), funcName(std::move(funcName)), paramNodes(), paramTypeTokens(), returnTypeToken(),
        returnType(0), blockNode() {
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

void FunctionNode::setReturnTypeToken(TypeToken *typeToken) {
    this->returnTypeToken = typeToken;
}

TypeToken *FunctionNode::getReturnTypeToken() {
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

int FunctionNode::accept(NodeVisitor *visitor) {
    return visitor->visitFunctionNode(this);
}

// #######################
// ##     EmptyNode     ##
// #######################

EmptyNode::EmptyNode() :
        Node(0) {
}

int EmptyNode::accept(NodeVisitor *visitor) {
    return visitor->visitEmptyNode(this);
}

// #######################
// ##     DummyNode     ##
// #######################

DummyNode::DummyNode():
        Node(0) {
}

int DummyNode::accept(NodeVisitor *visitor) {
    return visitor->visitDummyNode(this);
}

// ######################
// ##     RootNode     ##
// ######################

RootNode::RootNode() :
        nodeList() {
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
    case INC:
        return std::string(OP_ADD);
    case DEC:
        return std::string(OP_SUB);
    case ADD_ASSIGN:
        return std::string(OP_ADD);
    case SUB_ASSIGN:
        return std::string(OP_SUB);
    case MUL_ASSIGN:
        return std::string(OP_MUL);
    case DIV_ASSIGN:
        return std::string(OP_DIV);
    case MOD_ASSIGN:
        return std::string(OP_MOD);
    default:
        fatal("unsupported binary op: %s\n", TO_NAME(op));
        return std::string("");
    }
}

ApplyNode *createApplyNode(Node *recvNode, std::string &&methodName) {
    AccessNode *a = new AccessNode(recvNode, std::move(methodName));
    return new ApplyNode(a, new ArgsNode(a->getLineNum()));
}

ForNode *createForInNode(int lineNum, std::string &&initName, Node *exprNode, BlockNode *blockNode) {
    // create for-init
    ApplyNode *apply_reset = createApplyNode(exprNode, std::string(OP_RESET));
    std::string reset_var_name = std::to_string(rand());
    VarDeclNode *reset_varDecl = new VarDeclNode(lineNum, std::string(reset_var_name), apply_reset, true);

    // create for-cond
    VarNode *reset_var = new VarNode(lineNum, std::string(reset_var_name));
    ApplyNode *apply_hasNext = createApplyNode(reset_var, std::string(OP_HAS_NEXT));

    // create forIn-init
    reset_var = new VarNode(lineNum, std::string(reset_var_name));
    ApplyNode *apply_next = createApplyNode(reset_var, std::string(OP_NEXT));
    VarDeclNode *init_var = new VarDeclNode(lineNum, std::move(initName), apply_next, false);

    // insert init to block
    blockNode->insertNodeToFirst(init_var);

    return new ForNode(lineNum, reset_varDecl, apply_hasNext, 0, blockNode);
}

std::string resolveAssignOpName(int op) {
    return std::string();   //FIXME:
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
        //FIXME: element self assignment
        return 0;
    } else {
        // assign to variable or field
        BinaryOpNode *opNode = new BinaryOpNode(new DummyNode(), op, rightNode);
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
