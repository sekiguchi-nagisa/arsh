/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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
#include <initializer_list>
#include <string>

#include "hash.hpp"
#include "flag_util.hpp"

namespace ydsh {
namespace argv {

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
    bufs.emplace_back();
    for(unsigned int i = 0; this->detail[i] != '\0'; i++) {
        char ch = this->detail[i];
        if(ch == '\n') {
            if(!bufs.back().empty()) {
                bufs.emplace_back();
            }
        } else {
            bufs.back() += ch;
        }
    }
    return bufs;
}

template<typename T>
using CmdLines = std::vector<std::pair<T, const char *>>;

template <typename T>
class ArgvParser {
private:
    std::vector<Option<T>> options;
    CStringHashMap<unsigned int> indexMap;
    std::string errorMessage;

public:
    ArgvParser(std::initializer_list<Option<T>> list);
    ~ArgvParser() = default;

    const char *getErrorMessage() const {
        return this->errorMessage.c_str();
    }

    bool hasError() const {
        return this->errorMessage.size() > 0;
    }

    int operator()(int argc, char **argv, CmdLines<T> &cmdLines);

    void printOption(FILE *fp) const;

private:
    void newError(const char *message, const char *suffix);
};

// ########################
// ##     ArgvParser     ##
// ########################

template <typename T>
ArgvParser<T>::ArgvParser(std::initializer_list<Option<T>> list) :
        options(list.size()), indexMap(), errorMessage() {
    // init options
    unsigned int count = 0;
    for(auto &e : list) {
        this->options[count++] = e;
    }
    std::sort(this->options.begin(), this->options.end(), [](const Option<T> &x, const Option<T> &y) {
        return strcmp(x.optionName, y.optionName) < 0;
    });

    // register option
    for(unsigned int i = 0; i < list.size(); i++) {
        const char *optionName = this->options[i].optionName;
        if(optionName[0] != '-') {
            this->newError("illegal option name", optionName);
            return;
        }
        if(!indexMap.insert(std::make_pair(optionName, i)).second) {
            this->newError("duplicated option", optionName);
            return;
        }
    }
}

template <typename T>
int ArgvParser<T>::operator()(int argc, char **argv, CmdLines<T> &cmdLines) {
    this->errorMessage.clear();
    unsigned int size = this->options.size();
    bool requireOptionMap[size];
    for(unsigned int i = 0; i < size; i++) {
        requireOptionMap[i] = this->options[i].require();
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

        auto iter = this->indexMap.find(arg);
        if(iter == this->indexMap.end()) {    // not found
            this->newError("illegal option", arg);
            return -1;
        }

        const unsigned int optionIndex = iter->second;
        const Option<T> &option = this->options[optionIndex];
        const char *optionArg = "";

        if(option.hasArg()) {
            if(index + 1 < argc && argv[++index][0] != '-') {
                optionArg = argv[index];
            } else {
                this->newError("need argument", arg);
                return -1;
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
    for(unsigned int i = 0; i < size; i++) {
        if(requireOptionMap[i]) {
            this->newError("require option", options[i].optionName);
            return -1;
        }
    }
    return index;
}

template <typename T>
void ArgvParser<T>::printOption(FILE *fp) const {
    unsigned int maxSizeOfUsage = 0;

    // compute usage size
    for(auto &option : this->options) {
        unsigned int size = option.getUsageSize();
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
    for(auto &option : this->options) {
        fputc('\n', fp);
        unsigned int size = option.getUsageSize();
        fprintf(fp, "    %s%s", option.optionName, (option.hasArg() ? usageSuffix : ""));
        for(unsigned int i = 0; i < maxSizeOfUsage - size; i++) {
            fputc(' ', fp);
        }

        std::vector<std::string> details(option.getDetails());
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
}

template <typename T>
void ArgvParser<T>::newError(const char *message, const char *suffix) {
    this->errorMessage = message;
    this->errorMessage += ": ";
    this->errorMessage += suffix;
}

} // namespace argv
} // namespace ydsh

#endif //YDSH_MISC_ARGV_HPP
