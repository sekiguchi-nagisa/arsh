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

Node::Node(int lineNum) : lineNum(lineNum) {
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

ExprNode::ExprNode(int lineNum, UnresolvedType *type):
		Node(lineNum), type(type) {
}

ExprNode::~ExprNode() {
	if(this->type != 0 && dynamic_cast<UnresolvedType*>(this->type) != 0) {
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

IntValueNode::IntValueNode(int lineNum, long value):
		ExprNode(lineNum, 0), value(value) {
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

FloatValueNode::FloatValueNode(int lineNum, double value):
		ExprNode(lineNum, 0), value(value) {
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

BooleanValueNode::BooleanValueNode(int lineNum, bool value):
		ExprNode(lineNum, 0), value(value) {
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

StringValueNode::StringValueNode(std::string value):
		ExprNode(0, 0), value(value) {
}

StringValueNode::StringValueNode(int lineNum, char *value, bool isSingleQuoteStr):	//TODO:
		ExprNode(lineNum, 0) {
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

StringExprNode::StringExprNode(int lineNum):
		ExprNode(lineNum, 0), nodes() {
}

StringExprNode::~StringExprNode() {
	int size = this->nodes.size();
	for(int i = 0; i < size; i++) {
		delete this->nodes[i];
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

ArrayNode::ArrayNode(int lineNum):
		ExprNode(lineNum, 0), nodes() {
}

ArrayNode::~ArrayNode() {
	int size = this->nodes.size();
	for(int i = 0; i < size; i++) {
		delete this->nodes[i];
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

MapNode::MapNode(int lineNum):
		ExprNode(lineNum, 0), keyNodes(), valueNodes() {
}

MapNode::~MapNode() {
	int size = this->keyNodes.size();
	for(int i = 0; i < size; i++) {
		delete this->keyNodes[i];
		delete this->valueNodes[i];
	}
	this->keyNodes.clear();
	this->valueNodes.clear();
}

void MapNode::addEntry(ExprNode *keyNode, ExprNode *valueNode) {
	this->keyNodes.push_back(keyNode);
	this->valueNodes.push_back(valueNode);
}

const std::vector<ExprNode*> &MapNode::getkeyNodes() {
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

PairNode::PairNode(int lineNum, ExprNode *leftNode, ExprNode *rightNode):
		ExprNode(lineNum, 0), leftNode(leftNode), rightNode(rightNode) {
}

PairNode::~PairNode() {
	delete this->leftNode;
	delete this->rightNode;
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

AssignableNode::AssignableNode(int lineNum):
		ExprNode(lineNum, 0) {
}

AssignableNode::~AssignableNode() {
}


// ########################
// ##     SymbolNode     ##
// ########################

VarNode::VarNode(int lineNum, std::string varName):
		AssignableNode(lineNum), varName(varName), readOnly(false) {
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

IndexNode::IndexNode(int lineNum, ExprNode *recvNode, ExprNode *indexNode):
		AssignableNode(lineNum), recvNode(recvNode), indexNode(indexNode),
		getterHandle(0), setterHandle(0) {
}

IndexNode::~IndexNode() {
	delete this->recvNode;
	delete this->indexNode;
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

AccessNode::AccessNode(int lineNum, ExprNode *recvNode, std::string fieldName):
		AssignableNode(lineNum), recvNode(recvNode), fieldName(fieldName), handle(0) {
}

AccessNode::~AccessNode() {
	delete this->recvNode;
}

ExprNode *AccessNode::getRecvNode() {
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

CastNode::CastNode(int lineNum, ExprNode *targetNode, UnresolvedType *type):
		ExprNode(lineNum, type), targetNode(targetNode) {
}

CastNode::~CastNode() {
	delete this->targetNode;
}

ExprNode *CastNode::getTargetNode() {
	return this->targetNode;
}

int CastNode::accept(NodeVisitor *visitor) {
	return visitor->visitCastNode(this);
}


// ############################
// ##     InstanceOfNode     ##
// ############################

InstanceOfNode::InstanceOfNode(int lineNum, ExprNode *targetNode, UnresolvedType *type):
		ExprNode(lineNum, type), targetNode(targetNode) {
}

InstanceOfNode::~InstanceOfNode() {
	delete this->targetNode;
}

ExprNode *InstanceOfNode::getTargetNode() {
	return this->targetNode;
}

int InstanceOfNode::accept(NodeVisitor *visitor) {
	return visitor->visitInstanceOfNode(this);
}


// #######################
// ##     ApplyNode     ##
// #######################

ApplyNode::ApplyNode(int lineNum, ExprNode *recvNode):
		ExprNode(lineNum, 0), recvNode(recvNode), argNodes() {
}

ApplyNode::~ApplyNode() {
	delete this->recvNode;

	int size = this->argNodes.size();
	for(int i = 0; i < size; i++) {
		delete this->argNodes[i];
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

int ApplyNode::accept(NodeVisitor *visitor) {
	return visitor->visitApplyNode(this);
}


// #################################
// ##     ConstructorCallNode     ##
// #################################

ConstructorCallNode::ConstructorCallNode(int lineNum, UnresolvedType *type):
		ExprNode(lineNum, type), argNodes(), handle(0) {
}

ConstructorCallNode::~ConstructorCallNode() {
	int size = this->argNodes.size();
	for(int i = 0; i < size; i++) {
		delete this->argNodes[i];
	}
	this->argNodes.clear();
}

void ConstructorCallNode::addArgNode(ExprNode *node) {
	this->argNodes.push_back(node);
}

const std::vector<ExprNode*> &ConstructorCallNode::getArgNodes() {
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

CondOpNode::CondOpNode(int lineNum, ExprNode *leftNode, ExprNode *rightNode, bool isAndOp):
		ExprNode(lineNum, 0), leftNode(leftNode), rightNode(rightNode), andOp(isAndOp) {
}

CondOpNode::~CondOpNode() {
	delete this->leftNode;
	delete this->rightNode;
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

ProcessNode::ProcessNode(int lineNum, std::string commandName):
		ExprNode(lineNum, 0), commandName(commandName), argNodes() {
}

ProcessNode::~ProcessNode() {
	int size = this->argNodes.size();
	for(int i = 0; i < size; i++) {
		delete this->argNodes[i];
	}
}

const std::string &ProcessNode::getCommandName() {
	return this->commandName;
}

void ProcessNode::addArgNode(ExprNode *node) {
	this->argNodes.push_back(node);
}

const std::vector<ExprNode*> &ProcessNode::getArgNodes() {
	return this->argNodes;
}

int ProcessNode::accept(NodeVisitor *visitor) {
	return visitor->visitProcessNode(this);
}


// ##########################
// ##     ArgumentNode     ##
// ##########################

ProcArgNode::ProcArgNode(int lineNum):
	ExprNode(lineNum, 0), segmentNodes() {
}

ProcArgNode::~ProcArgNode() {
	int size = this->segmentNodes.size();
	for(int i = 0; i < size; i++) {
		delete this->segmentNodes[i];
	}
	this->segmentNodes.clear();
}

void ProcArgNode::addSegmentNode(ExprNode *node) {
	ProcArgNode *argNode = dynamic_cast<ProcArgNode*>(node);
	if(argNode != 0) {
		std::vector<ExprNode*> segmentNodes = argNode->getSegmentNodes();
		int size = segmentNodes.size();
		for(int i = 0; i < size; i++) {
			this->segmentNodes.push_back(segmentNodes[i]);
		}
		segmentNodes.clear();
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

SpecialCharNode::SpecialCharNode(int lineNum):
		ExprNode(lineNum, 0) {
}

SpecialCharNode::~SpecialCharNode() {
}

int SpecialCharNode::accept(NodeVisitor *visitor) {
	return visitor->visitSpecialCharNode(this);
}


// ######################
// ##     TaskNode     ##
// ######################

TaskNode::TaskNode():
		ExprNode(0, 0), procNodes(), background(false) {
}

TaskNode::~TaskNode() {
	int size = this->procNodes.size();
	for(int i = 0; i < size; i++) {
		delete this->procNodes[i];
	}
	this->procNodes.clear();
}

void TaskNode::addProcNodes(ProcessNode* node) {
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

InnerTaskNode::InnerTaskNode(ExprNode *exprNode):
		ExprNode(0, 0), exprNode(exprNode) {
}

InnerTaskNode::~InnerTaskNode() {
	delete this->exprNode;
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

AssertNode::AssertNode(int lineNum, ExprNode *exprNode):
		Node(lineNum), exprNode(exprNode) {
}

AssertNode::~AssertNode() {
	delete this->exprNode;
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

BlockNode::BlockNode():
		Node(0), nodes() {
}

BlockNode::~BlockNode() {
	int size = this->nodes.size();
	for(int i = 0; i < size; i++) {
		delete this->nodes[i];
	}
	this->nodes.clear();
}

void BlockNode::addNode(Node *node) {
	this->nodes.push_back(node);
}

const std::vector<Node*> &BlockNode::getNodes() {
	return this->nodes;
}

int BlockNode::accept(NodeVisitor *visitor) {
	return visitor->visitBlockNode(this);
}


// ######################
// ##     BlockEnd     ##
// ######################

BlockEndNode::BlockEndNode(int lineNum):
		Node(lineNum) {
}


// #######################
// ##     BreakNode     ##
// #######################

BreakNode::BreakNode(int lineNum):
		BlockEndNode(lineNum) {
}

int BreakNode::accept(NodeVisitor *visitor) {
	return visitor->visitBreakNode(this);
}


// ##########################
// ##     ContinueNode     ##
// ##########################

ContinueNode::ContinueNode(int lineNum):
	BlockEndNode(lineNum) {
}

int ContinueNode::accept(NodeVisitor *visitor) {
	return visitor->visitContinueNode(this);
}


// ###########################
// ##     ExportEnvNode     ##
// ###########################

ExportEnvNode::ExportEnvNode(int lineNum, std::string envName, ExprNode *exprNode):
		Node(lineNum), envName(envName), exprNode(exprNode) {
}

ExportEnvNode::~ExportEnvNode() {
	delete this->exprNode;
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

ImportEnvNode::ImportEnvNode(int lineNum, std::string envName):
		Node(lineNum), envName(envName) {
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

LoopNode::LoopNode(int lineNum):
		Node(lineNum) {
}


// #####################
// ##     ForNode     ##
// #####################

ForNode::ForNode(int lineNum, Node *initNode, Node *condNode, Node *iterNode, BlockNode *blockNode):
		LoopNode(lineNum), initNode(initNode), condNode(condNode), iterNode(iterNode), blockNode(blockNode) {
}

ForNode::~ForNode() {
	delete this->initNode;
	delete this->condNode;
	delete this->iterNode;
	delete this->blockNode;
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
// ##     ForInNode     ##
// #######################

ForInNode::ForInNode(int lineNum, std::string initName, ExprNode *exprNode, BlockNode *blockNode):
		LoopNode(lineNum), initName(initName), exprNode(exprNode), blockNode(blockNode),
		resetHandle(0), nextHandle(0), hasNextHandle(0) {
}

ForInNode::~ForInNode() {
	delete this->exprNode;
	delete this->blockNode;
}

const std::string &ForInNode::getInitName() {
	return this->initName;
}

ExprNode *ForInNode::getExprNode() {
	return this->exprNode;
}

BlockNode *ForInNode::getBlockNode() {
	return this->blockNode;
}

void ForInNode::setIteratorHandle(FunctionHandle *resetHandle, FunctionHandle *nextHandle, FunctionHandle *hasNextHandle) {
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

WhileNode::WhileNode(int lineNum, ExprNode *condNode, BlockNode *blockNode, bool asDoWhile):
		LoopNode(lineNum), condNode(condNode), blockNode(blockNode), asDoWhile(asDoWhile){
}

WhileNode::~WhileNode() {
	delete this->condNode;
	delete this->blockNode;
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

IfNode::IfNode(int lineNum, ExprNode *condNode, BlockNode *thenNode, BlockNode *elseNode):
		Node(lineNum), condNode(condNode), thenNode(thenNode), elseNode(elseNode) {
}

IfNode::~IfNode() {
	delete this->condNode;
	delete this->thenNode;
	if(this->elseNode != 0) {
		delete this->elseNode;
	}
}

ExprNode *IfNode::getCondNode() {
	return this->condNode;
}

BlockNode *IfNode::getThenNode() {
	return this->thenNode;
}

BlockNode *IfNode::getElseNode() {
	if(this->elseNode != 0) {
		return this->elseNode;
	}
	return &EmptyBlockNode::emptyBlockNode;
}

int IfNode::accept(NodeVisitor *visitor) {
	return visitor->visitIfNode(this);
}


// ########################
// ##     ReturnNode     ##
// ########################

ReturnNode::ReturnNode(int lineNum, ExprNode *exprNode):
		BlockEndNode(lineNum), exprNode(exprNode) {
}

ReturnNode::ReturnNode(int lineNum):
		BlockEndNode(lineNum), exprNode(0) {
}

ReturnNode::~ReturnNode() {
	if(this->exprNode != 0) {
		delete this->exprNode;
	}
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

ThrowNode::ThrowNode(int lineNum, ExprNode *exprNode):
		BlockEndNode(lineNum), exprNode(exprNode) {
}

ThrowNode::~ThrowNode() {
	delete this->exprNode;
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

CatchNode::CatchNode(int lineNum, std::string exceptionName, UnresolvedType *type, BlockNode *blockNode):
		Node(lineNum), exceptionName(exceptionName), exceptionType(type), blockNode(blockNode) {
}

CatchNode::~CatchNode() {
	if(this->exceptionType != 0 && dynamic_cast<UnresolvedType*>(this->exceptionType) != 0) {
		delete this->exceptionType;
	}
	delete this->blockNode;
}

const std::string &CatchNode::getExceptionName() {
	return this->exceptionName;
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

TryNode::TryNode(int lineNum, BlockNode *blockNode):
		Node(lineNum), blockNode(blockNode), catchNodes(), finallyNode(0) {
}

TryNode::~TryNode() {
	delete this->blockNode;
	int size = this->catchNodes.size();
	for(int i = 0; i < size; i++) {
		delete this->catchNodes[i];
	}
	this->catchNodes.clear();

	if(this->finallyNode != 0) {
		delete this->finallyNode;
	}
}

void TryNode::addCatchNode(CatchNode *catchNode) {
	this->catchNodes.push_back(catchNode);
}

const std::vector<CatchNode*> &TryNode::getCatchNodes() {
	return this->catchNodes;
}

void TryNode::addFinallyNode(Node *finallyNode) {
	this->finallyNode = finallyNode;
}

Node *TryNode::getFinallyNode() {	//FIXME:
	return this->finallyNode;
}

int TryNode::accept(NodeVisitor *visitor) {
	return visitor->visitTryNode(this);
}


// #########################
// ##     FinallyNode     ##
// #########################

FinallyNode::FinallyNode(int lineNum, BlockNode *block):
		Node(lineNum), blockNode(block) {
}

FinallyNode::~FinallyNode() {
	delete this->blockNode;
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

VarDeclNode::VarDeclNode(int lineNum, std::string varName, ExprNode *initValueNode, bool readOnly):
		Node(lineNum), varName(varName), readOnly(readOnly), global(false), initValueNode(initValueNode) {
}

VarDeclNode::~VarDeclNode() {
	delete this->initValueNode;
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

AssignNode::AssignNode(int lineNum, ExprNode *leftNode, ExprNode *rightNode):
		ExprNode(lineNum, 0), leftNode(leftNode), rightNode(rightNode), handle(0) {
}

AssignNode::~AssignNode() {
	delete this->leftNode;
	delete this->rightNode;
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

FunctionNode::FunctionNode(int lineNum, std::string funcName):
		Node(lineNum), funcName(funcName), paramNodes(), returnType(0), blockNode(0) {
}

FunctionNode::~FunctionNode() {
	int size = this->paramNodes.size();
	for(int i = 0; i < size; i++) {
		delete this->paramNodes[i];
	}
	this->paramNodes.clear();

	if(this->returnType != 0 && dynamic_cast<UnresolvedType*>(this->returnType) != 0) {
		delete this->returnType;
	}

	if(this->blockNode != 0) {
		delete this->blockNode;
	}
}

const std::string &FunctionNode::getFuncName() {
	return this->funcName;
}

void FunctionNode::addParamNode(VarNode *node) {
	this->paramNodes.push_back(node);
}

const std::vector<VarNode*> &FunctionNode::getParamNodes() {
	return this->paramNodes;
}

void FunctionNode::setReturnType(UnresolvedType *returnType) {
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

EmptyNode::EmptyNode():
		ExprNode(0, 0) {
}

int EmptyNode::accept(NodeVisitor *visitor) {
	return visitor->visitEmptyNode(this);
}


// ############################
// ##     EmptyBlockNode     ##
// ############################

EmptyBlockNode::EmptyBlockNode():
		BlockNode() {
}

EmptyBlockNode::~EmptyBlockNode() {
}

void EmptyBlockNode::addNode(Node *node) {
}

int EmptyBlockNode::accept(NodeVisitor *visitor) {
	return visitor->visitEmptyBlockNode(this);
}

EmptyBlockNode EmptyBlockNode::emptyBlockNode = EmptyBlockNode();


// ######################
// ##     RootNode     ##
// ######################

RootNode::RootNode():
		nodes() {
}

RootNode::~RootNode() {
	int size = this->nodes.size();
	for(int i = 0; i < size; i++) {
		delete this->nodes[i];
	}
	this->nodes.clear();
}

void RootNode::addNode(Node *node) {
	this->nodes.push_back(node);
}

const std::vector<Node*> &RootNode::getNodes() {
	return this->nodes;
}
