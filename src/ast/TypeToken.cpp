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

TypeToken::TypeToken(int lineNum):
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
        TypeToken(templateType->getLineNum()), templateType(templateType), elementTypes(2) {
}

ReifiedTypeToken::~ReifiedTypeToken() {
    delete this->templateType;
    int size = this->elementTypes.size();
    for (int i = 0; i < size; i++) {
        delete this->elementTypes[i];
    }
    this->elementTypes.clear();
}

void ReifiedTypeToken::addElementType(TypeToken *type) {
    this->elementTypes.push_back(type);
}

DSType *ReifiedTypeToken::toType() {
    return 0;   //TODO:
}


// ###########################
// ##     FuncTypeToken     ##
// ###########################

ClassTypeToken *FuncTypeToken::unresolvedVoid =
        new ClassTypeToken(0, "Void");

FuncTypeToken::FuncTypeToken(int lineNum) :
        TypeToken(lineNum), returnType(0), paramTypes(2) {
}

FuncTypeToken::~FuncTypeToken() {
    if (this->returnType != 0) {
        delete this->returnType;
    }
    int size = this->paramTypes.size();
    for (int i = 0; i < size; i++) {
        delete this->paramTypes[i];
    }
    this->paramTypes.clear();
}

void FuncTypeToken::setReturnType(TypeToken *type) {
    this->returnType = type;
}

void FuncTypeToken::addParamType(TypeToken *type) {
    this->paramTypes.push_back(type);
}

//TODO: add TypePool to parameter
DSType *FuncTypeToken::toType() {
    return 0;
}

