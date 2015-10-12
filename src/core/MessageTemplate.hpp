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

#ifndef YDSH_MESSAGETEMPLATE_HPP
#define YDSH_MESSAGETEMPLATE_HPP

#include <stdexcept>
#include <string>

namespace ydsh {
namespace core {

class MessageTemplate {
protected:
    const char *kind;
    const char *value;

public:
    constexpr MessageTemplate(const char *kind, const char *value) :
            kind(kind), value(value) { }

    const char *getKind() const {
        return this->kind;
    }

    const char *getValue() const {
        return this->value;
    }

    void formatImpl(std::string &str, unsigned int &index, const std::string &arg) const {
        for(; this->value[index] != '\0'; index++) {
            const char ch = this->value[index];
            if(ch == '%') {
                str += arg;
                index++;
                break;
            } else {
                str += ch;
            }
        }
    }

    void formatNext(std::string &str, unsigned int &index) const {
        std::string arg;
        this->formatImpl(str, index, arg);
    }

    template <typename ... T>
    void formatNext(std::string &str, unsigned int &index, const std::string &arg, T && ... args) const {
        this->formatImpl(str, index, arg);
        this->formatNext(str, index, std::forward<T &&>(args)...);
    }

    template <typename ... T>
    std::string format(T && ... args) const {
        unsigned int index = 0;
        std::string str;
        this->formatNext(str, index, std::forward<T &&>(args)...);
        return str;
    }
};

constexpr unsigned int computeParamSize(const char *s, unsigned int index = 0) {
    return s[index] == '\0' ? 0 :
           (s[index] == '%' ? 1 : 0) + computeParamSize(s, index + 1);
}


} // namespace core
} // namespace ydsh

#endif //YDSH_MESSAGETEMPLATE_HPP
