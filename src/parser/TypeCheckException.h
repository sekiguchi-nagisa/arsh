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

#ifndef PARSER_TYPECHECKEXCEPTION_H_
#define PARSER_TYPECHECKEXCEPTION_H_

#include <string>
#include <vector>

class TypeError;
class TypeErrorZeroArg;
class TypeErrorOneArg;
class TypeErrorTwoArg;
class TypeErrorThreeArg;

/**
 * for type error reporting
 */
class TypeCheckException {
protected:
    /**
     * line number of error node
     */
    int lineNum;

private:
    /**
     * template of error message
     */
    std::string t;

    /**
     * message arguments
     */
    std::vector<std::string> args;

public:
    TypeCheckException(int lineNum, const std::string &t);
    TypeCheckException(int lineNum, const std::string &t, const std::string &arg1);
    TypeCheckException(int lineNum, const std::string &t, const std::string &arg1,
            const std::string &arg2);
    TypeCheckException(int lineNum, const std::string &t, const std::string &arg1,
            const std::string &arg2, const std::string &arg3);
    virtual ~TypeCheckException();
};

class TypeLookupException : public TypeCheckException {
public:
    TypeLookupException(const std::string &t, const std::string &arg1);
    TypeLookupException(const std::string &t, const std::string &arg1, const std::string &arg2, const std::string &arg3);

    void setLineNum(int lineNum);
};

#endif /* PARSER_TYPECHECKEXCEPTION_H_ */
