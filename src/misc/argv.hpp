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

#ifndef YDSH_ARGV_HPP
#define YDSH_ARGV_HPP

#include <cstring>
#include <type_traits>
#include <vector>
#include <algorithm>

#include "hash.hpp"
#include "flag_util.h"

namespace ydsh {
namespace argv {

class ParseError {
private:
    std::string message;

public:
    ParseError(const char *message, const char *suffix) :
            message(message) {
        this->message += ": ";
        this->message += suffix;
    }

    ~ParseError() = default;

    const std::string &getMessage() const {
        return this->message;
    }
};

constexpr unsigned int REQUIRE_ARG = 1 << 0;
constexpr unsigned int IGNORE_REST = 1 << 1;

constexpr char usageSuffix[] = " <arg>";

template<typename T>
struct Option {
    static_assert(std::is_enum<T>::value, "must be enum type");

    T kind;
    const char *optionName;
    unsigned int flag;
    const char *detail;

    bool requireArg() const {
        return misc::hasFlag(this->flag, REQUIRE_ARG);
    }

    bool ignoreRest() const {
        return misc::hasFlag(this->flag, IGNORE_REST);
    }

    unsigned int getUsageSize() const {
        return strlen(this->optionName) +
               (misc::hasFlag(this->flag, REQUIRE_ARG) ? sizeof(usageSuffix) - 1 : 0);
    }

    std::vector<std::string> getDetails() const;
};

template<typename T>
std::vector<std::string> Option<T>::getDetails() const {
    std::vector<std::string> bufs;
    std::string buf;
    for(unsigned int i = 0; this->detail[i] != '\0'; i++) {
        char ch = this->detail[i];
        if(ch == '\n') {
            if(!buf.empty()) {
                bufs.push_back(std::move(buf));
                buf.clear();
            }
        } else {
            buf += ch;
        }
    }
    if(!buf.empty()) {
        bufs.push_back(std::move(buf));
    }
    return bufs;
}

typedef std::vector<const char *> RestArgs;

template<typename T>
using CmdLines = std::vector<std::pair<T, const char *>>;

/**
 * first element of argv is always ignored.
 */
template<typename T, size_t N>
RestArgs parseArgv(int argc, char **argv, const Option<T> (&options)[N], CmdLines<T> &cmdLines) {
    // register option
    misc::CStringHashMap<unsigned int> indexMap;
    for(unsigned int i = 0; i < N; i++) {
        const char *optionName = options[i].optionName;
        if(optionName[0] != '-') {
            throw ParseError("illegal option name", optionName);
        }
        if(!indexMap.insert(std::make_pair(optionName, i)).second) {
            throw ParseError("duplicated option", optionName);
        }
    }

    // parse
    static char empty[] = "";
    RestArgs restArgs;

    int index = 1;
    for(; index < argc; index++) {
        const char *arg = argv[index];
        if(arg[0] != '-') {
            break;
        }

        auto iter = indexMap.find(arg);
        if(iter == indexMap.end()) {    // not found
            throw ParseError("illegal option", arg);
        }

        const Option<T> &option = options[iter->second];
        const char *optionArg = empty;

        if(option.requireArg()) {
            if(index + 1 < argc && argv[++index][0] != '-') {
                optionArg = argv[index];
            } else {
                throw ParseError("need argument", arg);
            }
        }
        cmdLines.push_back(std::make_pair(option.kind, optionArg));

        if(option.ignoreRest()) {
            index++;
            break;
        }
    }

    // get rest argument
    for(; index < argc; index++) {
        restArgs.push_back(argv[index]);
    }

    return restArgs;
};

template<typename T, size_t N>
std::ostream &operator<<(std::ostream &stream, const Option<T> (&options)[N]) {
    std::vector<const Option<T> *> sortedOptions;
    for(unsigned int i = 0; i < N; i++) {
        sortedOptions.push_back(&options[i]);
    }

    std::sort(sortedOptions.begin(), sortedOptions.end(), [](const Option<T> *x, const Option<T> *y) {
        return strcmp(x->optionName, y->optionName) < 0;
    });

    unsigned int maxSizeOfUsage = 0;

    // compute usage size
    for(auto &option : sortedOptions) {
        unsigned int size = option->getUsageSize();
        if(size > maxSizeOfUsage) {
            maxSizeOfUsage = size;
        }
    }

    std::string spaces;
    for(unsigned int i = 0; i < maxSizeOfUsage; i++) {
        spaces += ' ';
    }

    // print help message
    stream << "Options:";
    for(const Option<T> *option : sortedOptions) {
        stream << std::endl;
        unsigned int size = option->getUsageSize();
        stream << "    " << option->optionName;
        stream << (option->requireArg() ? usageSuffix : "");
        for(unsigned int i = 0; i < maxSizeOfUsage - size; i++) {
            stream << ' ';
        }

        std::vector<std::string> details(option->getDetails());
        unsigned int detailSize = details.size();
        for(unsigned int i = 0; i < detailSize; i++) {
            if(i > 0) {
                stream << std::endl << spaces << "    ";
            }
            stream << "    " << details[i];
        }
    }

    return stream;
};

} // namespace argv
} // namespace ydsh

#endif //YDSH_ARGV_HPP
