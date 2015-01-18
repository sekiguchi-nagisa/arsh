/*
 * TypeToken.cpp
 *
 *  Created on: 2015/01/13
 *      Author: skgchxngsxyz-osx
 */

#include "TypeToken.h"

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

DSType *ClassTypeToken::toType() {
    return 0;   //TODO:
}

// ##############################
// ##     ReifiedTypeToken     ##
// ##############################

ReifiedTypeToken::ReifiedTypeToken(TypeToken *templateType) :
        TypeToken(templateType->getLineNum()), templateTypeToken(templateType), elementTypeTokens(
                2) {
}

ReifiedTypeToken::~ReifiedTypeToken() {
    for(TypeToken *t : this->elementTypeTokens) {
        if(t == 0) {
            delete t;
        }
    }
}

void ReifiedTypeToken::addElementTypeToken(TypeToken *type) {
    this->elementTypeTokens.push_back(type);
}

DSType *ReifiedTypeToken::toType() {
    return 0;   //TODO:
}

// ###########################
// ##     FuncTypeToken     ##
// ###########################

TypeToken *FuncTypeToken::voidTypeToken = new ClassTypeToken(0, "Void");

FuncTypeToken::FuncTypeToken(int lineNum) :
        TypeToken(lineNum), returnTypeToken(), paramTypeTokens(2) {
}

FuncTypeToken::~FuncTypeToken() {
    if(this->returnTypeToken != 0) {
        delete this->returnTypeToken;
    }
    for(TypeToken *t : this->paramTypeTokens) {
        if(t != 0) {
            delete t;
        }
    }
}

void FuncTypeToken::setReturnTypeToken(TypeToken *type) {
    this->returnTypeToken = type;
}

void FuncTypeToken::addParamTypeToken(TypeToken *type) {
    this->paramTypeTokens.push_back(type);
}

//TODO: add TypePool to parameter
DSType *FuncTypeToken::toType() {
    return 0;
}

