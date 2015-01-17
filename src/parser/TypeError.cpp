/*
 * TypeError.cpp
 *
 *  Created on: 2015/01/15
 *      Author: skgchxngsxyz-opensuse
 */

#include <utility>

#include "TypeError.h"

// #######################
// ##     TypeError     ##
// #######################

TypeError::TypeError(std::string &&messageTemplate) :
        messageTemplate(std::move(messageTemplate)) {
}

TypeError::~TypeError() {
}

const std::string &TypeError::getTemplate() const {
    return this->messageTemplate;
}

// #############################
// ##    TypeErrorZeroArg     ##
// #############################

TypeErrorZeroArg::TypeErrorZeroArg(std::string &&messageTemplate) :
        TypeError(std::move(messageTemplate)) {
}

void TypeErrorZeroArg::report(int lineNum) const throw (TypeCheckException) {
    throw TypeCheckException(lineNum, this->getTemplate());
}

// #############################
// ##     TypeErrorOneArg     ##
// #############################

TypeErrorOneArg::TypeErrorOneArg(std::string &&messageTemplate) :
        TypeError(std::move(messageTemplate)) {
}

void TypeErrorOneArg::report(int lineNum, const std::string &arg1) const throw (TypeCheckException) {
    throw TypeCheckException(lineNum, this->getTemplate(), arg1);
}

// #############################
// ##     TypeErrorTwoArg     ##
// #############################

TypeErrorTwoArg::TypeErrorTwoArg(std::string &&messageTemplate) :
        TypeError(std::move(messageTemplate)) {
}

void TypeErrorTwoArg::report(int lineNum, const std::string &arg1, const std::string &arg2)
        const throw (TypeCheckException) {
    throw TypeCheckException(lineNum, this->getTemplate(), arg1, arg2);
}

// ###############################
// ##     TypeErrorThreeArg     ##
// ###############################

TypeErrorThreeArg::TypeErrorThreeArg(std::string &&messageTemplate) :
        TypeError(std::move(messageTemplate)) {
}

void TypeErrorThreeArg::report(int lineNum, const std::string &arg1, const std::string &arg2,
        const std::string &arg3) const throw (TypeCheckException) {
    throw TypeCheckException(lineNum, this->getTemplate(), arg1, arg2, arg3);
}

// zero arg
const TypeErrorZeroArg * const E_Unresolved    = new TypeErrorZeroArg("having unresolved type");
const TypeErrorZeroArg * const E_InsideLoop    = new TypeErrorZeroArg("only available inside loop statement");
const TypeErrorZeroArg * const E_UnfoundReturn = new TypeErrorZeroArg("not found return statement");
const TypeErrorZeroArg * const E_Unreachable   = new TypeErrorZeroArg("found unreachable code");
const TypeErrorZeroArg * const E_InsideFunc    = new TypeErrorZeroArg("only available inside function");
const TypeErrorZeroArg * const E_NotNeedExpr   = new TypeErrorZeroArg("not need expression");
const TypeErrorZeroArg * const E_Assignable    = new TypeErrorZeroArg("require assignable node");
const TypeErrorZeroArg * const E_ReadOnly      = new TypeErrorZeroArg("read only value");
const TypeErrorZeroArg * const E_InsideFinally = new TypeErrorZeroArg("unavailable inside finally block");

// one arg
const TypeErrorOneArg * const E_DefinedSymbol   = new TypeErrorOneArg("already defined symbol: %s");
const TypeErrorOneArg * const E_UndefinedSymbol = new TypeErrorOneArg("undefined symbol: %s");
const TypeErrorOneArg * const E_UndefinedField  = new TypeErrorOneArg("undefined field: %s");
const TypeErrorOneArg * const E_UndefinedMethod = new TypeErrorOneArg("undefined method: %s");
const TypeErrorOneArg * const E_UndefinedInit   = new TypeErrorOneArg("undefined constructor: %s");
const TypeErrorOneArg * const E_Unacceptable    = new TypeErrorOneArg("unacceptable type: %s");
const TypeErrorOneArg * const E_NotUseGeneric   = new TypeErrorOneArg("not directly use generic base type: %s");
const TypeErrorOneArg * const E_UndefinedType   = new TypeErrorOneArg("undefined type: %s");
const TypeErrorOneArg * const E_NotGenericBase  = new TypeErrorOneArg("not generic base type: %s");
const TypeErrorOneArg * const E_NotPrimitive    = new TypeErrorOneArg("not primitive type: %s");
const TypeErrorOneArg * const E_NotClass        = new TypeErrorOneArg("not class type: %s");
const TypeErrorOneArg * const E_Nonheritable    = new TypeErrorOneArg("nonheritable type: %s");
const TypeErrorOneArg * const E_DefinedType     = new TypeErrorOneArg("already defined type: %s");
const TypeErrorOneArg * const E_NoIterator      = new TypeErrorOneArg("not support iterator: %s");
const TypeErrorOneArg * const E_Unimplemented   = new TypeErrorOneArg("unimplemented type checker api: %s");

// two arg
const TypeErrorTwoArg * const E_Required     = new TypeErrorTwoArg("require %s, but is %s");
const TypeErrorTwoArg * const E_CastOp       = new TypeErrorTwoArg("unsupported cast op: %s -> %s");
const TypeErrorTwoArg * const E_UnaryOp      = new TypeErrorTwoArg("undefined operator: %s %s");
const TypeErrorTwoArg * const E_UnmatchParam = new TypeErrorTwoArg("not match parameter, require size is %s, but is %s");

// three arg
const TypeErrorThreeArg * const E_BinaryOp       = new TypeErrorThreeArg("undefined operator: %s %s %s");
const TypeErrorThreeArg * const E_UnmatchElement = new TypeErrorThreeArg("not match type element, %s requires %s type element, but is %s");
