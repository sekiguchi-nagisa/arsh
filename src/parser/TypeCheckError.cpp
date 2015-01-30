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

#include <parser/TypeCheckError.h>


// ################################
// ##     TypeCheckException     ##
// ################################

TypeCheckException::TypeCheckException(int lineNum, const char * const t) :
        lineNum(lineNum), t(t), args(0) {
}

TypeCheckException::TypeCheckException(int lineNum, const char * const t, const std::string &arg1) :
        lineNum(lineNum), t(t), args(1) {
    args.push_back(arg1);
}

TypeCheckException::TypeCheckException(int lineNum, const char * const t, const std::string &arg1,
        const std::string &arg2) :
        lineNum(lineNum), t(t), args(2) {
    args.push_back(arg1);
    args.push_back(arg2);
}

TypeCheckException::TypeCheckException(int lineNum, const char * const t, const std::string &arg1,
        const std::string &arg2, const std::string &arg3) :
        lineNum(lineNum), t(t), args(3) {
    args.push_back(arg1);
    args.push_back(arg2);
    args.push_back(arg3);
}

TypeCheckException::TypeCheckException(int lineNum, const std::string &t, const std::vector<std::string> &args) :
        lineNum(lineNum), t(t), args(args) {
}

TypeCheckException::~TypeCheckException() {
}

int TypeCheckException::getLineNum() const {
    return this->lineNum;
}

const std::string &TypeCheckException::getTemplate() const {
    return this->t;
}

const std::vector<std::string> &TypeCheckException::getArgs() const {
    return this->args;
}

bool TypeCheckException::operator==(const TypeCheckException &e) {
    // check line num
    if(this->lineNum != e.getLineNum()) {
        return false;
    }

    // check template
    if(this->t != e.getTemplate()) {
        return false;
    }

    // check arg size
    unsigned int size = this->args.size();
    if(size != e.getArgs().size()) {
        return false;
    }

    // check each arg
    for(unsigned int i = 0; i < size; i++) {
        if(this->args[i] != e.getArgs()[i]) {
            return false;
        }
    }
    return true;
}

// zero arg
const char * const TypeCheckException::E_Unresolved      = "having unresolved type";
const char * const TypeCheckException::E_InsideLoop      = "only available inside loop statement";
const char * const TypeCheckException::E_UnfoundReturn   = "not found return statement";
const char * const TypeCheckException::E_Unreachable     = "found unreachable code";
const char * const TypeCheckException::E_InsideFunc      = "only available inside function";
const char * const TypeCheckException::E_NotNeedExpr     = "not need expression";
const char * const TypeCheckException::E_Assignable      = "require assignable node";
const char * const TypeCheckException::E_ReadOnly        = "read only value";
const char * const TypeCheckException::E_InsideFinally   = "unavailable inside finally block";

// one arg
const char * const TypeCheckException::E_DefinedSymbol   = "already defined symbol: %s";
const char * const TypeCheckException::E_UndefinedSymbol = "undefined symbol: %s";
const char * const TypeCheckException::E_UndefinedField  = "undefined field: %s";
const char * const TypeCheckException::E_UndefinedMethod = "undefined method: %s";
const char * const TypeCheckException::E_UndefinedInit   = "undefined constructor: %s";
const char * const TypeCheckException::E_Unacceptable    = "unacceptable type: %s";
const char * const TypeCheckException::E_NoIterator      = "not support iterator: %s";
const char * const TypeCheckException::E_Unimplemented   = "unimplemented type checker api: %s";

// two arg
const char * const TypeCheckException::E_Required        = "require %s, but is %s";
const char * const TypeCheckException::E_CastOp          = "unsupported cast op: %s -> %s";
const char * const TypeCheckException::E_UnaryOp         = "undefined operator: %s %s";
const char * const TypeCheckException::E_UnmatchParam    = "not match parameter, require size is %s, but is %s";

// three arg
const char * const TypeCheckException::E_BinaryOp        = "undefined operator: %s %s %s";
