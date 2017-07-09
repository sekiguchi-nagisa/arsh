/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#ifndef YDSH_MISC_ARGV_HPP
#define YDSH_MISC_ARGV_HPP

#include <cstring>
#include <type_traits>
#include <vector>
#include <algorithm>

#include "hash.hpp"
#include "flag_util.hpp"

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

constexpr unsigned int HAS_ARG     = 1 << 0;
constexpr unsigned int REQUIRE     = 1 << 1;
constexpr unsigned int IGNORE_REST = 1 << 2;

constexpr const char *usageSuffix = " <arg>";

template<typename T>
struct Option {
    static_assert(std::is_enum<T>::value, "must be enum type");

    T kind;
    const char *optionName;
    unsigned int flag;
    const char *detail;

    bool hasArg() const {
        return hasFlag(this->flag, HAS_ARG);
    }

    bool ignoreRest() const {
        return hasFlag(this->flag, IGNORE_REST);
    }

    bool require() const {
        return hasFlag(this->flag, REQUIRE);
    }

    unsigned int getUsageSize() const {
        return strlen(this->optionName) +
                (hasFlag(this->flag, HAS_ARG) ? strlen(usageSuffix) : 0);
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
                buf = "";
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

template<typename T>
using CmdLines = std::vector<std::pair<T, const char *>>;

/**
 * first element of argv is always ignored.
 * return start index of rest argument(< argc).
 * if not found rest argument, return argc.
 */
template<typename T, size_t N>
int parseArgv(int argc, char **argv, const Option<T> (&options)[N], CmdLines<T> &cmdLines) {
    // register option
    CStringHashMap<unsigned int> indexMap;
    bool requireOptionMap[N];
    for(unsigned int i = 0; i < N; i++) {
        const char *optionName = options[i].optionName;
        if(optionName[0] != '-') {
            throw ParseError("illegal option name", optionName);
        }
        if(!indexMap.insert(std::make_pair(optionName, i)).second) {
            throw ParseError("duplicated option", optionName);
        }
        requireOptionMap[i] = options[i].require();
    }

    // parse
    int index = 1;
    for(; index < argc; index++) {
        const char *arg = argv[index];
        if(arg[0] != '-') {
            break;
        }

        if(strcmp(arg, "-") == 0) {
            break;
        }
        if(strcmp(arg, "--") == 0) {
            index++;
            break;
        }

        auto iter = indexMap.find(arg);
        if(iter == indexMap.end()) {    // not found
            throw ParseError("illegal option", arg);
        }

        const unsigned int optionIndex = iter->second;
        const Option<T> &option = options[optionIndex];
        const char *optionArg = "";

        if(option.hasArg()) {
            if(index + 1 < argc && argv[++index][0] != '-') {
                optionArg = argv[index];
            } else {
                throw ParseError("need argument", arg);
            }
        }
        cmdLines.push_back(std::make_pair(option.kind, optionArg));
        if(requireOptionMap[optionIndex]) {
            requireOptionMap[optionIndex] = false;
        }

        if(option.ignoreRest()) {
            index++;
            break;
        }
    }

    // check require option
    for(unsigned int i = 0; i < N; i++) {
        if(requireOptionMap[i]) {
            throw ParseError("require option", options[i].optionName);
        }
    }
    return index;
};

template<typename T, size_t N>
void printOption(FILE *fp, const Option<T> (&options)[N]) {
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
    fputs("Options:", fp);
    for(const Option<T> *option : sortedOptions) {
        fputc('\n', fp);
        unsigned int size = option->getUsageSize();
        fprintf(fp, "    %s%s", option->optionName, (option->hasArg() ? usageSuffix : ""));
        for(unsigned int i = 0; i < maxSizeOfUsage - size; i++) {
            fputc(' ', fp);
        }

        std::vector<std::string> details(option->getDetails());
        unsigned int detailSize = details.size();
        for(unsigned int i = 0; i < detailSize; i++) {
            if(i > 0) {
                fprintf(fp, "\n%s    ", spaces.c_str());
            }
            fprintf(fp, "    %s", details[i].c_str());
        }
    }
    fputc('\n', fp);
    fflush(fp);
};

} // namespace argv
} // namespace ydsh

#endif //YDSH_MISC_ARGV_HPP
