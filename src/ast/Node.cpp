/*
 * Node.cpp
 *
 *  Created on: 2014/12/31
 *      Author: skgchxngsxyz-osx
 */

#include "Node.h"
#include "../core/builtin_variable.h"

// ##################
// ##     Node     ##
// ##################

Node::Node(int lineNum) :
        lineNum(lineNum) {
}

Node::~Node() {
    // TODO Auto-generated destructor stub
}

int Node::getLineNum() {
    return this->lineNum;
}

// ######################
// ##     ExprNode     ##
// ######################

ExprNode::ExprNode(int lineNum) :
        Node(lineNum), type(0) {
}

ExprNode::~ExprNode() {
    delete this->type;
    this->type = 0;
}

void ExprNode::setType(DSType *type) {
    this->type = type;
}

DSType *ExprNode::getType() {
    return this->type;
}

// ##########################
// ##     IntValueNode     ##
// ##########################

IntValueNode::IntValueNode(int lineNum, long value) :
        ExprNode(lineNum), value(value) {
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
        ExprNode(lineNum), value(value) {
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
        ExprNode(lineNum), value(value) {
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
        ExprNode(0), value(std::move(value)) {
}

StringValueNode::StringValueNode(int lineNum, char *value, bool isSingleQuoteStr) :	//TODO:
        ExprNode(lineNum) {
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
     case 't' : ch = '\t'; break;
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
        ExprNode(lineNum), nodes() {
}

StringExprNode::~StringExprNode() {
    for(ExprNode *e : this->nodes) {
        delete e;
    }
    this->nodes.clear();
}

void StringExprNode::addExprNode(ExprNode *node) {	//TODO:
    this->nodes.push_back(node);
}

const std::vector<ExprNode*> &StringExprNode::getExprNodes() {
    return this->nodes;
}

int StringExprNode::accept(NodeVisitor *visitor) {
    return visitor->visitStringExprNode(this);
}

// #######################
// ##     ArrayNode     ##
// #######################

ArrayNode::ArrayNode(int lineNum) :
        ExprNode(lineNum), nodes() {
}

ArrayNode::~ArrayNode() {
    for(ExprNode *e : this->nodes) {
        delete e;
    }
    this->nodes.clear();
}

void ArrayNode::addExprNode(ExprNode *node) {
    this->nodes.push_back(node);
}

const std::vector<ExprNode*> &ArrayNode::getExprNodes() {
    return this->nodes;
}

int ArrayNode::accept(NodeVisitor *visitor) {
    return visitor->visitArrayNode(this);
}

// #####################
// ##     MapNode     ##
// #####################

MapNode::MapNode(int lineNum) :
        ExprNode(lineNum), keyNodes(), valueNodes() {
}

MapNode::~MapNode() {
    for(ExprNode *e : this->keyNodes) {
        delete e;
    }
    this->keyNodes.clear();
    for(ExprNode *e : this->valueNodes) {
        delete e;
    }
    this->valueNodes.clear();
}

void MapNode::addEntry(ExprNode *keyNode, ExprNode *valueNode) {
    this->keyNodes.push_back(keyNode);
    this->valueNodes.push_back(valueNode);
}

const std::vector<ExprNode*> &MapNode::getKeyNodes() {
    return this->keyNodes;
}

const std::vector<ExprNode*> &MapNode::getValueNodes() {
    return this->valueNodes;
}

int MapNode::accept(NodeVisitor *visitor) {
    return visitor->visitMapNode(this);
}

// ######################
// ##     PairNode     ##
// ######################

PairNode::PairNode(int lineNum, ExprNode *leftNode, ExprNode *rightNode) :
        ExprNode(lineNum), leftNode(leftNode), rightNode(rightNode) {
}

PairNode::~PairNode() {
    delete this->leftNode;
    this->leftNode = 0;

    delete this->rightNode;
    this->rightNode = 0;
}

ExprNode *PairNode::getLeftNode() {
    return this->leftNode;
}

ExprNode *PairNode::getRightNode() {
    return this->rightNode;
}

int PairNode::accept(NodeVisitor *visitor) {
    return visitor->visitPairNode(this);
}

// ############################
// ##     AssignableNode     ##
// ############################

AssignableNode::AssignableNode(int lineNum) :
        ExprNode(lineNum) {
}

AssignableNode::~AssignableNode() {
}

// ########################
// ##     SymbolNode     ##
// ########################

VarNode::VarNode(int lineNum, std::string &&varName) :
        AssignableNode(lineNum), varName(std::move(varName)), readOnly(false), global(false), varIndex(-1) {
}

const std::string &VarNode::getVarName() {
    return this->varName;
}

bool VarNode::isReadOnly() {
    return this->readOnly;
}

int VarNode::accept(NodeVisitor *visitor) {
    return visitor->visitVarNode(this);
}

void VarNode::setReadOnly(bool readOnly) {
    this->readOnly = readOnly;
}

void VarNode::setGlobal(bool global) {
    this->global = global;
}

bool VarNode::isGlobal() {
    return this->global;
}

void VarNode::setVarIndex(int index) {
    this->varIndex = index;
}

int VarNode::getVarIndex() {
    return this->varIndex;
}

// #######################
// ##     IndexNode     ##
// #######################

IndexNode::IndexNode(int lineNum, ExprNode *recvNode, ExprNode *indexNode) :
        AssignableNode(lineNum), recvNode(recvNode), indexNode(indexNode),
        getterHandle(0), setterHandle(0) {
}

IndexNode::~IndexNode() {
    delete this->recvNode;
    this->recvNode = 0;

    delete this->indexNode;
    this->indexNode = 0;
}

ExprNode *IndexNode::getRecvNode() {
    return this->recvNode;
}

ExprNode *IndexNode::getIndexNode() {
    return this->indexNode;
}

void IndexNode::setGetterHandle(FunctionHandle *handle) {
    this->getterHandle = handle;
}

FunctionHandle *IndexNode::getGetterHandle() {
    return this->getterHandle;
}

void IndexNode::setSetterHandle(FunctionHandle *handle) {
    this->setterHandle = handle;
}

FunctionHandle *IndexNode::getSetterHandle() {
    return this->setterHandle;
}

bool IndexNode::isReadOnly() {
    return false;
}

int IndexNode::accept(NodeVisitor *visitor) {
    return visitor->visitIndexNode(this);
}

// ########################
// ##     AccessNode     ##
// ########################

AccessNode::AccessNode(int lineNum, ExprNode *recvNode, std::string &&fieldName) :
        AssignableNode(lineNum), recvNode(recvNode), fieldName(std::move(fieldName)), fieldIndex(-1) {
}

AccessNode::~AccessNode() {
    delete this->recvNode;
    this->recvNode = 0;
}

ExprNode *AccessNode::getRecvNode() {
    return this->recvNode;
}

const std::string &AccessNode::getFieldName() {
    return this->fieldName;
}

void AccessNode::setFieldIndex(int index) {
    this->fieldIndex = index;
}

int AccessNode::getFieldIndex() {
    return this->fieldIndex;
}

bool AccessNode::isReadOnly() {
    return false;	//TODO: handle.isReadOnly()
}

int AccessNode::accept(NodeVisitor *visitor) {
    return visitor->visitAccessNode(this);
}

// ######################
// ##     CastNode     ##
// ######################

CastNode::CastNode(int lineNum, ExprNode *targetNode, TypeToken *type) :
        ExprNode(lineNum), targetNode(targetNode), targetTypeToken(type) {
}

CastNode::~CastNode() {
    delete this->targetNode;
    this->targetNode = 0;

    delete this->targetTypeToken;
    this->targetTypeToken = 0;
}

ExprNode *CastNode::getTargetNode() {
    return this->targetNode;
}

TypeToken *CastNode::getTargetTypeToken() {
    return this->targetTypeToken;
}

int CastNode::accept(NodeVisitor *visitor) {
    return visitor->visitCastNode(this);
}

// ############################
// ##     InstanceOfNode     ##
// ############################

InstanceOfNode::InstanceOfNode(int lineNum, ExprNode *targetNode, TypeToken *type) :
        ExprNode(lineNum), targetNode(targetNode), targetTypeToken(type),
        targetType(0), opKind(ALWAYS_FALSE) {
}

InstanceOfNode::~InstanceOfNode() {
    delete this->targetNode;
    this->targetNode = 0;

    delete this->targetTypeToken;
    this->targetTypeToken = 0;
}

ExprNode *InstanceOfNode::getTargetNode() {
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

void InstanceOfNode::resolveOpKind(int opKind) {
    switch(opKind) {
    case ALWAYS_FALSE:
    case INSTANCEOF:
        this->opKind = opKind;
        break;
    }
}

int InstanceOfNode::getOpKind() {
    return this->opKind;
}

int InstanceOfNode::accept(NodeVisitor *visitor) {
    return visitor->visitInstanceOfNode(this);
}

// #######################
// ##     ApplyNode     ##
// #######################

ApplyNode::ApplyNode(ExprNode *recvNode) :
        ApplyNode(recvNode, false) {
}

ApplyNode::ApplyNode(ExprNode *recvNode, bool overload) :
        ExprNode(recvNode->getLineNum()), recvNode(recvNode), argNodes(),
        asFuncCall(false), overload(false) {
}

ApplyNode::~ApplyNode() {
    delete this->recvNode;
    this->recvNode = 0;

    for(ExprNode *e : this->argNodes) {
        delete e;
    }
    this->argNodes.clear();
}

ExprNode *ApplyNode::getRecvNode() {
    return this->recvNode;
}

/**
 * for parser
 */
void ApplyNode::addArgNode(ExprNode *node) {
    this->argNodes.push_back(node);
}

const std::vector<ExprNode*> &ApplyNode::getArgNodes() {
    return this->argNodes;
}

bool ApplyNode::isFuncCall() {
    return this->asFuncCall;
}

bool ApplyNode::isOverload() {
    return this->overload;
}

int ApplyNode::accept(NodeVisitor *visitor) {
    return visitor->visitApplyNode(this);
}

// #####################
// ##     NewNode     ##
// #####################

NewNode::NewNode(int lineNum, TypeToken *targetTypeToken) :
        ExprNode(lineNum), targetTypeToken(targetTypeToken) {
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

int NewNode::accept(NodeVisitor *visitor) {
    return visitor->visitNewNode(this);
}

// ########################
// ##     CondOpNode     ##
// ########################

CondOpNode::CondOpNode(int lineNum, ExprNode *leftNode, ExprNode *rightNode, bool isAndOp) :
        ExprNode(lineNum), leftNode(leftNode), rightNode(rightNode), andOp(isAndOp) {
}

CondOpNode::~CondOpNode() {
    delete this->leftNode;
    this->leftNode = 0;

    delete this->rightNode;
    this->rightNode = 0;
}

ExprNode *CondOpNode::getLeftNode() {
    return this->leftNode;
}

ExprNode *CondOpNode::getRightNode() {
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
        ExprNode(lineNum), commandName(std::move(commandName)), argNodes(), redirOptions() {
}

ProcessNode::~ProcessNode() {
    for(ProcArgNode *e : this->argNodes) {
        delete e;
    }
    this->argNodes.clear();

    for(const std::pair<int, ExprNode*> &pair : this->redirOptions) {
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

void ProcessNode::addRedirOption(std::pair<int, ExprNode*> &&optionPair) {
    this->redirOptions.push_back(std::move(optionPair));
}

const std::vector<std::pair<int, ExprNode*>> &ProcessNode::getRedirOptions() {
    return this->redirOptions;
}

int ProcessNode::accept(NodeVisitor *visitor) {
    return visitor->visitProcessNode(this);
}

// #########################
// ##     ProcArgNode     ##
// #########################

ProcArgNode::ProcArgNode(int lineNum) :
        ExprNode(lineNum), segmentNodes() {
}

ProcArgNode::~ProcArgNode() {
    for(ExprNode *e : this->segmentNodes) {
        delete e;
    }
    this->segmentNodes.clear();
}

void ProcArgNode::addSegmentNode(ExprNode *node) {
    ProcArgNode *argNode = dynamic_cast<ProcArgNode*>(node);
    if(argNode != 0) {
        int size = argNode->getSegmentNodes().size();
        for(int i = 0; i < size; i++) {
            ExprNode *s = argNode->segmentNodes[i];
            argNode->segmentNodes[i] = 0;
            this->segmentNodes.push_back(s);
        }
        delete argNode;
        return;
    }
    this->segmentNodes.push_back(node);
}

const std::vector<ExprNode*> &ProcArgNode::getSegmentNodes() {
    return this->segmentNodes;
}

int ProcArgNode::accept(NodeVisitor *visitor) {
    return visitor->visitProcArgNode(this);
}

// #############################
// ##     SpecialCharNode     ##
// #############################

SpecialCharNode::SpecialCharNode(int lineNum) :
        ExprNode(lineNum) {
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
        ExprNode(0), procNodes(), background(false) {
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

InnerTaskNode::InnerTaskNode(ExprNode *exprNode) :
        ExprNode(0), exprNode(exprNode) {
}

InnerTaskNode::~InnerTaskNode() {
    delete this->exprNode;
    this->exprNode = 0;
}

ExprNode *InnerTaskNode::getExprNode() {
    return this->exprNode;
}

int InnerTaskNode::accept(NodeVisitor *visitor) {
    return visitor->visitInnerTaskNode(this);
}

// ########################
// ##     AssertNode     ##
// ########################

AssertNode::AssertNode(int lineNum, ExprNode *exprNode) :
        Node(lineNum), exprNode(exprNode) {
}

AssertNode::~AssertNode() {
    delete this->exprNode;
    this->exprNode = 0;
}

ExprNode *AssertNode::getExprNode() {
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

ExportEnvNode::ExportEnvNode(int lineNum, std::string &&envName, ExprNode *exprNode) :
        Node(lineNum), envName(std::move(envName)), exprNode(exprNode) {
}

ExportEnvNode::~ExportEnvNode() {
    delete this->exprNode;
    this->exprNode = 0;
}

const std::string &ExportEnvNode::getEnvName() {
    return this->envName;
}

ExprNode *ExportEnvNode::getExprNode() {
    return this->exprNode;
}

int ExportEnvNode::accept(NodeVisitor *visitor) {
    return visitor->visitExportEnvNode(this);
}

// ###########################
// ##     ImportEnvNode     ##
// ###########################

ImportEnvNode::ImportEnvNode(int lineNum, std::string &&envName) :
        Node(lineNum), envName(std::move(envName)) {
}

const std::string &ImportEnvNode::getEnvName() {
    return this->envName;
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

WhileNode::WhileNode(int lineNum, ExprNode *condNode, BlockNode *blockNode, bool asDoWhile) :
        LoopNode(lineNum), condNode(condNode), blockNode(blockNode), asDoWhile(asDoWhile) {
}

WhileNode::~WhileNode() {
    delete this->condNode;
    this->condNode = 0;

    delete this->blockNode;
    this->blockNode = 0;
}

ExprNode *WhileNode::getCondNode() {
    return this->condNode;
}

BlockNode *WhileNode::getBlockNode() {
    return this->blockNode;
}

bool WhileNode::isDoWhile() {
    return this->asDoWhile;
}

int WhileNode::accept(NodeVisitor *visitor) {
    return visitor->visitWhileNode(this);
}

// ####################
// ##     IfNode     ##
// ####################

IfNode::IfNode(int lineNum, ExprNode *condNode, BlockNode *thenNode, BlockNode *elseNode) :
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

ExprNode *IfNode::getCondNode() {
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

ReturnNode::ReturnNode(int lineNum, ExprNode *exprNode) :
        BlockEndNode(lineNum), exprNode(exprNode) {
}

ReturnNode::ReturnNode(int lineNum) :
        BlockEndNode(lineNum), exprNode() {
}

ReturnNode::~ReturnNode() {
    delete this->exprNode;
    this->exprNode = 0;
}

ExprNode *ReturnNode::getExprNode() {
    return this->exprNode;
}

int ReturnNode::accept(NodeVisitor *visitor) {
    return visitor->visitReturnNode(this);
}

// #######################
// ##     ThrowNode     ##
// #######################

ThrowNode::ThrowNode(int lineNum, ExprNode *exprNode) :
        BlockEndNode(lineNum), exprNode(exprNode) {
}

ThrowNode::~ThrowNode() {
    delete this->exprNode;
    this->exprNode = 0;
}

ExprNode *ThrowNode::getExprNode() {
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

VarDeclNode::VarDeclNode(int lineNum, std::string &&varName, ExprNode *initValueNode, bool readOnly) :
        Node(lineNum), varName(std::move(varName)), readOnly(readOnly), global(false),
        initValueNode(initValueNode) {
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

void VarDeclNode::setGlobal(bool global) {
    this->global = global;
}

bool VarDeclNode::isGlobal() {
    return this->global;
}

ExprNode *VarDeclNode::getInitValueNode() {
    return this->initValueNode;
}

int VarDeclNode::accept(NodeVisitor *visitor) {
    return visitor->visitVarDeclNode(this);
}

// ########################
// ##     AssignNode     ##
// ########################

AssignNode::AssignNode(int lineNum, ExprNode *leftNode, ExprNode *rightNode) :
        ExprNode(lineNum), leftNode(leftNode), rightNode(rightNode), handle(0) {
}

AssignNode::~AssignNode() {
    delete this->leftNode;
    this->leftNode = 0;

    delete this->rightNode;
    this->rightNode  = 0;
}

ExprNode *AssignNode::getLeftNode() {
    return this->leftNode;
}

void AssignNode::setRightNode(ExprNode *rightNode) {
    this->rightNode = rightNode;
}

ExprNode *AssignNode::getRightNode() {
    return this->rightNode;
}

void AssignNode::setHandle(FunctionHandle *handle) {
    this->handle = handle;
}

FunctionHandle *AssignNode::getHandle() {
    return this->handle;
}

int AssignNode::accept(NodeVisitor *visitor) {
    return visitor->visitAssignNode(this);
}

// ##########################
// ##     FunctionNode     ##
// ##########################

FunctionNode::FunctionNode(int lineNum, std::string &&funcName) :
        Node(lineNum), funcName(std::move(funcName)), paramNodes(), paramTypeTokens(), returnTypeToken(), returnType(
                0), blockNode() {
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
        ExprNode(0) {
}

int EmptyNode::accept(NodeVisitor *visitor) {
    return visitor->visitEmptyNode(this);
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
