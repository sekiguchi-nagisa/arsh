/*
 * TypeToken.h
 *
 *  Created on: 2015/01/13
 *      Author: skgchxngsxyz-osx
 */

#ifndef AST_TYPETOKEN_H_
#define AST_TYPETOKEN_H_

#include "../core/DSType.h"
#include "../core/TypePool.h"

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
    virtual DSType *toType(TypePool *typePool) = 0;
};

class ClassTypeToken: public TypeToken {
private:
    std::string typeName;

public:
    ClassTypeToken(int lineNum, std::string &&typeName);

    DSType *toType(TypePool *typePool);   // override
};

class ReifiedTypeToken: public TypeToken {
private:
    TypeToken* templateTypeToken;
    std::vector<TypeToken*> elementTypeTokens;

public:
    ReifiedTypeToken(TypeToken *templateType);
    ~ReifiedTypeToken();

    void addElementTypeToken(TypeToken *type);
    DSType *toType(TypePool *typePool);   // override
};

class FuncTypeToken: public TypeToken {
private:
    /**
     * may be null, if has return type annotation (return void)
     */
    TypeToken *returnTypeToken;

    /**
     * may be empty vector, if has no parameter
     */
    std::vector<TypeToken*> paramTypeTokens;

    /**
     * UnresolvedClassType of Void
     */
    static TypeToken *voidTypeToken;

public:
    FuncTypeToken(int lineNum);
    ~FuncTypeToken();

    void setReturnTypeToken(TypeToken *type);
    void addParamTypeToken(TypeToken *type);
    DSType *toType(TypePool *typePool);   // override
};

#endif /* AST_TYPETOKEN_H_ */
