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

#include "../core/DSType.h"
#include "../core/TypePool.h"

namespace ydsh {
namespace ast {

using namespace ydsh::core;

/**
 * represent for parsed type.
 */
class TypeToken {
private:
    unsigned int lineNum;

public:
    TypeToken(unsigned int lineNum);

    virtual ~TypeToken();

    unsigned int getLineNum();

    /**
     * get token text
     */
    virtual std::string toTokenText() const = 0;

    /**
     * convert to DSType.
     */
    virtual DSType *toType(TypePool *typePool) = 0;
};

class ClassTypeToken : public TypeToken {
private:
    std::string typeName;

public:
    ClassTypeToken(unsigned int lineNum, std::string &&typeName);

    std::string toTokenText() const;  // override
    DSType *toType(TypePool *typePool);   // override
    const std::string &getTokenText();
};

/**
 * for reified type and tuple type
 */
class ReifiedTypeToken : public TypeToken {
private:
    ClassTypeToken *templateTypeToken;
    std::vector<TypeToken *> elementTypeTokens;

public:
    ReifiedTypeToken(ClassTypeToken *templateTypeToken);

    ~ReifiedTypeToken();

    void addElementTypeToken(TypeToken *type);

    std::string toTokenText() const;  // override
    DSType *toType(TypePool *typePool);   // override
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
    FuncTypeToken(TypeToken *type);

    ~FuncTypeToken();

    void addParamTypeToken(TypeToken *type);

    std::string toTokenText() const;  // override
    DSType *toType(TypePool *typePool);   // override
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

    std::string toTokenText() const; // override
    DSType *toType(TypePool *typePool);
};

TypeToken *newAnyTypeToken(unsigned int lineNum = 0);

TypeToken *newVoidTypeToken(unsigned int lineNum = 0);

ReifiedTypeToken *newTupleTypeToken(TypeToken *typeToken);

} // namespace ast
} // namespace ydsh

#endif /* AST_TYPETOKEN_H_ */
