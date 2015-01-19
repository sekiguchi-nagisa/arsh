/*
 * TypeLookupError.cpp
 *
 *  Created on: 2015/01/19
 *      Author: skgchxngsxyz-osx
 */

#include "TypeLookupError.h"

// #############################
// ##     TypeLookupError     ##
// #############################

TypeLookupError::TypeLookupError(const std::string &messageTemplate) :
        messageTemplate(messageTemplate) {
}

TypeLookupError::~TypeLookupError() {
}

const std::string &TypeLookupError::getTemplate() const {
    return this->messageTemplate;
}


// ###################################
// ##     TypeLookupErrorOneArg     ##
// ###################################

TypeLookupErrorOneArg::TypeLookupErrorOneArg(const std::string &messageTemplate) :
        TypeLookupError(messageTemplate) {
}

void TypeLookupErrorOneArg::report(const std::string &arg1)  const throw(TypeLookupException) {
    throw TypeLookupException(this->getTemplate(), arg1);
}


// #####################################
// ##     TypeLookupErrorThreeArg     ##
// #####################################

TypeLookupErrorThreeArg::TypeLookupErrorThreeArg(const std::string &messageTemplate) :
        TypeLookupError(messageTemplate) {
}

void TypeLookupErrorThreeArg::report(const std::string &arg1, const std::string &arg2,
        const std::string &arg3) const throw(TypeLookupException) {
    throw TypeLookupException(this->getTemplate(), arg1, arg2, arg3);
}


// error message definition
// one arg
const TypeLookupErrorOneArg * const E_NotUseGeneric  = new TypeLookupErrorOneArg("not directly use generic base type: %s");
const TypeLookupErrorOneArg * const E_UndefinedType  = new TypeLookupErrorOneArg("undefined type: %s");
const TypeLookupErrorOneArg * const E_NotGenericBase = new TypeLookupErrorOneArg("not generic base type: %s");
const TypeLookupErrorOneArg * const E_NotPrimitive   = new TypeLookupErrorOneArg("not primitive type: %s");
const TypeLookupErrorOneArg * const E_NotClass       = new TypeLookupErrorOneArg("not class type: %s");
const TypeLookupErrorOneArg * const E_Nonheritable   = new TypeLookupErrorOneArg("nonheritable type: %s");
const TypeLookupErrorOneArg * const E_DefinedType    = new TypeLookupErrorOneArg("already defined type: %s");

// three arg
const TypeLookupErrorThreeArg * const E_UnmatchElement = new TypeLookupErrorThreeArg("not match type element, %s requires %s type element, but is %s");
