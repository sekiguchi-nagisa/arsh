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

#include "TypeToken.h"

namespace ydsh {
namespace ast {

// #######################
// ##     TypeToken     ##
// #######################

TypeToken::TypeToken(unsigned int lineNum) :
        lineNum(lineNum) {
}

TypeToken::~TypeToken() {
}

unsigned int TypeToken::getLineNum() {
    return this->lineNum;
}


// ############################
// ##     ClassTypeToken     ##
// ############################

ClassTypeToken::ClassTypeToken(unsigned int lineNum, std::string &&typeName) :
        TypeToken(lineNum), typeName(std::move(typeName)) {
}

std::string ClassTypeToken::toTokenText() const {
    return this->typeName;
}

const std::string &ClassTypeToken::getTokenText() {
    return this->typeName;
}

void ClassTypeToken::accept(TypeTokenVisitor *visitor) {
    visitor->visitClassTypeToken(this);
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

ClassTypeToken *ReifiedTypeToken::getTemplate() {
    return this->templateTypeToken;
}

const std::vector<TypeToken *> &ReifiedTypeToken::getElementTypeTokens() {
    return this->elementTypeTokens;
}

std::string ReifiedTypeToken::toTokenText() const {
    std::string text(this->templateTypeToken->toTokenText());
    text += "<";
    unsigned int size = this->elementTypeTokens.size();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            text += ",";
        }
        text += this->elementTypeTokens[i]->toTokenText();
    }
    text += ">";
    return text;
}

void ReifiedTypeToken::accept(TypeTokenVisitor *visitor) {
    visitor->visitReifiedTypeToken(this);
}


// ###########################
// ##     FuncTypeToken     ##
// ###########################

FuncTypeToken::FuncTypeToken(TypeToken *returnTypeToken) :
        TypeToken(returnTypeToken->getLineNum()),
        returnTypeToken(returnTypeToken), paramTypeTokens() {
}

FuncTypeToken::~FuncTypeToken() {
    delete this->returnTypeToken;
    this->returnTypeToken = 0;

    for(TypeToken *t : this->paramTypeTokens) {
        delete t;
    }
    this->paramTypeTokens.clear();
}

void FuncTypeToken::addParamTypeToken(TypeToken *type) {
    this->paramTypeTokens.push_back(type);
}

const std::vector<TypeToken *> &FuncTypeToken::getParamTypeTokens() {
    return this->paramTypeTokens;
}

TypeToken *FuncTypeToken::getReturnTypeToken() {
    return this->returnTypeToken;
}

std::string FuncTypeToken::toTokenText() const {
    std::string text("Func<");
    text += this->returnTypeToken->toTokenText();
    unsigned int size = this->paramTypeTokens.size();
    for(unsigned int i = 0; i < size; i++) {
        if(i == 0) {
            text += ",[";
        } else {
            text += ",";
        }
        text += this->paramTypeTokens[i]->toTokenText();
        if(i == size - 1) {
            text += "]";
        }
    }
    text += ">";
    return text;
}

void FuncTypeToken::accept(TypeTokenVisitor *visitor) {
    visitor->visitFuncTypeToken(this);
}

// ################################
// ##     DBusInterfaceToken     ##
// ################################

DBusInterfaceToken::DBusInterfaceToken(unsigned int lineNum, std::string &&name) :
        TypeToken(lineNum), name(name) {
}

const std::string &DBusInterfaceToken::getTokenText() {
    return this->name;
}

std::string DBusInterfaceToken::toTokenText() const {
    return this->name;
}

void DBusInterfaceToken::accept(TypeTokenVisitor *visitor) {
    visitor->visitDBusInterfaceToken(this);
}


TypeToken *newAnyTypeToken(unsigned int lineNum) {
    return new ClassTypeToken(lineNum, std::string("Any"));
}

TypeToken *newVoidTypeToken(unsigned int lineNum) {
    return new ClassTypeToken(lineNum, std::string("Void"));
}

ReifiedTypeToken *newTupleTypeToken(TypeToken *typeToken) {
    ReifiedTypeToken *t = new ReifiedTypeToken(
            new ClassTypeToken(typeToken->getLineNum(), std::string("Tuple")));
    t->addElementTypeToken(typeToken);
    return t;
}

} // namespace ast
} // namespace ydsh