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

#include <cstdarg>
#include <string>
#include <cassert>

#include "MessageTemplate.h"

namespace ydsh {
namespace core {

// #############################
// ##     MessageTemplate     ##
// #############################

void MessageTemplate::formatImpl(std::string &str, unsigned int &index, const std::string &arg) const {
    for(; this->value[index] != '\0'; index++) {
        const char ch = this->value[index];
        if(ch == '%') {
            str += arg;
        } else {
            str += ch;
        }
    }
}

void MessageTemplate::formatImpl(std::string &str, unsigned int &index) const {
    std::string arg;
    this->formatImpl(str, index, arg);
}

} // namespace core
} // namespace ydsh

