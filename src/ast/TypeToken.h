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

#ifndef YDSH_TYPETOKEN_H
#define YDSH_TYPETOKEN_H

#include <string>
#include <vector>

namespace ydsh {
namespace ast {

struct TypeTokenVisitor;
class Node;

/**
 * represent for parsed type.
 */
class TypeToken {
private:
    unsigned int lineNum;

public:
    explicit TypeToken(unsigned int lineNum) : lineNum(lineNum) { }

    virtual ~TypeToken() = default;

    unsigned int getLineNum() const {
        return this->lineNum;
    }

    /**
     * get token text
     */
    virtual std::string toTokenText() const = 0;

    virtual void accept(TypeTokenVisitor *visitor) = 0;
};

class ClassTypeToken : public TypeToken {
private:
    std::string typeName;

public:
    ClassTypeToken(unsigned int lineNum, std::string &&typeName) :
            TypeToken(lineNum), typeName(std::move(typeName)) { }

    ~ClassTypeToken() = default;

    std::string toTokenText() const;  // override

    const std::string &getTokenText() const {
        return this->typeName;
    }

    void accept(TypeTokenVisitor *visitor); // override
};

/**
 * for reified type and tuple type
 */
class ReifiedTypeToken : public TypeToken {
private:
    ClassTypeToken *templateTypeToken;
    std::vector<TypeToken *> elementTypeTokens;

public:
    explicit ReifiedTypeToken(ClassTypeToken *templateTypeToken) :
            TypeToken(templateTypeToken->getLineNum()),
            templateTypeToken(templateTypeToken), elementTypeTokens() { }

    ~ReifiedTypeToken();

    void addElementTypeToken(TypeToken *type);

    ClassTypeToken *getTemplate() const {
        return this->templateTypeToken;
    }

    const std::vector<TypeToken *> &getElementTypeTokens() const {
        return this->elementTypeTokens;
    }

    std::string toTokenText() const;  // override
    void accept(TypeTokenVisitor *visitor); // override
};

class FuncTypeToken : public TypeToken {
private:
    /**
     * may be null, if has return type annotation (return void)
     */
    TypeToken *returnTypeToken;

    /**
     * may be empty vector, if has no parameter
     */
    std::vector<TypeToken *> paramTypeTokens;

public:
    explicit FuncTypeToken(TypeToken *returnTypeToken) :
            TypeToken(returnTypeToken->getLineNum()),
            returnTypeToken(returnTypeToken), paramTypeTokens() { }

    ~FuncTypeToken();

    void addParamTypeToken(TypeToken *type);

    const std::vector<TypeToken *> &getParamTypeTokens() const {
        return this->paramTypeTokens;
    }

    TypeToken *getReturnTypeToken() const {
        return this->returnTypeToken;
    }

    std::string toTokenText() const;  // override
    void accept(TypeTokenVisitor *visitor); // override
};

class DBusInterfaceToken : public TypeToken {
private:
    /**
     * must be valid interface name.
     * ex. org.freedesktop.NetworkManager
     */
    std::string name;

public:
    DBusInterfaceToken(unsigned int lineNum, std::string &&name) :
            TypeToken(lineNum), name(std::move(name)) { }

    ~DBusInterfaceToken() = default;

    const std::string &getTokenText() const {
        return this->name;
    }

    std::string toTokenText() const; // override
    void accept(TypeTokenVisitor *visitor); // override
};

/**
 * for multiple return type
 */
class ReturnTypeToken : public TypeToken {
private:
    std::vector<TypeToken *> typeTokens;

public:
    explicit ReturnTypeToken(TypeToken *token);
    ~ReturnTypeToken();

    void addTypeToken(TypeToken *token);

    const std::vector<TypeToken *> &getTypeTokens() const {
        return this->typeTokens;
    }

    bool hasMultiReturn() const {
        return this->typeTokens.size() > 1;
    }

    std::string toTokenText() const; // override
    void accept(TypeTokenVisitor *visitor); // override
};

class TypeOfToken : public TypeToken {
private:
    Node *exprNode;

public:
    TypeOfToken(Node *exprNode);
    ~TypeOfToken();

    Node *getExprNode() const {
        return this->exprNode;
    }

    std::string toTokenText() const; // override
    void accept(TypeTokenVisitor *visitor); // override
};

TypeToken *newAnyTypeToken(unsigned int lineNum = 0);

TypeToken *newVoidTypeToken(unsigned int lineNum = 0);


struct TypeTokenVisitor {
    virtual ~TypeTokenVisitor() = default;

    virtual void visitClassTypeToken(ClassTypeToken *token) = 0;
    virtual void visitReifiedTypeToken(ReifiedTypeToken *token) = 0;
    virtual void visitFuncTypeToken(FuncTypeToken *token) = 0;
    virtual void visitDBusInterfaceToken(DBusInterfaceToken *token) = 0;
    virtual void visitReturnTypeToken(ReturnTypeToken *token) = 0;
    virtual void visitTypeOfToken(TypeOfToken *token) = 0;
};


} // namespace ast
} // namespace ydsh

#endif //YDSH_TYPETOKEN_H
