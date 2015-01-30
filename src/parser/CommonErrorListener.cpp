/*
 * CommonErrorListener.cpp
 *
 *  Created on: 2015/01/30
 *      Author: skgchxngsxyz-osx
 */

#include <stdio.h>

#include <parser/CommonErrorListener.h>

CommonErrorListener::CommonErrorListener() {
    // TODO Auto-generated constructor stub

}

CommonErrorListener::~CommonErrorListener() {
    // TODO Auto-generated destructor stub
}

void CommonErrorListener::displayTypeError(const std::string &sourceName,
        const TypeCheckException &e) {
    int argSize = e.getArgs().size();
    int messageSize = e.getTemplate().size() + 1;
    for(const std::string &arg : e.getArgs()) {
        messageSize += arg.size();
    }

    // format error message
    char *strBuf = new char[messageSize];
    switch(argSize) {
    case 0:
        snprintf(strBuf, messageSize, e.getTemplate().c_str());
        break;
    case 1:
        snprintf(strBuf, messageSize, e.getTemplate().c_str(), e.getArgs()[0].c_str());
        break;
    case 2:
        snprintf(strBuf, messageSize, e.getTemplate().c_str(),
                e.getArgs()[0].c_str(), e.getArgs()[1].c_str());
        break;
    case 3:
        snprintf(strBuf, messageSize, e.getTemplate().c_str(),
                e.getArgs()[0].c_str(), e.getArgs()[1].c_str(), e.getArgs()[2].c_str());
        break;
    default:
        snprintf(strBuf, messageSize, "!!broken args!!");
        break;
    }

    fprintf(stderr, "(%s):%d: [semantic error] %s\n", sourceName.c_str(), e.getLineNum(), strBuf);
    delete[] strBuf;
}
