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

#ifndef AST_TYPETOKEN_H_
#define AST_TYPETOKEN_H_

#include <string>
#include <vector>

namespace ydsh {
namespace ast {

struct TypeTokenVisitor;

/**
 * represent for parsed type.
 */
class TypeToken {
private:
    unsigned int lineNum;

public:
    explicit TypeToken(unsigned int lineNum);

    virtual ~TypeToken() = default;

    unsigned int getLineNum();

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
    ClassTypeToken(unsigned int lineNum, std::string &&typeName);
    ~ClassTypeToken() = default;

    std::string toTokenText() const;  // override
    const std::string &getTokenText();
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
    explicit ReifiedTypeToken(ClassTypeToken *templateTypeToken);

    ~ReifiedTypeToken();

    void addElementTypeToken(TypeToken *type);
    ClassTypeToken *getTemplate();
    const std::vector<TypeToken *> &getElementTypeTokens();

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
    explicit FuncTypeToken(TypeToken *type);

    ~FuncTypeToken();

    void addParamTypeToken(TypeToken *type);
    const std::vector<TypeToken *> &getParamTypeTokens();
    TypeToken *getReturnTypeToken();

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
    DBusInterfaceToken(unsigned int lineNum, std::string &&name);
    ~DBusInterfaceToken() = default;

    const std::string &getTokenText();

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
    const std::vector<TypeToken *> &getTypeTokens();
    bool hasMultiReturn();

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
};


} // namespace ast
} // namespace ydsh

#endif /* AST_TYPETOKEN_H_ */
