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

#ifndef YDSH_INCLUDE_PARSER_TYPELOOKUPERROR_H_
#define YDSH_INCLUDE_PARSER_TYPELOOKUPERROR_H_

#include <parser/TypeCheckException.h>

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

#endif /* YDSH_INCLUDE_PARSER_TYPELOOKUPERROR_H_ */
