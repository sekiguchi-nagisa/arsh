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

    const std::string &getTemplate() const;
};

class TypeErrorZeroArg: public TypeError {
public:
    TypeErrorZeroArg(std::string &&messageTemplate);

    void report(int lineNum) const throw (TypeCheckException);
};

class TypeErrorOneArg: public TypeError {
public:
    TypeErrorOneArg(std::string &&messageTemplate);

    void report(int lineNum, const std::string &arg1) const throw (TypeCheckException);
};

class TypeErrorTwoArg: public TypeError {
public:
    TypeErrorTwoArg(std::string &&messageTemplate);

    void report(int lineNum, const std::string &arg1, const std::string &arg2)
            const throw (TypeCheckException);
};

class TypeErrorThreeArg: public TypeError {
public:
    TypeErrorThreeArg(std::string &&messageTemplate);

    void report(int lineNum, const std::string &arg1, const std::string &arg2,
            const std::string &arg3) const throw (TypeCheckException);
};

// error message definition
// zero arg
extern const TypeErrorZeroArg * const E_Unresolved;
extern const TypeErrorZeroArg * const E_InsideLoop;
extern const TypeErrorZeroArg * const E_UnfoundReturn;
extern const TypeErrorZeroArg * const E_Unreachable;
extern const TypeErrorZeroArg * const E_InsideFunc;
extern const TypeErrorZeroArg * const E_NotNeedExpr;
extern const TypeErrorZeroArg * const E_Assignable;
extern const TypeErrorZeroArg * const E_ReadOnly;
extern const TypeErrorZeroArg * const E_InsideFinally;

// one arg
extern const TypeErrorOneArg * const E_DefinedSymbol;
extern const TypeErrorOneArg * const E_UndefinedSymbol;
extern const TypeErrorOneArg * const E_UndefinedField;
extern const TypeErrorOneArg * const E_UndefinedMethod;
extern const TypeErrorOneArg * const E_UndefinedInit;
extern const TypeErrorOneArg * const E_Unacceptable;
extern const TypeErrorOneArg * const E_NoIterator;
extern const TypeErrorOneArg * const E_Unimplemented;

// two arg
extern const TypeErrorTwoArg * const E_Required;
extern const TypeErrorTwoArg * const E_CastOp;
extern const TypeErrorTwoArg * const E_UnaryOp;
extern const TypeErrorTwoArg * const E_UnmatchParam;

// three arg
extern const TypeErrorThreeArg * const E_BinaryOp;

#endif /* SRC_PARSER_TYPEERROR_H_ */
