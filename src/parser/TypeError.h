/*
 * TypeError.h
 *
 *  Created on: 2015/01/15
 *      Author: skgchxngsxyz-opensuse
 */

#ifndef SRC_PARSER_TYPEERROR_H_
#define SRC_PARSER_TYPEERROR_H_

#include <string>
#include <memory>

#include "TypeCheckException.h"

/**
 * for type error reporting. not use it directly
 */
class TypeError {
private:
    std::string messageTemplate;

public:
    TypeError(std::string &&messageTemplate);
    virtual ~TypeError();

    const std::string &getTemplate();
};

class TypeErrorZeroArg: public TypeError {
public:
    TypeErrorZeroArg(std::string &&messageTemplate);

    void report(int lineNum) throw (TypeCheckException);
};

class TypeErrorOneArg: public TypeError {
public:
    TypeErrorOneArg(std::string &&messageTemplate);

    void report(int lineNum, const std::string &arg1) throw (TypeCheckException);
};

class TypeErrorTwoArg: public TypeError {
public:
    TypeErrorTwoArg(std::string &&messageTemplate);

    void report(int lineNum, const std::string &arg1, const std::string &arg2)
            throw (TypeCheckException);
};

class TypeErrorThreeArg: public TypeError {
public:
    TypeErrorThreeArg(std::string &&messageTemplate);

    void report(int lineNum, const std::string &arg1, const std::string &arg2,
            const std::string &arg3) throw (TypeCheckException);
};

// error message definition
// zero arg
extern const std::unique_ptr<TypeErrorZeroArg> Unresolved;
extern const std::unique_ptr<TypeErrorZeroArg> InsideLoop;
extern const std::unique_ptr<TypeErrorZeroArg> UnfoundReturn;
extern const std::unique_ptr<TypeErrorZeroArg> Unreachable;
extern const std::unique_ptr<TypeErrorZeroArg> InsideFunc;
extern const std::unique_ptr<TypeErrorZeroArg> NotNeedExpr;
extern const std::unique_ptr<TypeErrorZeroArg> Assignable;
extern const std::unique_ptr<TypeErrorZeroArg> ReadOnly;
extern const std::unique_ptr<TypeErrorZeroArg> InsideFinally;

// one arg
extern const std::unique_ptr<TypeErrorOneArg> DefinedSymbol;
extern const std::unique_ptr<TypeErrorOneArg> UndefinedSymbol;
extern const std::unique_ptr<TypeErrorOneArg> UndefinedField;
extern const std::unique_ptr<TypeErrorOneArg> UndefinedMethod;
extern const std::unique_ptr<TypeErrorOneArg> UndefinedInit;
extern const std::unique_ptr<TypeErrorOneArg> Unacceptable;
extern const std::unique_ptr<TypeErrorOneArg> NotUseGeneric;
extern const std::unique_ptr<TypeErrorOneArg> UndefinedType;
extern const std::unique_ptr<TypeErrorOneArg> NotGenericBase;
extern const std::unique_ptr<TypeErrorOneArg> NotPrimitive;
extern const std::unique_ptr<TypeErrorOneArg> NotClass;
extern const std::unique_ptr<TypeErrorOneArg> Nonheritable;
extern const std::unique_ptr<TypeErrorOneArg> DefinedType;
extern const std::unique_ptr<TypeErrorOneArg> Unimplemented;

// two arg
extern const std::unique_ptr<TypeErrorTwoArg> Required;
extern const std::unique_ptr<TypeErrorTwoArg> CastOp;
extern const std::unique_ptr<TypeErrorTwoArg> UnaryOp;
extern const std::unique_ptr<TypeErrorTwoArg> UnmatchParam;

// three arg
extern const std::unique_ptr<TypeErrorThreeArg> BinaryOp;
extern const std::unique_ptr<TypeErrorThreeArg> UnmatchElement;

#endif /* SRC_PARSER_TYPEERROR_H_ */
