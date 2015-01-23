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

#include <parser/TypeCheckException.h>
#include <parser/TypeError.h>

#include <utility>


// ################################
// ##     TypeCheckException     ##
// ################################

TypeCheckException::TypeCheckException(int lineNum, const std::string &t) :
        lineNum(lineNum), t(t), args(0) {
}

TypeCheckException::TypeCheckException(int lineNum, const std::string &t, const std::string &arg1) :
        lineNum(lineNum), t(t), args(1) {
    args.push_back(arg1);
}

TypeCheckException::TypeCheckException(int lineNum, const std::string &t, const std::string &arg1,
        const std::string &arg2) :
        lineNum(lineNum), t(t), args(2) {
    args.push_back(arg1);
    args.push_back(arg2);
}

TypeCheckException::TypeCheckException(int lineNum, const std::string &t, const std::string &arg1,
        const std::string &arg2, const std::string &arg3) :
        lineNum(lineNum), t(t), args(3) {
    args.push_back(arg1);
    args.push_back(arg2);
    args.push_back(arg3);
}

TypeCheckException::~TypeCheckException() {
}


// #################################
// ##     TypeLookupException     ##
// #################################

TypeLookupException::TypeLookupException(const std::string &t, const std::string &arg1) :
        TypeCheckException(-1, t, arg1) {
}

TypeLookupException::TypeLookupException(const std::string &t, const std::string &arg1,
        const std::string &arg2, const std::string &arg3) :
        TypeCheckException(-1, t, arg1, arg2, arg3) {
}

void TypeLookupException::setLineNum(int lineNum) {
    this->lineNum = lineNum;
}
