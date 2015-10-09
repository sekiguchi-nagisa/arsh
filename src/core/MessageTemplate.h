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

#ifndef YDSH_MESSAGETEMPLATE_H
#define YDSH_MESSAGETEMPLATE_H

#include <stdexcept>

namespace ydsh {
namespace core {

class MessageTemplate {
protected:
    const char *kind;
    const char *value;

public:
    MessageTemplate() = delete;
    MessageTemplate(const MessageTemplate &o) = delete;
    MessageTemplate(MessageTemplate &&o) = delete;

    ~MessageTemplate() = default;

    constexpr MessageTemplate(const char *kind, const char *value) :
            kind(kind), value(value) { }

    const char *getKind() const {
        return this->kind;
    }

    const char *getValue() const {
        return this->value;
    }

    void formatImpl(std::string &str, unsigned int &index, const std::string &arg) const;
    void formatImpl(std::string &str, unsigned int &index) const;

    void formatNext(std::string &str, unsigned int &index) const {
        this->formatImpl(str, index);
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

class StringWrapper {
private:
    const char *const str;
    const std::size_t _size; // not contains terminate char

public:
    template <std::size_t N>
    constexpr StringWrapper(const char (&text)[N]) : str(text), _size(N - 1) {}

    constexpr char operator[](std::size_t index) const {
        return index < this->_size ? str[index] : throw std::out_of_range("");
    }

    constexpr std::size_t size() const {
        return this->_size;
    }
};

constexpr unsigned int computeParamSize(StringWrapper s, unsigned int index = 0) {
    return index >= s.size() ? 0 :
           (s[index] == '%' ? 1 : 0) + computeParamSize(s, index + 1);

}


} // namespace core
} // namespace ydsh

#endif //YDSH_MESSAGETEMPLATE_H
