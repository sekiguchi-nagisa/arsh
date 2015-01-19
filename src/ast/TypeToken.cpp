/*
 * TypeToken.cpp
 *
 *  Created on: 2015/01/13
 *      Author: skgchxngsxyz-osx
 */

#include "TypeToken.h"
#include "../parser/TypeCheckException.h"

// #######################
// ##     TypeToken     ##
// #######################

TypeToken::TypeToken(int lineNum) :
        lineNum(lineNum) {
}

TypeToken::~TypeToken() {
}

int TypeToken::getLineNum() {
    return this->lineNum;
}


// ############################
// ##     ClassTypeToken     ##
// ############################

ClassTypeToken::ClassTypeToken(int lineNum, std::string &&typeName) :
        TypeToken(lineNum), typeName(std::move(typeName)) {
}

DSType *ClassTypeToken::toType(TypePool *typePool) {
    try {
        return typePool->getTypeAndThrowIfUndefined(this->typeName);
    } catch(TypeLookupException &e) {
        e.setLineNum(this->getLineNum());
        throw;
    }
}

const std::string &ClassTypeToken::getTokenText() {
    return this->typeName;
}


// ##############################
// ##     ReifiedTypeToken     ##
// ##############################

ReifiedTypeToken::ReifiedTypeToken(ClassTypeToken *templateTypeToken) :
        TypeToken(templateTypeToken->getLineNum()), templateTypeToken(templateTypeToken),
        elementTypeTokens(2) {
}

ReifiedTypeToken::~ReifiedTypeToken() {
    for(TypeToken *t : this->elementTypeTokens) {
        delete t;
    }
    this->elementTypeTokens.clear();
}

void ReifiedTypeToken::addElementTypeToken(TypeToken *type) {
    this->elementTypeTokens.push_back(type);
}

DSType *ReifiedTypeToken::toType(TypePool *typePool) {
    int size = this->elementTypeTokens.size();
    DSType *templateType = typePool->getTemplateType(this->templateTypeToken->getTokenText(), size);    //FIXME: template type
    std::vector<DSType*> elementTypes(size);
    for(int i = 0; i < size; i++) {
        elementTypes.push_back(this->elementTypeTokens[i]->toType(typePool));
    }

    try {
        return typePool->createAndGetReifiedTypeIfUndefined(templateType, elementTypes);
    } catch(TypeLookupException &e) {
        e.setLineNum(this->getLineNum());
        throw;
    }
}


// ###########################
// ##     FuncTypeToken     ##
// ###########################

TypeToken *FuncTypeToken::voidTypeToken = new ClassTypeToken(0, "Void");

FuncTypeToken::FuncTypeToken(int lineNum) :
        TypeToken(lineNum), returnTypeToken(), paramTypeTokens(2) {
}

FuncTypeToken::~FuncTypeToken() {
    delete this->returnTypeToken;
    this->returnTypeToken = 0;

    for(TypeToken *t : this->paramTypeTokens) {
        delete t;
    }
    this->paramTypeTokens.clear();
}

void FuncTypeToken::setReturnTypeToken(TypeToken *type) {
    this->returnTypeToken = type;
}

void FuncTypeToken::addParamTypeToken(TypeToken *type) {
    this->paramTypeTokens.push_back(type);
}

DSType *FuncTypeToken::toType(TypePool *typePool) {
    DSType *returnType = this->returnTypeToken->toType(typePool);
    int size = this->paramTypeTokens.size();
    std::vector<DSType*> paramTypes(size);
    for(int i = 0; i < size; i++) {
        paramTypes.push_back(this->paramTypeTokens[i]->toType(typePool));
    }

    try {
        return typePool->createAndGetFuncTypeIfUndefined(returnType, paramTypes);
    } catch(TypeLookupException &e) {
        e.setLineNum(this->getLineNum());
        throw;
    }
    return 0;
}

