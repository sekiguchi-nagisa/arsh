/*
 * TypeToken.h
 *
 *  Created on: 2015/01/13
 *      Author: skgchxngsxyz-osx
 */

#ifndef AST_TYPETOKEN_H_
#define AST_TYPETOKEN_H_

#include <memory>

#include "../core/DSType.h"

/**
 * represent for parsed type.
 */
class TypeToken {
private:
    int lineNum;

public:
    TypeToken(int lineNum);
    virtual ~TypeToken();

    int getLineNum();

    /**
     * convert to DSType.
     */
    virtual DSType *toType() = 0;   //TODO: add TypePool to parameter
};

class ClassTypeToken: public TypeToken {
private:
    std::string typeName;

public:
    ClassTypeToken(int lineNum, std::string &&typeName);

    DSType *toType();   // override
};

class ReifiedTypeToken: public TypeToken {
private:
    std::unique_ptr<TypeToken> templateTypeToken;
    std::vector<std::unique_ptr<TypeToken>> elementTypeTokens;

public:
    ReifiedTypeToken(std::unique_ptr<TypeToken> &&templateType);
    ~ReifiedTypeToken();

    void addElementTypeToken(std::unique_ptr<TypeToken> &&type);
    //TODO: add TypePool to parameter
    DSType *toType();   // override
};

class FuncTypeToken: public TypeToken {
private:
    /**
     * may be null, if has return type annotation (return void)
     */
    std::unique_ptr<TypeToken> returnTypeToken;

    /**
     * may be empty vector, if has no parameter
     */
    std::vector<std::unique_ptr<TypeToken>> paramTypeTokens;

    /**
     * UnresolvedClassType of Void
     */
    static std::unique_ptr<TypeToken> voidTypeToken;

public:
    FuncTypeToken(int lineNum);
    ~FuncTypeToken();

    void setReturnTypeToken(std::unique_ptr<TypeToken> &&type);
    void addParamTypeToken(std::unique_ptr<TypeToken> &&type);
    //TODO: add TypePool to parameter
    DSType *toType();   // override
};

#endif /* AST_TYPETOKEN_H_ */
