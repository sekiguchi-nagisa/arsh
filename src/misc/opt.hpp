/*
 * Copyright (C) 2018 Nagisa Sekiguchi
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

#ifndef YDSH_OPT_HPP
#define YDSH_OPT_HPP

#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#include "flag_util.hpp"

namespace ydsh {
namespace opt {

enum OptionFlag : unsigned char {
    NO_ARG,
    HAS_ARG,    // require additional argument
    OPT_ARG,    // may have additional argument
};

constexpr const char *getOptSuffix(OptionFlag flag) {
    return flag == NO_ARG ? "" :
           flag == HAS_ARG ? " arg": "[=arg]";
}

template<typename T>
struct Option {
    static_assert(std::is_enum<T>::value, "must be enum type");

    T kind;
    const char *optionName;
    OptionFlag flag;
    const char *detail;

    unsigned int getUsageSize() const {
        return strlen(this->optionName) + strlen(getOptSuffix(this->flag));
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

enum Error : unsigned char {
    UNRECOG,
    NEED_ARG,
    END,
};

template <typename T>
class Parser;

template <typename T>
class Result {
private:
    bool ok;
    union {
        Error error_;
        T value_;
    };

    const char *recog_;
    const char *arg_;

    friend class Parser<T>;

public:
    Result(T value, const char *recog) :
            ok(true), value_(value), recog_(recog), arg_(nullptr) {}

    Result(Error error, const char *unrecog) :
            ok(false), error_(error), recog_(unrecog), arg_(nullptr) {}

    Result() : Result(END, nullptr) {}

    static Result unrecog(const char *unrecog) {
        return Result(UNRECOG, unrecog);
    }

    static Result needArg(const char *recog) {
        return Result(NEED_ARG, recog);
    }

    static Result end(const char *opt = nullptr) {
        return Result(END, opt);
    }

    const char *recog() const {
        return this->recog_;
    }

    const char *arg() const {
        return this->arg_;
    }

    explicit operator bool() const {
        return this->ok;
    }

    Error error() const {
        return this->error_;
    }

    T value() const {
        return this->value_;
    }

    std::string formatError() const {
        std::string str;
        if(!(*this)) {
            if(this->error() == UNRECOG) {
                str += "invalid option: ";
            } else if(this->error() == NEED_ARG) {
                str += "need argument: ";
            }
        }
        str += this->recog();
        return str;
    }
};

template <typename T>
class Parser {
private:
    std::vector<Option<T>> options;

public:
    Parser(std::initializer_list<Option<T>> list);
    ~Parser() = default;

    void printOption(FILE *fp) const;

    template <typename Iter>
    Result<T> operator()(Iter &begin, Iter end) const;
};

// ####################
// ##     Parser     ##
// ####################

template <typename T>
Parser<T>::Parser(std::initializer_list<Option<T>> list) :
        options(list.size()) {
    // init options
    unsigned int count = 0;
    for(auto &e : list) {
        this->options[count++] = e;
    }
    std::sort(this->options.begin(), this->options.end(), [](const Option<T> &x, const Option<T> &y) {
        return strcmp(x.optionName, y.optionName) < 0;
    });
}

template <typename T>
void Parser<T>::printOption(FILE *fp) const {
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
        fprintf(fp, "    %s%s", option.optionName, getOptSuffix(option.flag));
        for(unsigned int i = 0; i < maxSizeOfUsage - size; i++) {
            fputc(' ', fp);
        }

        unsigned int count = 0;
        for(auto &detail : option.getDetails()) {
            if(count++ > 0) {
                fprintf(fp, "\n%s    ", spaces.c_str());
            }
            fprintf(fp, "    %s", detail.c_str());
        }
    }
    fputc('\n', fp);
    fflush(fp);
}

template <typename T>
template <typename Iter>
Result<T> Parser<T>::operator()(Iter &begin, Iter end) const {
    if(begin == end) {
        return Result<T>::end();
    }

    const char *haystack = *begin;
    if(haystack[0] != '-' || strcmp(haystack, "-") == 0) {
        return Result<T>::end(haystack);
    }
    if(strcmp(haystack, "--") == 0) {
        ++begin;
        return Result<T>::end(haystack);
    }

    // check options
    unsigned int haystackSize = strlen(haystack);
    for(auto &option : this->options) {
        const char *needle = option.optionName;
        unsigned int needleSize = strlen(needle);
        if(needleSize > haystackSize || memcmp(needle, haystack, needleSize) != 0) {
            continue;
        }
        const char *cursor = haystack + needleSize;
        auto result = Result<T>(option.kind, haystack);
        if(*cursor == '\0') {
            if(option.flag == OptionFlag::HAS_ARG) {
                if(begin + 1 == end) {
                    return Result<T>::needArg(haystack);
                }
                ++begin;
                result.arg_ = *begin;
            }
            ++begin;
            return result;
        } else if(option.flag == OptionFlag::OPT_ARG) {
            if(*cursor != '=') {
                break;
            }
            result.arg_ = ++cursor;
            ++begin;
            return result;
        }
    }
    return Result<T>::unrecog(haystack);
}

// getopt like command line option parser
struct GetOptState {
    /**
     * currently processed argument.
     */
    const char *nextChar{nullptr};

    /**
     * may be null, if has no optional argument.
     */
    const char *optArg{nullptr};

    /**
     * unrecognized option.
     */
    int optOpt{0};

    void reset() {
        this->nextChar = nullptr;
        this->optArg = nullptr;
        this->optOpt = 0;
    }

    /**
     *
     * @tparam Iter
     * @param begin
     * after succeed, may indicate next option.
     * @param end
     * @param optStr
     * @return
     * recognized option.
     * if reach `end' or not match any option (not starts with '-'), return -1.
     * if match '--', increment `begin' and return -1.
     * if not match additional argument, return ':' and `begin' may indicates next option.
     * if match unrecognized option, return '?' and `begin' indicate current option (no increment).
     */
    template <typename Iter>
    int operator()(Iter &begin, Iter end, const char *optStr);
};

template <typename Iter>
int GetOptState::operator()(Iter &begin, Iter end, const char *optStr) {
    // reset previous state
    this->optArg = nullptr;
    this->optOpt = 0;

    if(begin == end) {
        this->nextChar = nullptr;
        return -1;
    }

    const char *arg = *begin;
    if(*arg != '-' || strcmp(arg, "-") == 0) {
        this->nextChar = nullptr;
        return -1;
    }

    if(strcmp(arg, "--") == 0) {
        this->nextChar = nullptr;
        ++begin;
        return -1;
    }

    if(this->nextChar == nullptr || *this->nextChar == '\0') {
        this->nextChar = arg + 1;
    }

    const char *ptr = strchr(optStr, *this->nextChar);
    if(ptr != nullptr) {
        if(*(ptr + 1) == ':') {
            if(++begin == end) {
                this->optOpt = *ptr;
                return ':';
            }
            this->optArg = *begin;
            this->nextChar = nullptr;
        }

        if(this->nextChar == nullptr || *(++this->nextChar) == '\0') {
            ++begin;
        }
        return *ptr;
    }
    this->optOpt = *this->nextChar;
    return '?';
}

} // namespace opt
} // namespace ydsh

#endif //YDSH_OPT_HPP
