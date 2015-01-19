/*
 * TypeLookupError.h
 *
 *  Created on: 2015/01/19
 *      Author: skgchxngsxyz-osx
 */

#ifndef PARSER_TYPELOOKUPERROR_H_
#define PARSER_TYPELOOKUPERROR_H_

#include "TypeCheckException.h"

class TypeLookupError {
private:
    std::string messageTemplate;

public:
    TypeLookupError(const std::string &messageTemplate);
    virtual ~TypeLookupError();

    const std::string &getTemplate() const;
};

class TypeLookupErrorOneArg : public TypeLookupError {
public:
    TypeLookupErrorOneArg(const std::string &messageTemplate);

    void report(const std::string &arg1) const throw(TypeLookupException);
};

class TypeLookupErrorThreeArg : public TypeLookupError {
public:
    TypeLookupErrorThreeArg(const std::string &messageTemplate);

    void report(const std::string &arg1, const std::string &arg2,
            const std::string &arg3) const throw(TypeLookupException);
};


// error message definition
// one arg
extern const TypeLookupErrorOneArg * const E_NotUseGeneric;
extern const TypeLookupErrorOneArg * const E_UndefinedType;
extern const TypeLookupErrorOneArg * const E_NotGenericBase;
extern const TypeLookupErrorOneArg * const E_NotPrimitive;
extern const TypeLookupErrorOneArg * const E_NotClass;
extern const TypeLookupErrorOneArg * const E_Nonheritable;
extern const TypeLookupErrorOneArg * const E_DefinedType;

// three arg
extern const TypeLookupErrorThreeArg * const E_UnmatchElement;

#endif /* PARSER_TYPELOOKUPERROR_H_ */
