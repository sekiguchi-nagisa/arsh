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

ReifiedTypeToken::ReifiedTypeToken(std::unique_ptr<TypeToken> &&templateType) :
        TypeToken(templateType->getLineNum()), templateTypeToken(std::move(templateType)), elementTypeTokens(
                2) {
}

ReifiedTypeToken::~ReifiedTypeToken() {
}

void ReifiedTypeToken::addElementTypeToken(std::unique_ptr<TypeToken> &&type) {
    this->elementTypeTokens.push_back(std::move(type));
}

DSType *ReifiedTypeToken::toType() {
    return 0;   //TODO:
}

// ###########################
// ##     FuncTypeToken     ##
// ###########################

std::unique_ptr<TypeToken> FuncTypeToken::voidTypeToken = std::unique_ptr<TypeToken>(
        new ClassTypeToken(0, "Void"));

FuncTypeToken::FuncTypeToken(int lineNum) :
        TypeToken(lineNum), returnTypeToken(), paramTypeTokens(2) {
}

FuncTypeToken::~FuncTypeToken() {
}

void FuncTypeToken::setReturnTypeToken(std::unique_ptr<TypeToken> &&type) {
    this->returnTypeToken = std::move(type);
}

void FuncTypeToken::addParamTypeToken(std::unique_ptr<TypeToken> &&type) {
    this->paramTypeTokens.push_back(std::move(type));
}

//TODO: add TypePool to parameter
DSType *FuncTypeToken::toType() {
    return 0;
}

