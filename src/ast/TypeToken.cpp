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
#include "Node.h"

namespace ydsh {
namespace ast {

// ############################
// ##     ClassTypeToken     ##
// ############################

std::string ClassTypeToken::toTokenText() const {
    return this->typeName;
}

void ClassTypeToken::accept(TypeTokenVisitor *visitor) {
    visitor->visitClassTypeToken(this);
}


// ##############################
// ##     ReifiedTypeToken     ##
// ##############################

ReifiedTypeToken::~ReifiedTypeToken() {
    delete this->templateTypeToken;
    for(TypeToken *t : this->elementTypeTokens) {
        delete t;
    }
    this->elementTypeTokens.clear();
}

void ReifiedTypeToken::addElementTypeToken(TypeToken *type) {
    this->elementTypeTokens.push_back(type);
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

FuncTypeToken::~FuncTypeToken() {
    delete this->returnTypeToken;
    this->returnTypeToken = nullptr;

    for(TypeToken *t : this->paramTypeTokens) {
        delete t;
    }
    this->paramTypeTokens.clear();
}

void FuncTypeToken::addParamTypeToken(TypeToken *type) {
    this->paramTypeTokens.push_back(type);
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

std::string DBusInterfaceToken::toTokenText() const {
    return this->name;
}

void DBusInterfaceToken::accept(TypeTokenVisitor *visitor) {
    visitor->visitDBusInterfaceToken(this);
}

// #############################
// ##     ReturnTypeToken     ##
// #############################

ReturnTypeToken::ReturnTypeToken(TypeToken *token) :
        TypeToken(token->getLineNum()), typeTokens() {
    this->addTypeToken(token);
}

ReturnTypeToken::~ReturnTypeToken() {
    for(auto t : this->typeTokens) {
        delete t;
    }
    this->typeTokens.clear();
}

void ReturnTypeToken::addTypeToken(TypeToken *token) {
    this->typeTokens.push_back(token);
}

std::string ReturnTypeToken::toTokenText() const {
    std::string str;
    unsigned int count = 0;
    for(TypeToken *t : this->typeTokens) {
        if(count++ > 0) {
            str += ", ";
        }
        str += t->toTokenText();
    }
    return str;
}

void ReturnTypeToken::accept(TypeTokenVisitor *visitor) {
    visitor->visitReturnTypeToken(this);
}

// #########################
// ##     TypeOfToken     ##
// #########################

TypeOfToken::TypeOfToken(Node *exprNode) :
        TypeToken(exprNode->getLineNum()), exprNode(exprNode) { }

TypeOfToken::~TypeOfToken() {
    delete this->exprNode;
    this->exprNode = nullptr;
}

std::string TypeOfToken::toTokenText() const {
    return std::string("typeof(*)");
}

void TypeOfToken::accept(TypeTokenVisitor *visitor) {
    visitor->visitTypeOfToken(this);
}


TypeToken *newAnyTypeToken(unsigned int lineNum) {
    return new ClassTypeToken(lineNum, std::string("Any"));
}

TypeToken *newVoidTypeToken(unsigned int lineNum) {
    return new ClassTypeToken(lineNum, std::string("Void"));
}

} // namespace ast
} // namespace ydsh