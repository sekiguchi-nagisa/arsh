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

const static char *msgTable[] = {
#define GEN_MSG(ENUM, MSG) #MSG,
        EACH_TC_ERROR(GEN_MSG)
#undef GEN_MSG
};

// ############################
// ##     TypeCheckError     ##
// ############################

TypeCheckError::TypeCheckError(int lineNum, ErrorKind kind) :
        lineNum(lineNum), t(msgTable[kind]), args(0) {
}

TypeCheckError::TypeCheckError(int lineNum, ErrorKind kind, const std::string &arg1) :
        lineNum(lineNum), t(msgTable[kind]), args(1) {
    args.push_back(arg1);
}

TypeCheckError::TypeCheckError(int lineNum, ErrorKind kind, const std::string &arg1,
        const std::string &arg2) :
        lineNum(lineNum), t(msgTable[kind]), args(2) {
    args.push_back(arg1);
    args.push_back(arg2);
}

TypeCheckError::TypeCheckError(int lineNum, ErrorKind kind, const std::string &arg1,
        const std::string &arg2, const std::string &arg3) :
        lineNum(lineNum), t(msgTable[kind]), args(3) {
    args.push_back(arg1);
    args.push_back(arg2);
    args.push_back(arg3);
}

TypeCheckError::TypeCheckError(int lineNum, const TypeLookupError &e) :
        lineNum(lineNum), t(e.getTemplate()), args(e.getArgs()) {
}

TypeCheckError::~TypeCheckError() {
}

int TypeCheckError::getLineNum() const {
    return this->lineNum;
}

const std::string &TypeCheckError::getTemplate() const {
    return this->t;
}

const std::vector<std::string> &TypeCheckError::getArgs() const {
    return this->args;
}

bool TypeCheckError::operator==(const TypeCheckError &e) {
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
