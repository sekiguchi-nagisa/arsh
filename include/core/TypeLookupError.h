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

#ifndef CORE_TYPELOOKUPERROR_H_
#define CORE_TYPELOOKUPERROR_H_

#include <string>
#include <vector>

class TypeLookupException {
private:
    std::string messageTemplate;
    std::vector<std::string> args;

public:
    TypeLookupException(const char * const t, const std::string &arg1);
    TypeLookupException(const char * const t, const std::string &arg1, const std::string &arg2, const std::string &arg3);

    const std::string &getTemplate() const;
    const std::vector<std::string> &getArgs() const;

    bool operator==(const TypeLookupException &e);

    // error message definition
    // one arg
    const static char * const E_NotUseGeneric ;
    const static char * const E_UndefinedType ;
    const static char * const E_NotGenericBase;
    const static char * const E_NotPrimitive  ;
    const static char * const E_NotClass      ;
    const static char * const E_Nonheritable  ;
    const static char * const E_DefinedType   ;

    // three arg
    const static char * const E_UnmatchElement;
};

#define REPORT_TL_ERROR1(name, arg1)             do { throw TypeLookupException(TypeLookupException::E_##name, arg1); } while(0)
#define REPORT_TL_ERROR3(name, arg1, arg2, arg3) do { throw TypeLookupException(TypeLookupException::E_##name, arg1, arg2, arg3); } while(0)

#define E_NotUseGeneric(arg1)              REPORT_TL_ERROR1(NotUseGeneric , arg1)
#define E_UndefinedType(arg1)              REPORT_TL_ERROR1(UndefinedType , arg1)
#define E_NotGenericBase(arg1)             REPORT_TL_ERROR1(NotGenericBase, arg1)
#define E_NotPrimitive(arg1)               REPORT_TL_ERROR1(NotPrimitive  , arg1)
#define E_NotClass(arg1)                   REPORT_TL_ERROR1(NotClass      , arg1)
#define E_Nonheritable(arg1)               REPORT_TL_ERROR1(Nonheritable  , arg1)
#define E_DefinedType(arg1)                REPORT_TL_ERROR1(DefinedType   , arg1)

#define E_UnmatchElement(arg1, arg2, arg3) REPORT_TL_ERROR3(UnmatchElement, arg1, arg2, arg3)


#endif /* CORE_TYPELOOKUPERROR_H_ */
