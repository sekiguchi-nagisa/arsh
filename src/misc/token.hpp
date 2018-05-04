/*
 * Copyright (C) 2017 Nagisa Sekiguchi
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

#ifndef YDSH_TOKEN_HPP
#define YDSH_TOKEN_HPP

#include <string>

namespace ydsh {

struct Token {
    unsigned int pos;
    unsigned int size;

    bool operator==(const Token &token) const {
        return this->pos == token.pos && this->size == token.size;
    }

    bool operator!=(const Token &token) const {
        return !(*this == token);
    }
};

inline std::string toString(Token token) {
    std::string str = "(pos = ";
    str += std::to_string(token.pos);
    str += ", size = ";
    str += std::to_string(token.size);
    str += ")";
    return str;
}

} // namespace

#endif //YDSH_TOKEN_HPP
