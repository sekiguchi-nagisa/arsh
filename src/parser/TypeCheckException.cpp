/*
 * TypeCheckException.cpp
 *
 *  Created on: 2015/01/08
 *      Author: skgchxngsxyz-osx
 */

#include <utility>

#include "TypeError.h"
#include "TypeCheckException.h"

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

TypeCheckException::TypeCheckException(int lineNum, const std::string &t,
        const std::string &arg1, const std::string &arg2, const std::string &arg3) :
        lineNum(lineNum), t(t), args(3) {
    args.push_back(arg1);
    args.push_back(arg2);
    args.push_back(arg3);
}

TypeCheckException::~TypeCheckException() {
}

