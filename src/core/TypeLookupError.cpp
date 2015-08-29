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

#define IN_SRC_FILE
#include "TypeLookupError.h"

namespace ydsh {
namespace core {

// ###########################
// ##     ErrorMessage0     ##
// ###########################

void ErrorMessage0::operator()() const throw(TypeLookupError) {
    throw TypeLookupError(this->kind, std::string(this->msgPart1));
}

// ###########################
// ##     ErrorMessage1     ##
// ###########################

void ErrorMessage1::operator()(const std::string &arg1) const throw(TypeLookupError) {
    std::string str(this->msgPart1);
    str += arg1;
    throw TypeLookupError(this->kind, std::move(str));
}

// ###########################
// ##     ErrorMessage3     ##
// ###########################

void ErrorMessage3::operator()(const std::string &arg1, const std::string &arg2,
                const std::string &arg3) const throw(TypeLookupError) {
    std::string str(this->msgPart1);
    str += arg1;
    str += this->msgPart2;
    str += arg2;
    str += this->msgPart3;
    str += arg3;
    throw TypeLookupError(this->kind, std::move(str));
}

#undef IN_SRC_FILE

} // namespace core
} // namespace ydsh