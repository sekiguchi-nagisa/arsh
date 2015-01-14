/*
 * Node.cpp
 *
 *  Created on: 2014/12/31
 *      Author: skgchxngsxyz-osx
 */

#include "Node.h"

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
    if(this->type != 0) {
        delete this->type;
    }
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
}

void StringExprNode::addExprNode(std::unique_ptr<ExprNode> &&node) {	//TODO:
    this->nodes.push_back(std::move(node));
}

const std::vector<std::unique_ptr<ExprNode>> &StringExprNode::getExprNodes() {
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
}

void ArrayNode::addExprNode(std::unique_ptr<ExprNode> &&node) {
    this->nodes.push_back(std::move(node));
}

const std::vector<std::unique_ptr<ExprNode>> &ArrayNode::getExprNodes() {
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
}

void MapNode::addEntry(std::unique_ptr<ExprNode> &&keyNode, std::unique_ptr<ExprNode> &&valueNode) {
    this->keyNodes.push_back(std::move(keyNode));
    this->valueNodes.push_back(std::move(valueNode));
}

const std::vector<std::unique_ptr<ExprNode>> &MapNode::getkeyNodes() {
    return this->keyNodes;
}

const std::vector<std::unique_ptr<ExprNode>> &MapNode::getValueNodes() {
    return this->valueNodes;
}

int MapNode::accept(NodeVisitor *visitor) {
    return visitor->visitMapNode(this);
}

// ######################
// ##     PairNode     ##
// ######################

PairNode::PairNode(int lineNum, std::unique_ptr<ExprNode> &&leftNode,
        std::unique_ptr<ExprNode> &&rightNode) :
        ExprNode(lineNum), leftNode(std::move(leftNode)), rightNode(std::move(rightNode)) {
}

PairNode::~PairNode() {
}

const std::unique_ptr<ExprNode> &PairNode::getLeftNode() {
    return this->leftNode;
}

