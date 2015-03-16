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

#define EACH_TL_ERROR(ERROR) \
        /* zero arg */\
        ERROR(E_TupleElement  , "Tuple type require at least 2 type element.") \
        /* one arg */\
        ERROR(E_NotUseGeneric , "not directly use generic base type: %s") \
        ERROR(E_UndefinedType , "undefined type: %s") \
        ERROR(E_NotGenericBase, "unsupported type template: %s") \
        ERROR(E_NotPrimitive  , "not primitive type: %s") \
        ERROR(E_NotClass      , "not class type: %s") \
        ERROR(E_Nonheritable  , "nonheritable type: %s") \
        ERROR(E_DefinedType   , "already defined type: %s") \
        ERROR(E_InvalidElement, "invalid type element: %s") \
        /* three arg */\
        ERROR(E_UnmatchElement, "not match type element, %s requires %s type element, but is %s")


class TypeLookupError {
public:
    typedef enum {
#define GEN_ENUM(ENUM, MSG) ENUM,
        EACH_TL_ERROR(GEN_ENUM)
#undef GEN_ENUM
    } ErrorKind;

private:
    std::string messageTemplate;
    std::vector<std::string> args;

public:
    TypeLookupError(TypeLookupError::ErrorKind kind);
    ~TypeLookupError();

    const std::string &getTemplate() const;
    const std::vector<std::string> &getArgs() const;

    bool operator==(const TypeLookupError &e);

    static void report(ErrorKind kind);
    static void report(ErrorKind kind, const std::string &arg1);
    static void report(ErrorKind kind, const std::string &arg1,
            const std::string &arg2, const std::string &arg3);
};

// helper macro for error reporting

#define REPORT_TL_ERROR0(name)                   do { TypeLookupError::report(TypeLookupError::E_##name); } while(0)
#define REPORT_TL_ERROR1(name, arg1)             do { TypeLookupError::report(TypeLookupError::E_##name, arg1); } while(0)
#define REPORT_TL_ERROR3(name, arg1, arg2, arg3) do { TypeLookupError::report(TypeLookupError::E_##name, arg1, arg2, arg3); } while(0)

#define E_TupleElement()                   REPORT_TL_ERROR0(TupleElement)
#define E_NotUseGeneric(arg1)              REPORT_TL_ERROR1(NotUseGeneric , arg1)
#define E_UndefinedType(arg1)              REPORT_TL_ERROR1(UndefinedType , arg1)
#define E_NotGenericBase(arg1)             REPORT_TL_ERROR1(NotGenericBase, arg1)
#define E_NotPrimitive(arg1)               REPORT_TL_ERROR1(NotPrimitive  , arg1)
#define E_NotClass(arg1)                   REPORT_TL_ERROR1(NotClass      , arg1)
#define E_Nonheritable(arg1)               REPORT_TL_ERROR1(Nonheritable  , arg1)
#define E_DefinedType(arg1)                REPORT_TL_ERROR1(DefinedType   , arg1)
#define E_InvalidElement(arg1)             REPORT_TL_ERROR1(InvalidElement, arg1)

#define E_UnmatchElement(arg1, arg2, arg3) REPORT_TL_ERROR3(UnmatchElement, arg1, arg2, arg3)


#endif /* CORE_TYPELOOKUPERROR_H_ */
