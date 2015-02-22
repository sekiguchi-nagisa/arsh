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

#ifndef PARSER_TYPECHECKERROR_H_
#define PARSER_TYPECHECKERROR_H_

#include <string>
#include <vector>
#include <core/TypeLookupError.h>

#define EACH_TC_ERROR(ERROR) \
        /* zero arg */\
        ERROR(E_Unresolved      , "having unresolved type") \
        ERROR(E_InsideLoop      , "only available inside loop statement") \
        ERROR(E_UnfoundReturn   , "not found return statement") \
        ERROR(E_Unreachable     , "found unreachable code") \
        ERROR(E_InsideFunc      , "only available inside function") \
        ERROR(E_NotNeedExpr     , "not need expression") \
        ERROR(E_Assignable      , "require assignable node") \
        ERROR(E_ReadOnly        , "read only value") \
        ERROR(E_InsideFinally   , "unavailable inside finally block") \
        ERROR(E_UnneedNamedArg  , "not need named argument") \
        ERROR(E_NeedNamedArg    , "need named argument") \
        ERROR(E_NoDefaultValue  , "has no default value") \
        /* one arg */\
        ERROR(E_DefinedSymbol     , "already defined symbol: %s") \
        ERROR(E_UndefinedSymbol   , "undefined symbol: %s") \
        ERROR(E_UndefinedField    , "undefined field: %s") \
        ERROR(E_UndefinedMethod   , "undefined method: %s") \
        ERROR(E_UndefinedInit     , "undefined constructor: %s") \
        ERROR(E_Unacceptable      , "unacceptable type: %s") \
        ERROR(E_NoIterator        , "not support iterator: %s") \
        ERROR(E_UnfoundNamedParam , "undefined parameter name: %s") \
        ERROR(E_DupNamedArg       , "found duplicated named argument: %s") \
        ERROR(E_Unimplemented     , "unimplemented type checker api: %s") \
        /* two arg */\
        ERROR(E_Required        , "require %s, but is %s") \
        ERROR(E_CastOp          , "unsupported cast op: %s -> %s") \
        ERROR(E_UnaryOp         , "undefined operator: %s %s") \
        ERROR(E_UnmatchParam    , "not match parameter, require size is %s, but is %s") \
        /* three arg */\
        ERROR(E_BinaryOp        , "undefined operator: %s %s %s")


/**
 * for type error reporting
 */
class TypeCheckError {
public:
    typedef enum {
#define GEN_ENUM(ENUM, MSG) ENUM,
        EACH_TC_ERROR(GEN_ENUM)
#undef GEN_ENUM
    } ErrorKind;

private:
    /**
     * line number of error node
     */
    int lineNum;

    /**
     * template of error message
     */
    std::string t;

    /**
     * message arguments
     */
    std::vector<std::string> args;

public:
    TypeCheckError(int lineNum, ErrorKind kind);
    TypeCheckError(int lineNum, ErrorKind kind, const std::string &arg1);
    TypeCheckError(int lineNum, ErrorKind kind, const std::string &arg1,
            const std::string &arg2);
    TypeCheckError(int lineNum, ErrorKind kind, const std::string &arg1,
            const std::string &arg2, const std::string &arg3);
    TypeCheckError(int lineNum, const TypeLookupError &e);
    ~TypeCheckError();

    int getLineNum() const;
    const std::string &getTemplate() const ;
    const std::vector<std::string> &getArgs() const;

    bool operator==(const TypeCheckError &e);
};

// helper macro for error reporting

#define REPORT_TC_ERROR0(name, node)                   do { throw TypeCheckError(node->getLineNum(), TypeCheckError::E_##name); } while(0)
#define REPORT_TC_ERROR1(name, node, arg1)             do { throw TypeCheckError(node->getLineNum(), TypeCheckError::E_##name, arg1); } while(0)
#define REPORT_TC_ERROR2(name, node, arg1, arg2)       do { throw TypeCheckError(node->getLineNum(), TypeCheckError::E_##name, arg1, arg2); } while(0)
#define REPORT_TC_ERROR3(name, node, arg1, arg2, arg3) do { throw TypeCheckError(node->getLineNum(), TypeCheckError::E_##name, arg1, arg2, arg3); } while(0)


#define E_Unresolved(node)                  REPORT_TC_ERROR0(Unresolved       , node)
#define E_InsideLoop(node)                  REPORT_TC_ERROR0(InsideLoop       , node)
#define E_UnfoundReturn(node)               REPORT_TC_ERROR0(UnfoundReturn    , node)
#define E_Unreachable(node)                 REPORT_TC_ERROR0(Unreachable      , node)
#define E_InsideFunc(node)                  REPORT_TC_ERROR0(InsideFunc       , node)
#define E_NotNeedExpr(node)                 REPORT_TC_ERROR0(NotNeedExpr      , node)
#define E_Assignable(node)                  REPORT_TC_ERROR0(Assignable       , node)
#define E_ReadOnly(node)                    REPORT_TC_ERROR0(ReadOnly         , node)
#define E_InsideFinally(node)               REPORT_TC_ERROR0(InsideFinally    , node)
#define E_UnneedNamedArg(node)              REPORT_TC_ERROR0(UnneedNamedArg   , node)
#define E_NeedNamedArg(node)                REPORT_TC_ERROR0(NeedNamedArg     , node)
#define E_NoDefaultValue(node)              REPORT_TC_ERROR0(NoDefaultValue   , node)

#define E_DefinedSymbol(node, arg1)         REPORT_TC_ERROR1(DefinedSymbol    , node, arg1)
#define E_UndefinedSymbol(node, arg1)       REPORT_TC_ERROR1(UndefinedSymbol  , node, arg1)
#define E_UndefinedField(node, arg1)        REPORT_TC_ERROR1(UndefinedField   , node, arg1)
#define E_UndefinedMethod(node, arg1)       REPORT_TC_ERROR1(UndefinedMethod  , node, arg1)
#define E_UndefinedInit(node, arg1)         REPORT_TC_ERROR1(UndefinedInit    , node, arg1)
#define E_Unacceptable(node, arg1)          REPORT_TC_ERROR1(Unacceptable     , node, arg1)
#define E_NoIterator(node, arg1)            REPORT_TC_ERROR1(NoIterator       , node, arg1)
#define E_UnfoundNamedParam(node, arg1)     REPORT_TC_ERROR1(UnfoundNamedParam, node, arg1)
#define E_DupNamedArg(node, arg1)           REPORT_TC_ERROR1(DupNamedArg      , node, arg1)
#define E_Unimplemented(node, arg1)         REPORT_TC_ERROR1(Unimplemented    , node, arg1)

#define E_Required(node, arg1, arg2)        REPORT_TC_ERROR2(Required    , node, arg1, arg2)
#define E_CastOp(node, arg1, arg2)          REPORT_TC_ERROR2(CastOp      , node, arg1, arg2)
#define E_UnaryOp(node, arg1, arg2)         REPORT_TC_ERROR2(UnaryOp     , node, arg1, arg2)
#define E_UnmatchParam(node, arg1, arg2)    REPORT_TC_ERROR2(UnmatchParam, node, arg1, arg2)

#define E_BinaryOp(node, arg1, arg2, arg3)  REPORT_TC_ERROR3(E_BinaryOp  , node, arg1, arg2, arg3)

#endif /* PARSER_TYPECHECKERROR_H_ */
