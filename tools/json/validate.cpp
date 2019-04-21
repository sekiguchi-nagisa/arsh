/*
 * Copyright (C) 2018-2019 Nagisa Sekiguchi
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

#include "jsonrpc.h"

namespace ydsh {
namespace json {

// #######################
// ##     Validator     ##
// #######################

std::string Validator::formatError() const {
    std::string str;
    if(!this->errors.empty()) {
        str = this->errors.back();
        for(auto iter = this->errors.rbegin() + 1; iter != this->errors.rend(); ++iter) {
            str += "\n    from: ";
            str += *iter;
        }
    }
    return str;
}

} // namespace json
} // namespace ydsh