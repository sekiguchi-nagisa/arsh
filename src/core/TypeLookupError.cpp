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


// #################################
// ##     TypeLookupException     ##
// #################################

TypeLookupException::TypeLookupException(const char t[], const std::string &arg1) :
        messageTemplate(t), args() {
    this->args.push_back(arg1);
}

TypeLookupException::TypeLookupException(const char t[], const std::string &arg1,
        const std::string &arg2, const std::string &arg3) :
        messageTemplate(t), args() {
    this->args.push_back(arg1);
    this->args.push_back(arg2);
    this->args.push_back(arg3);
}

const std::string &TypeLookupException::getTemplate() const {
    return this->messageTemplate;
}

const std::vector<std::string> &TypeLookupException::getArgs() const {
    return this->args;
}

bool TypeLookupException::operator==(const TypeLookupException &e) {
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

const char * const TypeLookupException::E_NotUseGeneric  = "not directly use generic base type: %s";
const char * const TypeLookupException::E_UndefinedType  = "undefined type: %s";
const char * const TypeLookupException::E_NotGenericBase = "not generic base type: %s";
const char * const TypeLookupException::E_NotPrimitive   = "not primitive type: %s";
const char * const TypeLookupException::E_NotClass       = "not class type: %s";
const char * const TypeLookupException::E_Nonheritable   = "nonheritable type: %s";
const char * const TypeLookupException::E_DefinedType    = "already defined type: %s";

const char * const TypeLookupException::E_UnmatchElement = "not match type element, %s requires %s type element, but is %s";
