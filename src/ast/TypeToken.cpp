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

#include <ast/TypeToken.h>
#include <core/TypeTemplate.h>

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
    return typePool->getTypeAndThrowIfUndefined(this->typeName);
}

const std::string &ClassTypeToken::getTokenText() {
    return this->typeName;
}


// ##############################
// ##     ReifiedTypeToken     ##
// ##############################

ReifiedTypeToken::ReifiedTypeToken(ClassTypeToken *templateTypeToken) :
        TypeToken(templateTypeToken->getLineNum()), templateTypeToken(templateTypeToken),
        elementTypeTokens() {
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
    TypeTemplate *typeTemplate = typePool->getTypeTemplate(this->templateTypeToken->getTokenText(), size);
    std::vector<DSType*> elementTypes(size);
    for(int i = 0; i < size; i++) {
        elementTypes[i] = this->elementTypeTokens[i]->toType(typePool);
    }
    return typePool->createAndGetReifiedTypeIfUndefined(typeTemplate, elementTypes);
}


// ###########################
// ##     FuncTypeToken     ##
// ###########################

TypeToken *FuncTypeToken::voidTypeToken = new ClassTypeToken(0, "Void");

FuncTypeToken::FuncTypeToken(int lineNum) :
        TypeToken(lineNum), returnTypeToken(), paramTypeTokens() {
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
        paramTypes[i] = this->paramTypeTokens[i]->toType(typePool);
    }
    return typePool->createAndGetFuncTypeIfUndefined(returnType, paramTypes);
}

