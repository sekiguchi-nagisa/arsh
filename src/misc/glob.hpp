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

#include "files.h"
#include "flag_util.hpp"

namespace ydsh {

enum class WildMatchOption {
    TILDE          = 1u << 0u,  // apply tilde expansion before globbing
    DOTGLOB        = 1u << 1u,  // match file names start with '.'
    IGNORE_SYS_DIR = 1u << 2u,  // ignore system directory (/dev, /proc, /sys)
};

template <> struct allow_enum_bitop<WildMatchOption> : std::true_type {};

enum class WildMatchResult {
    FAILED,     // match failed
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

    unsigned int consumeSeps() {
        unsigned int count = 0;
        for(; !this->isEnd() && *this->iter == '/'; ++this->iter) {
            count++;
        }
        return count;
    }

    bool consumeDot() {
        auto old = this->iter;
        if(!this->isEnd() && *this->iter == '.') {
            ++this->iter;
            if(this->isEndOrSep()) {
                return true;
            }
        }
        this->iter = old;
        return false;
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
            if(!name[1] || (name[1] == '.' && !name[2])) {  // check '.' or '..'
                return this->matchDots(name) > 0 ? WildMatchResult::MATCHED : WildMatchResult::FAILED;
            }

            if(!this->isEndOrSep() && *this->iter != '.') {
                if(!hasFlag(this->option, WildMatchOption::DOTGLOB)) {
                    return WildMatchResult::FAILED;
                }
            }
        }

        while(*name) {
            if(this->isEndOrSep()) {
                return WildMatchResult::FAILED;
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

        for(; !this->isEndOrSep() && Meta::isZeroOrMore(this->iter); ++this->iter);
        return this->isEndOrSep() ? WildMatchResult::MATCHED : WildMatchResult::FAILED;
    }

private:
    bool isEndOrSep() const {
        return this->isEnd() || *this->iter == '/';
    }

    /**
     *
     * @param name
     * @return
     * return 0, if not match '.' or '..'
     * return 1, if match '.'
     * return 2, if match '..'
     */
    unsigned int matchDots(const char *name) {
        auto old = this->iter;
        if(*name == '.' && *this->iter == '.') {
            ++name;
            ++this->iter;
            if(!*name && this->isEndOrSep()) {
                return 1;
            }
            if(*name == '.' && *this->iter == '.') {
                ++name;
                ++this->iter;
                if(!*name && this->isEndOrSep()) {
                    return 2;
                }
            }
        }
        this->iter = old;
        return 0;
    }
};

template <typename Meta, typename Iter>
inline auto createWildCardMatcher(Iter begin, Iter end, WildMatchOption option) {
    return WildCardMatcher<Meta, Iter>(begin, end, option);
}

namespace __detail_glob {

inline bool isDir(const std::string &fullpath, struct dirent *entry) {
    if(entry->d_type == DT_DIR) {
        return true;
    }
    if(entry->d_type == DT_UNKNOWN || entry->d_type == DT_LNK) {
        return S_ISDIR(getStMode(fullpath.c_str()));
    }
    return false;
}

}   // namespace __detail_glob

/**
 *
 * @tparam Meta
 * @tparam Iter
 * @tparam Appender
 * @param baseDir
 * @param iter
 * @param end
 * @param appender
 * @param option
 * @return
 * return number of matched files.
 * if reach number of matched files limits, return -1.
 */
template <typename Meta, typename Iter, typename Appender>
int globBase(const char *baseDir, Iter iter, Iter end,
            Appender &appender, WildMatchOption option) {
    if(hasFlag(option, WildMatchOption::IGNORE_SYS_DIR)) {
        const char *ignore[] = {
                "/dev", "/proc", "/sys"
        };
        for(auto &i : ignore) {
            if(isSameFile(i, baseDir)) {
                return 0;
            }
        }
    }
    DIR *dir = opendir(baseDir);
    if(!dir) {
        return 0;
    }

    int matchCount = 0;
    for(dirent *entry; (entry = readdir(dir)); ) {
        auto matcher = createWildCardMatcher<Meta>(iter, end, option);
        const WildMatchResult ret = matcher(entry->d_name);
        if(ret == WildMatchResult::FAILED) {
            continue;
        }

        std::string name = strcmp(baseDir, ".") != 0 ? baseDir : "";
        if(!name.empty() && name.back() != '/') {
            name += '/';
        }
        name += entry->d_name;

        if(__detail_glob::isDir(name, entry)) {
            while(true) {
                if(matcher.consumeSeps() > 0) {
                    name += '/';
                }
                if(matcher.consumeDot()) {
                    name += '.';
                } else {
                    break;
                }
            }
            if(!matcher.isEnd()) {
                int globRet = globBase<Meta>(name.c_str(), matcher.getIter(), end, appender, option);
                if(globRet < 0) {
                    matchCount = -1;
                    break;
                }
                matchCount += globRet;
            }
        }
        if(matcher.isEnd()) {
            if(!appender(std::move(name))) {
                matchCount = -1;
                break;
            }
            matchCount++;
        }

        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            break;
        }
    }
    closedir(dir);
    return matchCount;
}

template <typename Meta, typename Iter, typename Appender>
int globAt(const char *dir, Iter iter, Iter end,
        Appender &appender, WildMatchOption option) {
    auto begin = iter;
    auto latestSep = end;

    // resolve base dir
    std::string baseDir;
    for(; iter != end; ++iter) {
        if(Meta::isZeroOrMore(iter) || Meta::isAny(iter)) {
            break;
        } else if(*iter == '/') {
            latestSep = iter;
            if(!baseDir.empty() && baseDir.back() == '/') {
                continue;   // skip redundant '/'
            }
        }
        baseDir += *iter;
    }

    if(latestSep == end) {  // not found '/'
        iter = begin;
        baseDir = '.';
    } else {
        iter = latestSep;
        ++iter;
        for(; !baseDir.empty() && baseDir.back() != '/'; baseDir.pop_back());
        if(hasFlag(option, WildMatchOption::TILDE)) {
            Meta::preExpand(baseDir);
        }
    }

    if(dir && *dir == '/' && baseDir[0] != '/') {
        std::string tmp = dir;
        tmp += "/";
        tmp += baseDir;
        baseDir = tmp;
    }
    return globBase<Meta>(baseDir.c_str(), iter, end, appender, option);
}

template <typename Meta, typename Iter, typename Appender>
int glob(Iter iter, Iter end, Appender &appender, WildMatchOption option) {
    return globAt<Meta>(nullptr, iter, end, appender, option);
}

} // namespace ydsh

#endif //YDSH_MISC_GLOB_HPP
