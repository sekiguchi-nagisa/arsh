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

#include <assert.h>
#include <vector>

#include <core/symbol.h>
#include <core/TypeLookupError.h>
#include <parser/TypeChecker.h>
#include <parser/TypeCheckError.h>

namespace ydsh {
namespace parser {

// ##############################
// ##     HandleOrFuncType     ##
// ##############################

HandleOrFuncType::HandleOrFuncType(FunctionHandle * handle) :
        hasHandle(true), handle(handle) {
}

HandleOrFuncType::HandleOrFuncType(FunctionType * funcType) :
        hasHandle(false), funcType(funcType) {
}

bool HandleOrFuncType::treatAsHandle() {
    return this->hasHandle;
}

FunctionHandle *HandleOrFuncType::getHandle() {
    return this->hasHandle ? this->handle : 0;
}

FunctionType *HandleOrFuncType::getFuncType() {
    return this->hasHandle ? 0 : this->funcType;
}

// #########################
// ##     TypeChecker     ##
// #########################

TypeChecker::TypeChecker(TypePool * typePool) :
        typePool(typePool), symbolTable(), curReturnType(0),
        loopContextStack(), finallyContextStack(), cmdContextStack() {
}

TypeChecker::~TypeChecker() {
    this->finallyContextStack.clear();
}

void TypeChecker::checkTypeRootNode(RootNode & rootNode) {
    rootNode.accept(this);
}

DSType *TypeChecker::resolveInterface(TypePool *typePool, InterfaceNode *node) {
    InterfaceType *type = typePool->createAndGetInterfaceTypeIfUndefined(node->getInterfaceName());

    // create field handle
    unsigned int fieldSize = node->getFieldDeclNodes().size();
    for(unsigned int i = 0; i < fieldSize; i++) {
        VarDeclNode *fieldDeclNode = node->getFieldDeclNodes()[i];
        DSType *fieldType = toType(typePool, node->getFieldTypeTokens()[i]);
        FieldHandle *handle = type->newFieldHandle(
                fieldDeclNode->getVarName(), fieldType, fieldDeclNode->isReadOnly());
        if(handle == 0) {
            E_DefinedField(fieldDeclNode, fieldDeclNode->getVarName());
        }
    }

    // create method handle
    for(FunctionNode *funcNode : node->getMethodDeclNodes()) {
        InterfaceMethodHandle *handle = type->newMethodHandle(funcNode->getFuncName());
        handle->setRecvType(type);
        handle->setReturnType(toType(typePool, funcNode->getReturnTypeToken()));

        unsigned int paramSize = funcNode->getParamNodes().size();
        for(unsigned int i = 0; i < paramSize; i++) {
            handle->addParamType(toType(typePool, funcNode->getParamTypeTokens()[i]));
        }
    }

    node->setType(typePool->getVoidType());

    return type;
}

// type check entry point

DSType *TypeChecker::checkTypeAsStatement(Node * targetNode) {
    return this->checkType(0, targetNode, 0);
}

DSType *TypeChecker::checkType(Node * targetNode) {
    return this->checkType(0, targetNode, this->typePool->getVoidType());
}

DSType *TypeChecker::checkType(DSType * requiredType, Node * targetNode) {
    return this->checkType(requiredType, targetNode, 0);
}

DSType *TypeChecker::checkType(DSType *requiredType, Node *targetNode,
                               DSType *unacceptableType, bool allowCoercion) {
    /**
     * if target node is expr node and type is null,
     * try type check.
     */
    if(targetNode->getType() == 0) {
        targetNode->accept(this);
    }

    /**
     * after type checking, if type is still null,
     * throw exception.
     */
    DSType *type = targetNode->getType();
    if(type == 0) {
        E_Unresolved(targetNode);
    }

    /**
     * do not try type matching.
     */
    if(requiredType == 0) {
        if(unacceptableType != 0 && unacceptableType->isAssignableFrom(type)) {
            E_Unacceptable(targetNode, this->typePool->getTypeName(*type));
        }
        return type;
    }

    /**
     * try type matching.
     */
    if(requiredType->isAssignableFrom(type)) {
        return type;
    }

    /**
     * check coercion
     */
    if(allowCoercion && this->supportCoercion(requiredType, type)) {
        return type;
    }

    E_Required(targetNode, this->typePool->getTypeName(*requiredType),
               this->typePool->getTypeName(*type));
    return 0;
}

Node *TypeChecker::checkTypeAndResolveCoercion(DSType * requiredType, Node * targetNode) {
    TypePool *pool = this->typePool;
    DSType *type = this->checkType(requiredType, targetNode, pool->getVoidType(), true);
    if(this->supportCoercion(requiredType, type)) {
        if(*requiredType == *pool->getFloatType() && *type == *pool->getIntType()) {
            // int to float
            return this->intToFloat(targetNode);
        }
    }
    return targetNode;
}

bool TypeChecker::supportCoercion(DSType * requiredType, DSType * targetType) {
    // int to float cast
    if(*requiredType == *this->typePool->getFloatType() &&
       *targetType == *this->typePool->getIntType()) {
        return true;
    }
    return false;
}

CastNode *TypeChecker::intToFloat(Node * targetNode) {
    assert(targetNode->getType() != 0);
    assert(*targetNode->getType() == *this->typePool->getIntType());

    CastNode *castNode = new CastNode(targetNode, 0);
    castNode->setOpKind(CastNode::INT_TO_FLOAT);
    castNode->setType(this->typePool->getFloatType());
    return castNode;
}

void TypeChecker::checkTypeWithNewBlockScope(BlockNode * blockNode) {
    this->symbolTable.enterScope();
    this->checkTypeWithCurrentBlockScope(blockNode);
    this->symbolTable.exitScope();
}

void TypeChecker::checkTypeWithCurrentBlockScope(BlockNode * blockNode) {
    blockNode->accept(this);
}

FieldHandle *TypeChecker::addEntryAndThrowIfDefined(Node *node, const std::string &symbolName, DSType *type,
                                                    bool readOnly) {
    FieldHandle *handle = this->symbolTable.registerHandle(symbolName, type, readOnly);
    if(handle == 0) {
        E_DefinedSymbol(node, symbolName);
    }
    return handle;
}

void TypeChecker::enterLoop() {
    this->loopContextStack.push_back(true);
}

void TypeChecker::exitLoop() {
    this->loopContextStack.pop_back();
}

void TypeChecker::checkAndThrowIfOutOfLoop(Node * node) {
    if(!this->loopContextStack.empty() && this->loopContextStack.back()) {
        return;
    }
    E_InsideLoop(node);
}

bool TypeChecker::findBlockEnd(BlockNode * blockNode) {
    if(blockNode->getNodeList().size() == 0) {
        return false;
    }
    Node *endNode = blockNode->getNodeList().back();
    if(endNode->isBlockEndNode()) {
        return true;
    }

    /**
     * if endNode is IfNode, search recursively
     */
    IfNode *ifNode = dynamic_cast<IfNode *>(endNode);
    if(ifNode != 0) {
        if(!this->findBlockEnd(ifNode->getThenNode())) {
            return false;
        }
        for(BlockNode *elifThenNode : ifNode->getElifThenNodes()) {
            if(!this->findBlockEnd(elifThenNode)) {
                return false;
            }
        }
        return this->findBlockEnd(ifNode->getElseNode());
    }
    return false;
}

void TypeChecker::checkBlockEndExistence(BlockNode * blockNode, DSType * returnType) {
    Node *endNode = blockNode->getNodeList().back();

    if(*returnType == *this->typePool->getVoidType() && !endNode->isBlockEndNode()) {
        /**
         * insert return node to block end
         */
        blockNode->addNode(new ReturnNode(0, new EmptyNode()));
        return;
    }
    if(!this->findBlockEnd(blockNode)) {
        E_UnfoundReturn(blockNode);
    }
}

void TypeChecker::pushReturnType(DSType * returnType) {
    this->curReturnType = returnType;
}

DSType *TypeChecker::popReturnType() {
    DSType *returnType = this->curReturnType;
    this->curReturnType = 0;
    return returnType;
}

DSType *TypeChecker::getCurrentReturnType() {
    return this->curReturnType;
}

void TypeChecker::checkAndThrowIfInsideFinally(BlockEndNode * node) {
    if(!this->finallyContextStack.empty() && this->finallyContextStack.back()) {
        E_InsideFinally(node);
    }
}

DSType *TypeChecker::toType(TypePool *typePool, TypeToken * typeToken) {
    try {
        DSType *type = typeToken->toType(typePool);
        return type;
    } catch(TypeLookupError &e) {
        unsigned int lineNum = typeToken->getLineNum();
        throw TypeCheckError(lineNum, e);
    }
}

// for ApplyNode type checking
HandleOrFuncType TypeChecker::resolveCallee(Node * recvNode) {
    VarNode *varNode = dynamic_cast<VarNode *>(recvNode);
    if(varNode != 0) {
        return this->resolveCallee(varNode);
    }

    FunctionType *funcType =
            dynamic_cast<FunctionType *>(this->checkType(this->typePool->getBaseFuncType(), recvNode));
    return HandleOrFuncType(funcType);
}

HandleOrFuncType TypeChecker::resolveCallee(VarNode * recvNode) {
    FieldHandle *handle = this->symbolTable.lookupHandle(recvNode->getVarName());
    if(handle == 0) {
        E_UndefinedSymbol(recvNode, recvNode->getVarName());
    }
    recvNode->setAttribute(handle);

    FunctionHandle *funcHandle = dynamic_cast<FunctionHandle *>(handle);
    if(funcHandle != 0) {
        return HandleOrFuncType(funcHandle);
    }

    DSType *type = handle->getFieldType(this->typePool);
    FunctionType *funcType = dynamic_cast<FunctionType *>(type);
    if(funcType == 0) {
        E_Required(recvNode, this->typePool->getTypeName(*this->typePool->getBaseFuncType()),
                   this->typePool->getTypeName(*type));
    }
    return HandleOrFuncType(funcType);
}

void TypeChecker::checkTypeArgsNode(FunctionHandle *handle, ArgsNode *argsNode, bool isFuncCall) {
    const std::vector<DSType *> &paramTypes = handle->getParamTypes();
    this->checkTypeArgsNode(paramTypes, argsNode, isFuncCall);
}

void TypeChecker::checkTypeArgsNode(FunctionType *funcType, ArgsNode *argsNode, bool isFuncCall) {
    this->checkTypeArgsNode(funcType->getParamTypes(), argsNode, isFuncCall);
}

void TypeChecker::checkTypeArgsNode(const std::vector<DSType *> &paramTypes, ArgsNode *argsNode, bool isFuncCall) {
    const unsigned int startIndex = isFuncCall ? 0 : 1;
    unsigned int size = paramTypes.size();
    unsigned int argSize = argsNode->getNodes().size();
    // check param size
    if(size - startIndex != argSize) {
        E_UnmatchParam(argsNode,
                       std::to_string(size - startIndex),
                       std::to_string(argSize));
    }

    // check type each node
    for(unsigned int i = startIndex; i < size; i++) {
        argsNode->setArg(i - startIndex,
                         this->checkTypeAndResolveCoercion(paramTypes[i], argsNode->getNodes()[i - startIndex]));
    }
}

void TypeChecker::checkTypeArgsNode(MethodHandle *handle, ArgsNode *argsNode) { //FIXME: method overloading
    unsigned int argSize = argsNode->getNodes().size();
    do {
        // check param size
        unsigned int paramSize = handle->getParamTypes().size();
        if(paramSize != argSize) {
            E_UnmatchParam(argsNode,
                           std::to_string(paramSize),
                           std::to_string(argSize));
        }

        // check type each node
        for(unsigned int i = 0; i < paramSize; i++) {
            argsNode->setArg(i, this->checkTypeAndResolveCoercion(
                    handle->getParamTypes()[i], argsNode->getNodes()[i]));
        }
    } while(handle->getNext() != 0);
}

void TypeChecker::recover() {
    this->symbolTable.popAllLocal();
    this->symbolTable.removeCachedEntry();
    this->typePool->removeCachedType();

    this->curReturnType = 0;
    this->loopContextStack.clear();
    this->finallyContextStack.clear();
    this->cmdContextStack.clear();
}

// visitor api
void TypeChecker::visitDefault(Node *node) {
    E_Unimplemented(node, "unimplemented");
}

void TypeChecker::visitIntValueNode(IntValueNode * node) {
    DSType *type = this->typePool->getIntType();
    switch(node->getKind()) {
    case IntValueNode::BYTE:
        type = this->typePool->getByteType();
        break;
    case IntValueNode::INT16:
        type = this->typePool->getInt16Type();
        break;
    case IntValueNode::UINT16:
        type = this->typePool->getUint16Type();
        break;
    case IntValueNode::INT32:
        type = this->typePool->getInt32Type();
        break;
    case IntValueNode::UINT32:
        type = this->typePool->getUint32Type();
        break;
    }
    node->setType(type);
}

void TypeChecker::visitLongValueNode(LongValueNode *node) {
    DSType *type = this->typePool->getInt64Type();
    if(node->isUnsignedValue()) {
        type = this->typePool->getUint64Type();
    }
    node->setType(type);
}

void TypeChecker::visitFloatValueNode(FloatValueNode * node) {
    DSType *type = this->typePool->getFloatType();
    node->setType(type);
}

void TypeChecker::visitStringValueNode(StringValueNode * node) {
    DSType *type = this->typePool->getStringType();
    node->setType(type);
}

void TypeChecker::visitObjectPathNode(ObjectPathNode *node) {
    DSType *type = this->typePool->getObjectPathType();
    node->setType(type);
}

void TypeChecker::visitStringExprNode(StringExprNode * node) {
    for(Node *exprNode : node->getExprNodes()) {
        this->checkType(exprNode);
    }
    node->setType(this->typePool->getStringType());
}

void TypeChecker::visitArrayNode(ArrayNode * node) {
    unsigned int size = node->getExprNodes().size();
    assert(size != 0);
    Node *firstElementNode = node->getExprNodes()[0];
    DSType *elementType = this->checkType(firstElementNode);

    for(unsigned int i = 1; i < size; i++) {
        node->setExprNode(i, this->checkTypeAndResolveCoercion(elementType,
                                                               node->getExprNodes()[i]));
    }

    TypeTemplate *arrayTemplate = this->typePool->getArrayTemplate();
    std::vector<DSType *> elementTypes(1);
    elementTypes[0] = elementType;
    node->setType(this->typePool->createAndGetReifiedTypeIfUndefined(arrayTemplate, elementTypes));
}

void TypeChecker::visitMapNode(MapNode * node) {
    unsigned int size = node->getValueNodes().size();
    assert(size != 0);
    Node *firstKeyNode = node->getKeyNodes()[0];
    DSType *keyType = this->checkType(this->typePool->getValueType(), firstKeyNode);
    Node *firstValueNode = node->getValueNodes()[0];
    DSType *valueType = this->checkType(firstValueNode);

    for(unsigned int i = 1; i < size; i++) {
        node->setKeyNode(
                i, this->checkTypeAndResolveCoercion(keyType, node->getKeyNodes()[i]));
        node->setValueNode(
                i, this->checkTypeAndResolveCoercion(valueType, node->getValueNodes()[i]));
    }

    TypeTemplate *mapTemplate = this->typePool->getMapTemplate();
    std::vector<DSType *> elementTypes(2);
    elementTypes[0] = keyType;
    elementTypes[1] = valueType;
    node->setType(this->typePool->createAndGetReifiedTypeIfUndefined(mapTemplate, elementTypes));
}

void TypeChecker::visitTupleNode(TupleNode * node) {
    unsigned int size = node->getNodes().size();
    std::vector<DSType *> types(size);
    for(unsigned int i = 0; i < size; i++) {
        types[i] = this->checkType(node->getNodes()[i]);
    }
    node->setType(this->typePool->createAndGetTupleTypeIfUndefined(types));
}

void TypeChecker::visitVarNode(VarNode * node) {
    FieldHandle *handle = this->symbolTable.lookupHandle(node->getVarName());
    if(handle == 0) {
        E_UndefinedSymbol(node, node->getVarName());
    }

    node->setAttribute(handle);
    node->setType(handle->getFieldType(this->typePool));
}

void TypeChecker::visitAccessNode(AccessNode * node) {
    DSType *recvType = this->checkType(node->getRecvNode());
    FieldHandle *handle = recvType->lookupFieldHandle(this->typePool, node->getFieldName());
    if(handle == 0) {
        E_UndefinedField(node, node->getFieldName());
    }

    node->setAttribute(handle);
    node->setType(handle->getFieldType(this->typePool));
}

void TypeChecker::visitCastNode(CastNode * node) {
    DSType *exprType = this->checkType(node->getExprNode());
    DSType *targetType = toType(this->typePool, node->getTargetTypeToken());
    node->setType(targetType);

    // resolve cast op
    TypePool *pool = this->typePool;

    /**
     * nop
     */
    if(targetType->isAssignableFrom(exprType)) {
        return;
    }

    /**
     * int to float
     */
    if(*exprType == *pool->getIntType() && *targetType == *pool->getFloatType()) {
        node->setOpKind(CastNode::INT_TO_FLOAT);
        return;
    }

    /**
     * float to int
     */
    if(*exprType == *pool->getFloatType() && *targetType == *pool->getIntType()) {
        node->setOpKind(CastNode::FLOAT_TO_INT);
        return;
    }

    /**
     * to string
     */
    if(*targetType == *pool->getStringType()) {
        node->setOpKind(CastNode::TO_STRING);
        return;
    }

    /**
     * check cast
     */
    if(exprType->isAssignableFrom(targetType)) {
        node->setOpKind(CastNode::CHECK_CAST);
        return;
    }

    E_CastOp(node, this->typePool->getTypeName(*exprType), this->typePool->getTypeName(*targetType));
}

void TypeChecker::visitInstanceOfNode(InstanceOfNode * node) {
    DSType *exprType = this->checkType(node->getTargetNode());
    DSType *targetType = toType(this->typePool, node->getTargetTypeToken());
    node->setTargetType(targetType);


    if(targetType->isAssignableFrom(exprType)) {
        node->setOpKind(InstanceOfNode::ALWAYS_TRUE);
    } else if(exprType->isAssignableFrom(targetType)) {
        node->setOpKind(InstanceOfNode::INSTANCEOF);
    } else {
        node->setOpKind(InstanceOfNode::ALWAYS_FALSE);
    }
    node->setType(this->typePool->getBooleanType());
}

void TypeChecker::visitBinaryOpNode(BinaryOpNode * node) {
    DSType *leftType = this->checkType(node->getLeftNode());
    DSType *rightType = this->checkType(node->getRightNode());

    if(this->supportCoercion(rightType, leftType)) {    // cast leftNode.
        node->setLeftNode(this->intToFloat(node->getLeftNode()));
    }
    MethodCallNode *applyNode = node->creatApplyNode();
    node->setType(this->checkType(applyNode));
}

void TypeChecker::visitArgsNode(ArgsNode * node) {
    // not call it
}

void TypeChecker::visitApplyNode(ApplyNode * node) {
    /**
     * resolve handle
     */
    Node *exprNode = node->getExprNode();
    HandleOrFuncType hf = this->resolveCallee(exprNode);

    /**
     * check type arg nodes
     */
    if(hf.treatAsHandle()) {
        this->checkTypeArgsNode(hf.getHandle(), node->getArgsNode(), true);
        node->setType(hf.getHandle()->getReturnType());
    } else {
        this->checkTypeArgsNode(hf.getFuncType(), node->getArgsNode(), true);
        node->setType(hf.getFuncType()->getReturnType());
    }
}

void TypeChecker::visitMethodCallNode(MethodCallNode *node) {
    DSType *recvType = this->checkType(node->getRecvNode());
    MethodHandle *handle = recvType->lookupMethodHandle(this->typePool, node->getMethodName());
    if(handle == 0) {
        E_UndefinedMethod(node, node->getMethodName());
    }

    // check type argument
    this->checkTypeArgsNode(handle, node->getArgsNode());

    node->setMethodIndex(handle->getMethodIndex());
    node->setType(handle->getReturnType());
}

void TypeChecker::visitNewNode(NewNode * node) {
    DSType *type = toType(this->typePool, node->getTargetTypeToken());
    MethodHandle *handle = type->getConstructorHandle(this->typePool);
    if(handle == 0) {
        E_UndefinedInit(node, this->typePool->getTypeName(*type));
    }

    this->checkTypeArgsNode(handle, node->getArgsNode());
    node->setType(type);
}

void TypeChecker::visitGroupNode(GroupNode *node) {
    DSType *type = this->checkType(node->getExprNode());
    node->setType(type);
}

void TypeChecker::visitCondOpNode(CondOpNode * node) {
    DSType *booleanType = this->typePool->getBooleanType();
    this->checkType(booleanType, node->getLeftNode());
    this->checkType(booleanType, node->getRightNode());
    node->setType(booleanType);
}

void TypeChecker::visitCmdNode(CmdNode * node) {
    for(CmdArgNode *argNode : node->getArgNodes()) {
        this->checkType(argNode);
    }
    // check type redirect options
    for(const std::pair<int, Node *> &optionPair : node->getRedirOptions()) {
        this->checkTypeAsStatement(optionPair.second);
    }
    node->setType(this->typePool->getVoidType());   // FIXME
}

void TypeChecker::visitCmdArgNode(CmdArgNode * node) {
    for(Node *exprNode : node->getSegmentNodes()) {
        this->checkType(exprNode);
    }
    node->setType(this->typePool->getAnyType());   //FIXME
}

void TypeChecker::visitPipedCmdNode(PipedCmdNode * node) {
    for(CmdNode *procNode : node->getCmdNodes()) {
        this->checkTypeAsStatement(procNode);   // always void
    }
    if(node->treatAsBool() || this->cmdContextStack.back()->hasAttribute(CmdContextNode::CONDITION)) {
        node->setType(this->typePool->getBooleanType());
    } else {
        node->setType(this->typePool->getVoidType());
    }
}

void TypeChecker::visitCmdContextNode(CmdContextNode * node) {   //TODO: attribute
    // check type condNode
    this->cmdContextStack.push_back(node);
    this->checkTypeAsStatement(node->getExprNode());
    this->cmdContextStack.pop_back();

    DSType *type = this->typePool->getVoidType();

    if(node->hasAttribute(CmdContextNode::STR_CAP)) {
        type = this->typePool->getStringType();
    } else if(node->hasAttribute(CmdContextNode::ARRAY_CAP)) {
        type = this->typePool->getStringArrayType();
    } else if(node->hasAttribute(CmdContextNode::CONDITION)) {
        type = this->typePool->getBooleanType();
    }
    node->setType(type);
}

void TypeChecker::visitAssertNode(AssertNode * node) {
    this->checkType(this->typePool->getBooleanType(), node->getCondNode());
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitBlockNode(BlockNode * node) {
    int count = 0;
    int size = node->getNodeList().size();
    for(Node *targetNode : node->getNodeList()) {
        this->checkTypeAsStatement(targetNode);
        if(targetNode->isBlockEndNode() && (count != size - 1)) {
            E_Unreachable(node);
        }
        count++;
    }
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitBreakNode(BreakNode * node) {
    this->checkAndThrowIfInsideFinally(node);
    this->checkAndThrowIfOutOfLoop(node);
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitContinueNode(ContinueNode * node) {
    this->checkAndThrowIfInsideFinally(node);
    this->checkAndThrowIfOutOfLoop(node);
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitExportEnvNode(ExportEnvNode * node) {
    DSType *stringType = this->typePool->getStringType();
    FieldHandle *handle =
            this->addEntryAndThrowIfDefined(node, node->getEnvName(), stringType, false);
    handle->setAttribute(FieldHandle::ENV);

    // add env to type pool
    this->typePool->addEnv(node->getEnvName());

    node->setAttribute(handle);
    this->checkType(stringType, node->getExprNode());
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitImportEnvNode(ImportEnvNode * node) {
    // check env existence
    if(!this->typePool->hasEnv(node->getEnvName())) {
        E_UndefinedEnv(node, node->getEnvName());
    }

    DSType *stringType = this->typePool->getStringType();
    FieldHandle *handle =
            this->addEntryAndThrowIfDefined(node, node->getEnvName(), stringType, false);
    handle->setAttribute(FieldHandle::ENV);

    // add env to type pool
    this->typePool->addEnv(node->getEnvName());

    node->setAttribute(handle);
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitTypeAliasNode(TypeAliasNode *node) {
    TypeToken *typeToken = node->getTargetTypeToken();
    try {
        this->typePool->setAlias(node->getAlias(), toType(this->typePool, typeToken));
        node->setType(this->typePool->getVoidType());
    } catch(TypeLookupError &e) {
        unsigned int lineNum = typeToken->getLineNum();
        throw TypeCheckError(lineNum, e);
    }
}

void TypeChecker::visitForNode(ForNode * node) {
    this->symbolTable.enterScope();
    this->checkTypeAsStatement(node->getInitNode());
    this->checkType(this->typePool->getBooleanType(), node->getCondNode());
    this->checkTypeAsStatement(node->getIterNode());
    this->enterLoop();
    this->checkTypeWithCurrentBlockScope(node->getBlockNode());
    this->exitLoop();
    this->symbolTable.exitScope();
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitWhileNode(WhileNode * node) {
    this->checkType(this->typePool->getBooleanType(), node->getCondNode());
    this->enterLoop();
    this->checkTypeWithNewBlockScope(node->getBlockNode());
    this->exitLoop();
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitDoWhileNode(DoWhileNode * node) {
    this->enterLoop();
    this->symbolTable.enterScope();
    this->checkTypeWithCurrentBlockScope(node->getBlockNode());

    /**
     * block node and cond node is same scope.
     */
    this->checkType(this->typePool->getBooleanType(), node->getCondNode());

    this->symbolTable.exitScope();
    this->exitLoop();
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitIfNode(IfNode * node) {
    this->checkType(this->typePool->getBooleanType(), node->getCondNode());
    this->checkTypeWithNewBlockScope(node->getThenNode());

    unsigned int size = node->getElifCondNodes().size();
    for(unsigned int i = 0; i < size; i++) {
        this->checkType(this->typePool->getBooleanType(), node->getElifCondNodes()[i]);
        this->checkTypeWithNewBlockScope(node->getElifThenNodes()[i]);
    }

    this->checkTypeWithNewBlockScope(node->getElseNode());
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitReturnNode(ReturnNode * node) {
    this->checkAndThrowIfInsideFinally(node);
    DSType *returnType = this->getCurrentReturnType();
    if(returnType == 0) {
        E_InsideFunc(node);
    }
    DSType *exprType = this->checkType(returnType, node->getExprNode());
    if(*exprType == *this->typePool->getVoidType()) {
        if(dynamic_cast<EmptyNode *>(node->getExprNode()) == 0) {
            E_NotNeedExpr(node);
        }
    }
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitThrowNode(ThrowNode * node) {
    this->checkAndThrowIfInsideFinally(node);
    this->checkType(node->getExprNode());
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitCatchNode(CatchNode * node) {
    DSType *exceptionType = toType(this->typePool, node->getTypeToken());
    node->setExceptionType(exceptionType);

    /**
     * check type catch block
     */
    this->symbolTable.enterScope();
    FieldHandle *handle = this->addEntryAndThrowIfDefined(node,
                                                          node->getExceptionName(), exceptionType, true);
    node->setAttribute(handle);
    this->checkTypeWithCurrentBlockScope(node->getBlockNode());
    this->symbolTable.exitScope();
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitTryNode(TryNode * node) {
    this->checkTypeWithNewBlockScope(node->getBlockNode());
    // check type catch block
    for(CatchNode *c : node->getCatchNodes()) {
        this->checkTypeAsStatement(c);
    }

    // check type finally block, may be empty node
    this->finallyContextStack.push_back(true);
    this->checkTypeWithNewBlockScope(node->getFinallyNode());
    this->finallyContextStack.pop_back();

    /**
     * verify catch block order
     */
    int size = node->getCatchNodes().size();
    for(int i = 0; i < size - 1; i++) {
        DSType *curType = node->getCatchNodes()[i]->getExceptionType();
        CatchNode *nextNode = node->getCatchNodes()[i + 1];
        DSType *nextType = nextNode->getExceptionType();
        if(curType->isAssignableFrom(nextType)) {
            E_Unreachable(nextNode);
        }
    }
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitVarDeclNode(VarDeclNode * node) {
    DSType *initValueType = this->checkType(node->getInitValueNode());
    FieldHandle *handle =
            this->addEntryAndThrowIfDefined(node, node->getVarName(), initValueType, node->isReadOnly());
    node->setAttribute(handle);
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitAssignNode(AssignNode * node) {
    AssignableNode *leftNode = dynamic_cast<AssignableNode *>(node->getLeftNode());
    if(leftNode == 0) {
        E_Assignable(node);
    }

    DSType *leftType = this->checkType(leftNode);
    if(leftNode->isReadOnly()) {
        E_ReadOnly(node);
    }

    if(dynamic_cast<AccessNode *>(leftNode) != 0) {
        node->setAttribute(AssignNode::FIELD_ASSIGN);
    }
    if(node->isSelfAssignment()) {
        BinaryOpNode *opNode = dynamic_cast<BinaryOpNode *>(node->getRightNode());
        opNode->getLeftNode()->setType(leftType);
        AccessNode *accessNode = dynamic_cast<AccessNode *>(leftNode);
        if(accessNode != 0) {
            accessNode->setAdditionalOp(AccessNode::DUP_RECV);
        }
    }
    node->setRightNode(this->checkTypeAndResolveCoercion(leftType, node->getRightNode()));
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitElementSelfAssignNode(ElementSelfAssignNode *node) {
    DSType *recvType = this->checkType(node->getRecvNode());
    DSType *indexType = this->checkType(node->getIndexNode());

    node->setRecvType(recvType);
    node->setIndexType(indexType);

    DSType *elementType = this->checkType(node->getGetterNode());
    node->getBinaryNode()->getLeftNode()->setType(elementType);
    this->checkType(node->getBinaryNode());
    node->getSetterNode()->getArgsNode()->getNodes()[1]->setType(elementType);
    this->checkTypeAsStatement(node->getSetterNode());

    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitFunctionNode(FunctionNode * node) {   //TODO: named parameter
    // resolve return type, param type
    DSType *returnType = toType(this->typePool, node->getReturnTypeToken());
    unsigned int paramSize = node->getParamTypeTokens().size();
    std::vector<DSType *> paramTypes(paramSize);
    for(unsigned int i = 0; i < paramSize; i++) {
        paramTypes[i] = toType(this->typePool, node->getParamTypeTokens()[i]);
    }

    // register function handle
    FunctionHandle *handle =
            this->symbolTable.registerFuncHandle(node->getFuncName(), returnType, paramTypes);
    if(handle == 0) {
        E_DefinedSymbol(node, node->getFuncName());
    }
    node->setVarIndex(handle->getFieldIndex());

    // check type func body
    this->pushReturnType(returnType);
    this->symbolTable.enterFunc();
    this->symbolTable.enterScope();

    for(unsigned int i = 0; i < paramSize; i++) { // register parameter
        VarNode *paramNode = node->getParamNodes()[i];
        FieldHandle *handle = this->addEntryAndThrowIfDefined(
                paramNode, paramNode->getVarName(), paramTypes[i], false);
        paramNode->setAttribute(handle);
    }
    this->checkBlockEndExistence(node->getBlockNode(), returnType);
    this->checkTypeWithCurrentBlockScope(node->getBlockNode());
    this->symbolTable.exitScope();

    node->setMaxVarNum(this->symbolTable.getMaxVarIndex());
    this->symbolTable.exitFunc();
    this->popReturnType();
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitInterfaceNode(InterfaceNode *node) {
    resolveInterface(this->typePool, node);
}

void TypeChecker::visitBindVarNode(BindVarNode *node) {
    DSType *valueType = node->getValue()->getType();
    FieldHandle *handle =
            this->addEntryAndThrowIfDefined(node, node->getVarName(), valueType, true);
    node->setAttribute(handle);
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitEmptyNode(EmptyNode * node) {
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitDummyNode(DummyNode * node) {
    // do nothing.
}

void TypeChecker::visitRootNode(RootNode * node) {
    this->symbolTable.clearEntryCache();
    this->typePool->clearTypeCache();

    for(Node *targetNode : node->getNodeList()) {
        this->checkTypeAsStatement(targetNode);
    }
    node->setMaxVarNum(this->symbolTable.getMaxVarIndex());
    node->setMaxGVarNum(this->symbolTable.getMaxGVarIndex());
    node->setType(this->typePool->getVoidType());
}

} // namespace parser
} // namespace ydsh
