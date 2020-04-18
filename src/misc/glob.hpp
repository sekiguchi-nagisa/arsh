/*
 * Copyright (C) 2020 Nagisa Sekiguchi
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

#ifndef YDSH_MISC_GLOB_HPP
#define YDSH_MISC_GLOB_HPP

#include <dirent.h>

#include <string>
#include <vector>

#include "files.h"
#include "flag_util.hpp"

namespace ydsh {

enum class WildMatchOption {
    DOTGLOB = 1u << 0u, // match file names start with '.'
};

template <> struct allow_enum_bitop<WildMatchOption> : std::true_type {};

enum class WildMatchResult {
    FAILED,     // match failed
    DOT,        // match '.'
    DOTDOT,     // match '..'
    MATCHED,    // match pattern
};

/**
 *
 * @tparam Meta
 * detect meta character `?`, `*`
 * @tparam Iter
 * pattern iterator
 */
template <typename Meta, typename Iter>
class WildCardMatcher {
private:
    Iter iter;
    Iter end;

    WildMatchOption option;

public:
    WildCardMatcher(Iter begin, Iter end, WildMatchOption option) :
            iter(begin), end(end), option(option) {}

    Iter getIter() const {
        return this->iter;
    }

    bool isEnd() const {
        return this->iter == this->end;
    }

    unsigned int consumeSep() {
        unsigned int count = 0;
        for(; !this->isEnd() && *this->iter == '/'; ++this->iter) {
            count++;
        }
        return count;
    }

    /**
     * based on (https://www.codeproject.com/Articles/1088/Wildcard-string-compare-globbing)
     *
     * @param name
     * must be null terminated
     * @return
     */
    WildMatchResult operator()(const char *name) {
        const char *cp = nullptr;
        auto oldIter = this->end;

        // ignore starting with '.'
        if(*name == '.') {
            if(!this->isEndOrSep() && *this->iter != '.') {
                if(!hasFlag(this->option, WildMatchOption::DOTGLOB)) {
                    return WildMatchResult::FAILED;
                }
            }
        }

        while(*name) {
            if(this->isEndOrSep()) {
                return WildMatchResult::FAILED;
            } else if(this->matchDot()) {
                return WildMatchResult::DOT;
            } else if(this->matchDotDot()) {
                return WildMatchResult::DOTDOT;
            } else if(Meta::isZeroOrMore(this->iter)) {
                break;
            } else if(*name != *this->iter && !Meta::isAny(this->iter)) {
                return WildMatchResult::FAILED;
            }
            ++name;
            ++this->iter;
        }

        while(*name) {
            if(this->isEndOrSep()) {
                this->iter = oldIter;
                name = cp++;
            } else if(*name == *this->iter || Meta::isAny(this->iter)) {
                ++name;
                ++this->iter;
            } else if(Meta::isZeroOrMore(this->iter)) {
                ++this->iter;
                if(this->isEndOrSep()) {
                    return WildMatchResult::MATCHED;
                }
                oldIter = this->iter;
                cp = name + 1;
            } else {
                this->iter = oldIter;
                name = cp++;
            }
        }

        for(; !this->isEndOrSep() && Meta::isZeroOrMore(this->iter); this->iter++);
        return this->isEndOrSep() ? WildMatchResult::MATCHED : WildMatchResult::FAILED;
    }

private:
    bool isEndOrSep() const {
        return this->isEnd() || *this->iter == '/';
    }

    bool matchDot() {
        auto old = this->iter;
        if(*this->iter == '.') {
            ++this->iter;
            if(this->isEndOrSep()) {
                return true;
            }
        }
        this->iter = old;
        return false;
    }

    bool matchDotDot() {
        auto old = this->iter;
        if(*this->iter == '.') {
            ++this->iter;
            if(*this->iter == '.') {
                ++this->iter;
                if(this->isEndOrSep()) {
                    return true;
                }
            }
        }
        this->iter = old;
        return false;
    }
};

template <typename Meta, typename Iter>
inline auto createWildCardMatcher(Iter begin, Iter end, WildMatchOption option) {
    return WildCardMatcher<Meta, Iter>(begin, end, option);
}

template <typename Meta, typename Iter, typename Appender>
unsigned int globBase(const char *baseDir, Iter iter, Iter end,
                            Appender &appender, WildMatchOption option) {
    DIR *dir = opendir(baseDir);
    if(!dir) {
        return 0;
    }

    unsigned int matchCount = 0;
    for(dirent *entry; (entry = readdir(dir)); ) {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        auto matcher = createWildCardMatcher<Meta>(iter, end, option);
        const WildMatchResult ret = matcher(entry->d_name);
        if(ret == WildMatchResult::FAILED) {
            continue;
        }

        std::string name = strcmp(baseDir, ".") != 0 ? baseDir : "";
        if(!name.empty() && name.back() != '/') {
            name += '/';
        }
        switch(ret) {
        case WildMatchResult::DOT:
            name += ".";
            break;
        case WildMatchResult::DOTDOT:
            name += "..";
            break;
        default:
            name += entry->d_name;
            break;
        }

        if(S_ISDIR(getStMode(name.c_str()))) {
            if(matcher.consumeSep() > 0) {
                name += '/';
            }
            if(!matcher.isEnd()) {
                matchCount += globBase<Meta>(name.c_str(), matcher.getIter(), end, appender, option);
            }
        }
        if(matcher.isEnd()) {
            appender(std::move(name));
            matchCount++;
        }

        if(ret == WildMatchResult::DOT || ret == WildMatchResult::DOTDOT) {
            break;
        }
    }
    closedir(dir);
    return matchCount;
}

} // namespace ydsh

#endif //YDSH_MISC_GLOB_HPP
