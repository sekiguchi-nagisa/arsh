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

#include "../ast/Node.h"

#define IN_SRC_FILE
#include "TypeCheckError.h"


namespace ydsh {
namespace parser {

// ###########################
// ##     ErrorMessage0     ##
// ###########################

void ErrorMessage0::operator()(ast::Node *node) const throw(TypeCheckError) {
    throw TypeCheckError(node->getLineNum(), this->kind, std::string(this->msgPart1));
}

// ###########################
// ##     ErrorMessage1     ##
// ###########################

void ErrorMessage1::operator()(ast::Node *node, const std::string &arg1) const throw(TypeCheckError) {
    std::string str(this->msgPart1);
    str += arg1;
    throw TypeCheckError(node->getLineNum(), this->kind, std::move(str));
}

// ###########################
// ##     ErrorMessage2     ##
// ###########################

void ErrorMessage2::operator()(ast::Node *node, const std::string &arg1,
                               const std::string &arg2) const throw(TypeCheckError) {
    std::string str(this->msgPart1);
    str += arg1;
    str += this->msgPart2;
    str += arg2;
    throw TypeCheckError(node->getLineNum(), this->kind, std::move(str));
}

// ###########################
// ##     ErrorMessage3     ##
// ###########################

void ErrorMessage3::operator()(ast::Node *node, const std::string &arg1,
                               const std::string &arg2, const std::string &arg3) const throw(TypeCheckError) {
    std::string str(this->msgPart1);
    str += arg1;
    str += this->msgPart2;
    str += arg2;
    str += this->msgPart3;
    str += arg3;
    throw TypeCheckError(node->getLineNum(), this->kind, std::move(str));
}

#undef IN_SRC_FILE

} // namespace parser
} // namespace ydsh