const std::unique_ptr<ExprNode> &PairNode::getRightNode() {
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
        AssignableNode(lineNum), varName(std::move(varName)), readOnly(false) {
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

// #######################
// ##     IndexNode     ##
// #######################

IndexNode::IndexNode(int lineNum, std::unique_ptr<ExprNode> &&recvNode,
        std::unique_ptr<ExprNode> &&indexNode) :
        AssignableNode(lineNum), recvNode(std::move(recvNode)), indexNode(std::move(indexNode)), getterHandle(
                0), setterHandle(0) {
}

IndexNode::~IndexNode() {
}

const std::unique_ptr<ExprNode> &IndexNode::getRecvNode() {
    return this->recvNode;
}

const std::unique_ptr<ExprNode> &IndexNode::getIndexNode() {
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

AccessNode::AccessNode(int lineNum, std::unique_ptr<ExprNode> &&recvNode, std::string &&fieldName) :
        AssignableNode(lineNum), recvNode(std::move(recvNode)), fieldName(std::move(fieldName)), handle(
                0) {
}

AccessNode::~AccessNode() {
}

const std::unique_ptr<ExprNode> &AccessNode::getRecvNode() {
    return this->recvNode;
}

const std::string &AccessNode::getFieldName() {
    return this->fieldName;
}

void AccessNode::setFieldHandle(FieldHandle *handle) {
    this->handle = handle;
}

FieldHandle *AccessNode::getFieldHandle() {
    return this->handle;
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

CastNode::CastNode(int lineNum, std::unique_ptr<ExprNode> &&targetNode,
        std::unique_ptr<TypeToken> &&type) :
        ExprNode(lineNum), targetNode(std::move(targetNode)), targetTypeToken(std::move(type)) {
}

CastNode::~CastNode() {
}

const std::unique_ptr<ExprNode> &CastNode::getTargetNode() {
    return this->targetNode;
}

const std::unique_ptr<TypeToken> &CastNode::getTargetTypeToken() {
    return this->targetTypeToken;
}

int CastNode::accept(NodeVisitor *visitor) {
    return visitor->visitCastNode(this);
}

// ############################
// ##     InstanceOfNode     ##
// ############################

InstanceOfNode::InstanceOfNode(int lineNum, std::unique_ptr<ExprNode> &&targetNode,
        std::unique_ptr<TypeToken> &&type) :
        ExprNode(lineNum), targetNode(std::move(targetNode)), targetTypeToken(std::move(type)) {
}

InstanceOfNode::~InstanceOfNode() {
}

const std::unique_ptr<ExprNode> &InstanceOfNode::getTargetNode() {
    return this->targetNode;
}

const std::unique_ptr<TypeToken> &InstanceOfNode::getTargetTypeToken() {
    return this->targetTypeToken;
}

int InstanceOfNode::accept(NodeVisitor *visitor) {
    return visitor->visitInstanceOfNode(this);
}

// #######################
// ##     ApplyNode     ##
// #######################

ApplyNode::ApplyNode(int lineNum, std::unique_ptr<ExprNode> &&recvNode) :
        ExprNode(lineNum), recvNode(std::move(recvNode)), argNodes() {
}

ApplyNode::~ApplyNode() {
}

const std::unique_ptr<ExprNode> &ApplyNode::getRecvNode() {
    return this->recvNode;
}

/**
 * for parser
 */
void ApplyNode::addArgNode(std::unique_ptr<ExprNode> &&node) {
    this->argNodes.push_back(std::move(node));
}

const std::vector<std::unique_ptr<ExprNode>> &ApplyNode::getArgNodes() {
    return this->argNodes;
}

int ApplyNode::accept(NodeVisitor *visitor) {
    return visitor->visitApplyNode(this);
}

// #################################
// ##     ConstructorCallNode     ##
// #################################

ConstructorCallNode::ConstructorCallNode(int lineNum, std::unique_ptr<TypeToken> &&type) :
        ExprNode(lineNum), targetType(std::move(type)), argNodes(), handle(0) {
}

ConstructorCallNode::~ConstructorCallNode() {
}

const std::unique_ptr<TypeToken> &ConstructorCallNode::getTargetType() {
    return this->targetType;
}

void ConstructorCallNode::addArgNode(std::unique_ptr<ExprNode> &&node) {
    this->argNodes.push_back(std::move(node));
}

const std::vector<std::unique_ptr<ExprNode>> &ConstructorCallNode::getArgNodes() {
    return this->argNodes;
}

void ConstructorCallNode::setConstructorHandle(ConstructorHandle *handle) {
    this->handle = handle;
}

ConstructorHandle *ConstructorCallNode::getConstructorHandle() {
    return this->handle;
}

int ConstructorCallNode::accept(NodeVisitor *visitor) {
    return visitor->visitConstructorCallNode(this);
}

// ########################
// ##     CondOpNode     ##
// ########################

CondOpNode::CondOpNode(int lineNum, std::unique_ptr<ExprNode> &&leftNode,
        std::unique_ptr<ExprNode> &&rightNode, bool isAndOp) :
        ExprNode(lineNum), leftNode(std::move(leftNode)), rightNode(std::move(rightNode)), andOp(
                isAndOp) {
}

CondOpNode::~CondOpNode() {
}

const std::unique_ptr<ExprNode> &CondOpNode::getLeftNode() {
    return this->leftNode;
}

const std::unique_ptr<ExprNode> &CondOpNode::getRightNode() {
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
}

const std::string &ProcessNode::getCommandName() {
    return this->commandName;
}

void ProcessNode::addArgNode(std::unique_ptr<ProcArgNode> &&node) {
    this->argNodes.push_back(std::move(node));
}

const std::vector<std::unique_ptr<ProcArgNode>> &ProcessNode::getArgNodes() {
    return this->argNodes;
}

void ProcessNode::addRedirOption(std::pair<int, std::unique_ptr<ExprNode>> &&optionPair) {
    this->redirOptions.push_back(std::move(optionPair));
}

const std::vector<std::pair<int, std::unique_ptr<ExprNode>>> &ProcessNode::getRedirOptions() {
    return this->redirOptions;
}

int ProcessNode::accept(NodeVisitor *visitor) {
    return visitor->visitProcessNode(this);
}

// ##########################
// ##     ArgumentNode     ##
// ##########################

ProcArgNode::ProcArgNode(int lineNum) :
        ExprNode(lineNum), segmentNodes() {
}

ProcArgNode::~ProcArgNode() {
}

void ProcArgNode::addSegmentNode(std::unique_ptr<ExprNode> &&node) {
    ProcArgNode *argNode = dynamic_cast<ProcArgNode*>(node.release());
    if(argNode != 0) {
        int size = argNode->getSegmentNodes().size();
        for(int i = 0; i < size; i++) {
            this->segmentNodes.push_back(std::move(argNode->segmentNodes[i]));
        }
        delete argNode;
        return;
    }
    this->segmentNodes.push_back(std::move(node));
}

const std::vector<std::unique_ptr<ExprNode>> &ProcArgNode::getSegmentNodes() {
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
}

void TaskNode::addProcNodes(std::unique_ptr<ProcessNode> &&node) {
    this->procNodes.push_back(std::move(node));
}

const std::vector<std::unique_ptr<ProcessNode>> &TaskNode::getProcNodes() {
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

InnerTaskNode::InnerTaskNode(std::unique_ptr<ExprNode> &&exprNode) :
        ExprNode(0), exprNode(std::move(exprNode)) {
}

InnerTaskNode::~InnerTaskNode() {
}

const std::unique_ptr<ExprNode> &InnerTaskNode::getExprNode() {
    return this->exprNode;
}

int InnerTaskNode::accept(NodeVisitor *visitor) {
    return visitor->visitInnerTaskNode(this);
}

// ########################
// ##     AssertNode     ##
// ########################

AssertNode::AssertNode(int lineNum, std::unique_ptr<ExprNode> &&exprNode) :
        Node(lineNum), exprNode(std::move(exprNode)) {
}

AssertNode::~AssertNode() {
}

const std::unique_ptr<ExprNode> &AssertNode::getExprNode() {
    return this->exprNode;
}

int AssertNode::accept(NodeVisitor *visitor) {
    return visitor->visitAssertNode(this);
}

// #######################
// ##     BlockNode     ##
// #######################

BlockNode::BlockNode() :
        Node(0), nodes() {
}

BlockNode::~BlockNode() {
}

void BlockNode::addNode(std::unique_ptr<Node> &&node) {
    this->nodes.push_back(std::move(node));
}

const std::vector<std::unique_ptr<Node>> &BlockNode::getNodes() {
    return this->nodes;
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

ExportEnvNode::ExportEnvNode(int lineNum, std::string &&envName,
        std::unique_ptr<ExprNode> &&exprNode) :
        Node(lineNum), envName(std::move(envName)), exprNode(std::move(exprNode)) {
}

ExportEnvNode::~ExportEnvNode() {
}

const std::string &ExportEnvNode::getEnvName() {
    return this->envName;
}

const std::unique_ptr<ExprNode> &ExportEnvNode::getExprNode() {
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

ForNode::ForNode(int lineNum, std::unique_ptr<Node> &&initNode, std::unique_ptr<Node> &&condNode,
        std::unique_ptr<Node> &&iterNode, std::unique_ptr<BlockNode> &&blockNode) :
        LoopNode(lineNum), initNode(std::move(initNode)), condNode(std::move(condNode)), iterNode(
                std::move(iterNode)), blockNode(std::move(blockNode)) {
}

ForNode::~ForNode() {
}

const std::unique_ptr<Node> &ForNode::getInitNode() {
    return this->initNode;
}

const std::unique_ptr<Node> &ForNode::getCondNode() {
    return this->condNode;
}

const std::unique_ptr<Node> &ForNode::getIterNode() {
    return this->iterNode;
}

const std::unique_ptr<BlockNode> &ForNode::getBlockNode() {
    return this->blockNode;
}

int ForNode::accept(NodeVisitor *visitor) {
    return visitor->visitForNode(this);
}

// #######################
// ##     ForInNode     ##
// #######################

ForInNode::ForInNode(int lineNum, std::string &&initName, std::unique_ptr<ExprNode> &&exprNode,
        std::unique_ptr<BlockNode> &&blockNode) :
        LoopNode(lineNum), initName(std::move(initName)), exprNode(std::move(exprNode)), blockNode(
                std::move(blockNode)), resetHandle(0), nextHandle(0), hasNextHandle(0) {
}

ForInNode::~ForInNode() {
}

const std::string &ForInNode::getInitName() {
    return this->initName;
}

const std::unique_ptr<ExprNode> &ForInNode::getExprNode() {
    return this->exprNode;
}

const std::unique_ptr<BlockNode> &ForInNode::getBlockNode() {
    return this->blockNode;
}

void ForInNode::setIteratorHandle(FunctionHandle *resetHandle, FunctionHandle *nextHandle,
        FunctionHandle *hasNextHandle) {
    this->resetHandle = resetHandle;
    this->nextHandle = nextHandle;
    this->hasNextHandle = hasNextHandle;
}

FunctionHandle *ForInNode::getResetHandle() {
    return this->resetHandle;
}

FunctionHandle *ForInNode::getNextHandle() {
    return this->nextHandle;
}

FunctionHandle *ForInNode::getHasNextHandle() {
    return this->hasNextHandle;
}

int ForInNode::accept(NodeVisitor *visitor) {
    return visitor->visitForInNode(this);
}

// #######################
// ##     WhileNode     ##
// #######################

WhileNode::WhileNode(int lineNum, std::unique_ptr<ExprNode> &&condNode,
        std::unique_ptr<BlockNode> &&blockNode, bool asDoWhile) :
        LoopNode(lineNum), condNode(std::move(condNode)), blockNode(std::move(blockNode)), asDoWhile(
                asDoWhile) {
}

WhileNode::~WhileNode() {
}

const std::unique_ptr<ExprNode> &WhileNode::getCondNode() {
    return this->condNode;
}

const std::unique_ptr<BlockNode> &WhileNode::getBlockNode() {
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

IfNode::IfNode(int lineNum, std::unique_ptr<ExprNode> &&condNode,
        std::unique_ptr<BlockNode> &&thenNode, std::unique_ptr<BlockNode> &&elseNode) :
        Node(lineNum), condNode(std::move(condNode)), thenNode(std::move(thenNode)), elseNode(
                std::move(elseNode)) {
}

IfNode::~IfNode() {
}

const std::unique_ptr<ExprNode> &IfNode::getCondNode() {
    return this->condNode;
}

const std::unique_ptr<BlockNode> &IfNode::getThenNode() {
    return this->thenNode;
}

const std::unique_ptr<BlockNode> &IfNode::getElseNode() {
    return this->elseNode;
}

int IfNode::accept(NodeVisitor *visitor) {
    return visitor->visitIfNode(this);
}

// ########################
// ##     ReturnNode     ##
// ########################

ReturnNode::ReturnNode(int lineNum, std::unique_ptr<ExprNode> &&exprNode) :
        BlockEndNode(lineNum), exprNode(std::move(exprNode)) {
}

ReturnNode::ReturnNode(int lineNum) :
        BlockEndNode(lineNum), exprNode() {
}

ReturnNode::~ReturnNode() {
}

const std::unique_ptr<ExprNode> &ReturnNode::getExprNode() {
    return this->exprNode;
}

int ReturnNode::accept(NodeVisitor *visitor) {
    return visitor->visitReturnNode(this);
}

// #######################
// ##     ThrowNode     ##
// #######################

ThrowNode::ThrowNode(int lineNum, std::unique_ptr<ExprNode> &&exprNode) :
        BlockEndNode(lineNum), exprNode(std::move(exprNode)) {
}

ThrowNode::~ThrowNode() {
}

const std::unique_ptr<ExprNode> &ThrowNode::getExprNode() {
    return this->exprNode;
}

int ThrowNode::accept(NodeVisitor *visitor) {
    return visitor->visitThrowNode(this);
}

// #######################
// ##     CatchNode     ##
// #######################

CatchNode::CatchNode(int lineNum, std::string &&exceptionName, std::unique_ptr<TypeToken> &&type,
        std::unique_ptr<BlockNode> &&blockNode) :
        Node(lineNum), exceptionName(std::move(exceptionName)), typeToken(std::move(type)), exceptionType(
                0), blockNode(std::move(blockNode)) {
}

CatchNode::~CatchNode() {
}

const std::string &CatchNode::getExceptionName() {
    return this->exceptionName;
}

const std::unique_ptr<TypeToken> &CatchNode::getTypeToken() {
    return this->typeToken;
}

void CatchNode::setExceptionType(DSType *type) {
    this->exceptionType = type;
}

DSType *CatchNode::getExceptionType() {
    return this->exceptionType;
}

const std::unique_ptr<BlockNode> &CatchNode::getBlockNode() {
    return this->blockNode;
}

int CatchNode::accept(NodeVisitor *visitor) {
    return visitor->visitCatchNode(this);
}

// #####################
// ##     TryNode     ##
// #####################

TryNode::TryNode(int lineNum, std::unique_ptr<BlockNode> &&blockNode) :
        Node(lineNum), blockNode(std::move(blockNode)), catchNodes(), finallyNode() {
}

TryNode::~TryNode() {
}

void TryNode::addCatchNode(std::unique_ptr<CatchNode> &&catchNode) {
    this->catchNodes.push_back(std::move(catchNode));
}

const std::vector<std::unique_ptr<CatchNode>> &TryNode::getCatchNodes() {
    return this->catchNodes;
}

void TryNode::addFinallyNode(std::unique_ptr<Node> &&finallyNode) {
    this->finallyNode = std::move(finallyNode);
}

const std::unique_ptr<Node> &TryNode::getFinallyNode() {	//FIXME:
    return this->finallyNode;
}

int TryNode::accept(NodeVisitor *visitor) {
    return visitor->visitTryNode(this);
}

// #########################
// ##     FinallyNode     ##
// #########################

FinallyNode::FinallyNode(int lineNum, std::unique_ptr<BlockNode> &&block) :
        Node(lineNum), blockNode(std::move(block)) {
}

FinallyNode::~FinallyNode() {
}

const std::unique_ptr<BlockNode> &FinallyNode::getBlockNode() {
    return this->blockNode;
}

int FinallyNode::accept(NodeVisitor *visitor) {
    return visitor->visitFinallyNode(this);
}

// #########################
// ##     VarDeclNode     ##
// #########################

VarDeclNode::VarDeclNode(int lineNum, std::string &&varName,
        std::unique_ptr<ExprNode> &&initValueNode, bool readOnly) :
        Node(lineNum), varName(std::move(varName)), readOnly(readOnly), global(false), initValueNode(
                std::move(initValueNode)) {
}

VarDeclNode::~VarDeclNode() {
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

const std::unique_ptr<ExprNode> &VarDeclNode::getInitValueNode() {
    return this->initValueNode;
}

int VarDeclNode::accept(NodeVisitor *visitor) {
    return visitor->visitVarDeclNode(this);
}

// ########################
// ##     AssignNode     ##
// ########################

AssignNode::AssignNode(int lineNum, std::unique_ptr<ExprNode> &&leftNode,
        std::unique_ptr<ExprNode> &&rightNode) :
        ExprNode(lineNum), leftNode(std::move(leftNode)), rightNode(std::move(rightNode)), handle(0) {
}

AssignNode::~AssignNode() {
}

const std::unique_ptr<ExprNode> &AssignNode::getLeftNode() {
    return this->leftNode;
}

void AssignNode::setRightNode(std::unique_ptr<ExprNode> &&rightNode) {
    this->rightNode = std::move(rightNode);
}

const std::unique_ptr<ExprNode> &AssignNode::getRightNode() {
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
}

const std::string &FunctionNode::getFuncName() {
    return this->funcName;
}

void FunctionNode::addParamNode(std::unique_ptr<VarNode> &&node,
        std::unique_ptr<TypeToken> &&paramType) {
    this->paramNodes.push_back(std::move(node));
    this->paramTypeTokens.push_back(std::move(paramType));
}

const std::vector<std::unique_ptr<VarNode>> &FunctionNode::getParamNodes() {
    return this->paramNodes;
}

const std::vector<std::unique_ptr<TypeToken>> &FunctionNode::getParamTypeTokens() {
    return this->paramTypeTokens;
}

void FunctionNode::setReturnTypeToken(std::unique_ptr<TypeToken> &&typeToken) {
    this->returnTypeToken = std::move(typeToken);
}

const std::unique_ptr<TypeToken> &FunctionNode::getReturnTypeToken() {
    return this->returnTypeToken;
}

void FunctionNode::setReturnType(DSType *returnType) {
    this->returnType = returnType;
}

DSType *FunctionNode::getReturnType() {
    return this->returnType;
}

void FunctionNode::setBlockNode(std::unique_ptr<BlockNode> &&blockNode) {
    this->blockNode = std::move(blockNode);
}

const std::unique_ptr<BlockNode> &FunctionNode::getBlockNode() {
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

// ############################
// ##     EmptyBlockNode     ##
// ############################

EmptyBlockNode::EmptyBlockNode() :
        BlockNode() {
}

EmptyBlockNode::~EmptyBlockNode() {
}

void EmptyBlockNode::addNode(std::unique_ptr<Node> &&node) {
}

int EmptyBlockNode::accept(NodeVisitor *visitor) {
    return visitor->visitEmptyBlockNode(this);
}

std::unique_ptr<EmptyBlockNode> EmptyBlockNode::emptyBlockNode = std::unique_ptr<EmptyBlockNode>(
        new EmptyBlockNode());

// ######################
// ##     RootNode     ##
// ######################

RootNode::RootNode() :
        nodes() {
}

RootNode::~RootNode() {
}

void RootNode::addNode(std::unique_ptr<Node> &&node) {
    this->nodes.push_back(std::move(node));
}

const std::vector<std::unique_ptr<Node>> &RootNode::getNodes() {
    return this->nodes;
}
