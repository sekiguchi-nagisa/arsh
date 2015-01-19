/*
 * TypeCheckException.h
 *
 *  Created on: 2015/01/08
 *      Author: skgchxngsxyz-osx
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
