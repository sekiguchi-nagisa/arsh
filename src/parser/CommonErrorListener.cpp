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

#include <stdio.h>

#include <parser/CommonErrorListener.h>

CommonErrorListener::CommonErrorListener() {
    // TODO Auto-generated constructor stub

}

CommonErrorListener::~CommonErrorListener() {
    // TODO Auto-generated destructor stub
}

void CommonErrorListener::displayTypeError(const std::string &sourceName,
        const TypeCheckError &e) {
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

void CommonErrorListener::displayParseError(const std::string &sourceName,
            const ParseError &e) {
    fprintf(stderr, "(%s):%d: [syntax error] %s\n");    //FIXME:

}
