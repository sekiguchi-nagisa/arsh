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

/**
 * for type error reporting
 */
class TypeCheckException {
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
    TypeCheckException(int lineNum, const char * const t);
    TypeCheckException(int lineNum, const char * const t, const std::string &arg1);
    TypeCheckException(int lineNum, const char * const t, const std::string &arg1,
            const std::string &arg2);
    TypeCheckException(int lineNum, const char * const t, const std::string &arg1,
            const std::string &arg2, const std::string &arg3);
    TypeCheckException(int lineNum, const std::string &t, const std::vector<std::string> &args);
    ~TypeCheckException();

    int getLineNum() const;
    const std::string &getTemplate() const ;
    const std::vector<std::string> &getArgs() const;

    bool operator==(const TypeCheckException &e);

    // error message definition
    // zero arg
    const static char * const E_Unresolved;
    const static char * const E_InsideLoop;
    const static char * const E_UnfoundReturn;
    const static char * const E_Unreachable;
    const static char * const E_InsideFunc;
    const static char * const E_NotNeedExpr;
    const static char * const E_Assignable;
    const static char * const E_ReadOnly;
    const static char * const E_InsideFinally;

    // one arg
    const static char * const E_DefinedSymbol;
    const static char * const E_UndefinedSymbol;
    const static char * const E_UndefinedField;
    const static char * const E_UndefinedMethod;
    const static char * const E_UndefinedInit;
    const static char * const E_Unacceptable;
    const static char * const E_NoIterator;
    const static char * const E_Unimplemented;

    // two arg
    const static char * const E_Required;
    const static char * const E_CastOp;
    const static char * const E_UnaryOp;
    const static char * const E_UnmatchParam;

    // three arg
    const static char * const E_BinaryOp;
};

#define REPORT_TC_ERROR0(name, node)                   do { throw TypeCheckException(node->getLineNum(), TypeCheckException::E_##name); } while(0)
#define REPORT_TC_ERROR1(name, node, arg1)             do { throw TypeCheckException(node->getLineNum(), TypeCheckException::E_##name, arg1); } while(0)
#define REPORT_TC_ERROR2(name, node, arg1, arg2)       do { throw TypeCheckException(node->getLineNum(), TypeCheckException::E_##name, arg1, arg2); } while(0)
#define REPORT_TC_ERROR3(name, node, arg1, arg2, arg3) do { throw TypeCheckException(node->getLineNum(), TypeCheckException::E_##name, arg1, arg2, arg3); } while(0)

#define E_Unresolved(node)                  REPORT_TC_ERROR0(Unresolved   , node)
#define E_InsideLoop(node)                  REPORT_TC_ERROR0(InsideLoop   , node)
#define E_UnfoundReturn(node)               REPORT_TC_ERROR0(UnfoundReturn, node)
#define E_Unreachable(node)                 REPORT_TC_ERROR0(Unreachable  , node)
#define E_InsideFunc(node)                  REPORT_TC_ERROR0(InsideFunc   , node)
#define E_NotNeedExpr(node)                 REPORT_TC_ERROR0(NotNeedExpr  , node)
#define E_Assignable(node)                  REPORT_TC_ERROR0(Assignable   , node)
#define E_ReadOnly(node)                    REPORT_TC_ERROR0(ReadOnly     , node)
#define E_InsideFinally(node)               REPORT_TC_ERROR0(InsideFinally, node)

#define E_DefinedSymbol(node, arg1)         REPORT_TC_ERROR1(DefinedSymbol  , node, arg1)
#define E_UndefinedSymbol(node, arg1)       REPORT_TC_ERROR1(UndefinedSymbol, node, arg1)
#define E_UndefinedField(node, arg1)        REPORT_TC_ERROR1(UndefinedField , node, arg1)
#define E_UndefinedMethod(node, arg1)       REPORT_TC_ERROR1(UndefinedMethod, node, arg1)
#define E_UndefinedInit(node, arg1)         REPORT_TC_ERROR1(UndefinedInit  , node, arg1)
#define E_Unacceptable(node, arg1)          REPORT_TC_ERROR1(Unacceptable   , node, arg1)
#define E_NoIterator(node, arg1)            REPORT_TC_ERROR1(NoIterator     , node, arg1)
#define E_Unimplemented(node, arg1)         REPORT_TC_ERROR1(Unimplemented  , node, arg1)

#define E_Required(node, arg1, arg2)        REPORT_TC_ERROR2(Required    , node, arg1, arg2)
#define E_CastOp(node, arg1, arg2)          REPORT_TC_ERROR2(CastOp      , node, arg1, arg2)
#define E_UnaryOp(node, arg1, arg2)         REPORT_TC_ERROR2(UnaryOp     , node, arg1, arg2)
#define E_UnmatchParam(node, arg1, arg2)    REPORT_TC_ERROR2(UnmatchParam, node, arg1, arg2)

#define E_BinaryOp(node, arg1, arg2, arg3)  REPORT_TC_ERROR3(E_BinaryOp  , node, arg1, arg2, arg3)

#endif /* PARSER_TYPECHECKERROR_H_ */
