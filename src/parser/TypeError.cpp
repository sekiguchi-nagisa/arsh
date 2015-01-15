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

const std::string &TypeError::getTemplate() {
    return this->messageTemplate;
}

// #############################
// ##    TypeErrorZeroArg     ##
// #############################

TypeErrorZeroArg::TypeErrorZeroArg(std::string &&messageTemplate) :
        TypeError(std::move(messageTemplate)) {
}

void TypeErrorZeroArg::report(int lineNum) throw (TypeCheckException) {
    throw TypeCheckException(lineNum, this->getTemplate());
}

// #############################
// ##     TypeErrorOneArg     ##
// #############################

TypeErrorOneArg::TypeErrorOneArg(std::string &&messageTemplate) :
        TypeError(std::move(messageTemplate)) {
}

void TypeErrorOneArg::report(int lineNum, const std::string &arg1) throw (TypeCheckException) {
    throw TypeCheckException(lineNum, this->getTemplate(), arg1);
}

// #############################
// ##     TypeErrorTwoArg     ##
// #############################

TypeErrorTwoArg::TypeErrorTwoArg(std::string &&messageTemplate) :
        TypeError(std::move(messageTemplate)) {
}

void TypeErrorTwoArg::report(int lineNum, const std::string &arg1, const std::string &arg2)
        throw (TypeCheckException) {
    throw TypeCheckException(lineNum, this->getTemplate(), arg1, arg2);
}

// ###############################
// ##     TypeErrorThreeArg     ##
// ###############################

TypeErrorThreeArg::TypeErrorThreeArg(std::string &&messageTemplate) :
        TypeError(std::move(messageTemplate)) {
}

void TypeErrorThreeArg::report(int lineNum, const std::string &arg1, const std::string &arg2,
        const std::string &arg3) throw (TypeCheckException) {
    throw TypeCheckException(lineNum, this->getTemplate(), arg1, arg2, arg3);
}

// zero arg
const std::unique_ptr<TypeErrorZeroArg> Unresolved(
        std::unique_ptr < TypeErrorZeroArg > (new TypeErrorZeroArg("having unresolved type")));
const std::unique_ptr<TypeErrorZeroArg> InsideLoop(
        std::unique_ptr < TypeErrorZeroArg
                > (new TypeErrorZeroArg("only available inside loop statement")));
const std::unique_ptr<TypeErrorZeroArg> UnfoundReturn(
        std::unique_ptr < TypeErrorZeroArg > (new TypeErrorZeroArg("not found return statement")));
const std::unique_ptr<TypeErrorZeroArg> Unreachable(
        std::unique_ptr < TypeErrorZeroArg > (new TypeErrorZeroArg("found unreachable code")));
const std::unique_ptr<TypeErrorZeroArg> InsideFunc(
        std::unique_ptr < TypeErrorZeroArg
                > (new TypeErrorZeroArg("only available inside function")));
const std::unique_ptr<TypeErrorZeroArg> NotNeedExpr(
        std::unique_ptr < TypeErrorZeroArg > (new TypeErrorZeroArg("not need expression")));
const std::unique_ptr<TypeErrorZeroArg> Assignable(
        std::unique_ptr < TypeErrorZeroArg > (new TypeErrorZeroArg("require assignable node")));
const std::unique_ptr<TypeErrorZeroArg> ReadOnly(
        std::unique_ptr < TypeErrorZeroArg > (new TypeErrorZeroArg("read only value")));
const std::unique_ptr<TypeErrorZeroArg> InsideFinally(
        std::unique_ptr < TypeErrorZeroArg
                > (new TypeErrorZeroArg("unavailable inside finally block")));

// one arg
const std::unique_ptr<TypeErrorOneArg> DefinedSymbol(
        std::unique_ptr < TypeErrorOneArg > (new TypeErrorOneArg("already defined symbol: %s")));
const std::unique_ptr<TypeErrorOneArg> UndefinedSymbol(
        std::unique_ptr < TypeErrorOneArg > (new TypeErrorOneArg("undefined symbol: %s")));
const std::unique_ptr<TypeErrorOneArg> UndefinedField(
        std::unique_ptr < TypeErrorOneArg > (new TypeErrorOneArg("undefined field: %s")));
const std::unique_ptr<TypeErrorOneArg> UndefinedMethod(
        std::unique_ptr < TypeErrorOneArg > (new TypeErrorOneArg("undefined method: %s")));
const std::unique_ptr<TypeErrorOneArg> UndefinedInit(
        std::unique_ptr < TypeErrorOneArg > (new TypeErrorOneArg("undefined constructor: %s")));
const std::unique_ptr<TypeErrorOneArg> Unacceptable(
        std::unique_ptr < TypeErrorOneArg > (new TypeErrorOneArg("unacceptable type: %s")));
const std::unique_ptr<TypeErrorOneArg> NotUseGeneric(
        std::unique_ptr < TypeErrorOneArg
                > (new TypeErrorOneArg("not directly use generic base type: %s")));
const std::unique_ptr<TypeErrorOneArg> UndefinedType(
        std::unique_ptr < TypeErrorOneArg > (new TypeErrorOneArg("undefined type: %s")));
const std::unique_ptr<TypeErrorOneArg> NotGenericBase(
        std::unique_ptr < TypeErrorOneArg > (new TypeErrorOneArg("not generic base type: %s")));
const std::unique_ptr<TypeErrorOneArg> NotPrimitive(
        std::unique_ptr < TypeErrorOneArg > (new TypeErrorOneArg("not primitive type: %s")));
const std::unique_ptr<TypeErrorOneArg> NotClass(
        std::unique_ptr < TypeErrorOneArg > (new TypeErrorOneArg("not class type: %s")));
const std::unique_ptr<TypeErrorOneArg> Nonheritable(
        std::unique_ptr < TypeErrorOneArg > (new TypeErrorOneArg("nonheritable type: %s")));
const std::unique_ptr<TypeErrorOneArg> DefinedType(
        std::unique_ptr < TypeErrorOneArg > (new TypeErrorOneArg("already defined type: %s")));
const std::unique_ptr<TypeErrorOneArg> Unimplemented(
        std::unique_ptr < TypeErrorOneArg
                > (new TypeErrorOneArg("unimplemented type checker api: %s")));

// two arg
const std::unique_ptr<TypeErrorTwoArg> Required(
        std::unique_ptr < TypeErrorTwoArg > (new TypeErrorTwoArg("require %s, but is %s")));
const std::unique_ptr<TypeErrorTwoArg> CastOp(
        std::unique_ptr < TypeErrorTwoArg > (new TypeErrorTwoArg("unsupported cast op: %s -> %s")));
const std::unique_ptr<TypeErrorTwoArg> UnaryOp(
        std::unique_ptr < TypeErrorTwoArg > (new TypeErrorTwoArg("undefined operator: %s %s")));
const std::unique_ptr<TypeErrorTwoArg> UnmatchParam(
        std::unique_ptr < TypeErrorTwoArg
                > (new TypeErrorTwoArg("not match parameter, require size is %d, but is %d")));

// three arg
const std::unique_ptr<TypeErrorThreeArg> BinaryOp(
        std::unique_ptr < TypeErrorThreeArg
                > (new TypeErrorThreeArg("undefined operator: %s %s %s")));
const std::unique_ptr<TypeErrorThreeArg> UnmatchElement(
        std::unique_ptr < TypeErrorThreeArg
                > (new TypeErrorThreeArg(
                        "not match type element, %s requires %s type element, but is %s")));
