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

#include <core/TypeLookupError.h>

const static char *msgTable[] = {
#define GEN_MSG(ENUM, MSG) #MSG,
        EACH_TL_ERROR(GEN_MSG)
#undef GEN_MSG
};

// #############################
// ##     TypeLookupError     ##
// #############################

TypeLookupError::TypeLookupError(ErrorKind kind, const std::string &arg1) :
        messageTemplate(msgTable[kind]), args() {
    this->args.push_back(arg1);
}

TypeLookupError::TypeLookupError(ErrorKind kind, const std::string &arg1,
        const std::string &arg2, const std::string &arg3) :
        messageTemplate(msgTable[kind]), args() {
    this->args.push_back(arg1);
    this->args.push_back(arg2);
    this->args.push_back(arg3);
}

const std::string &TypeLookupError::getTemplate() const {
    return this->messageTemplate;
}

const std::vector<std::string> &TypeLookupError::getArgs() const {
    return this->args;
}

bool TypeLookupError::operator==(const TypeLookupError &e) {
    // check template
    if(this->messageTemplate != e.getTemplate()) {
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
