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
                switch(this->matchDots(name)) {
                case 1:
                    return WildMatchResult::DOT;
                case 2:
                    return WildMatchResult::DOTDOT;
                default:
                    return WildMatchResult::FAILED;
                }
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

enum class GlobMatchResult {
    MATCH,
    NOMATCH,
    LIMIT,
};

template <typename Meta, typename Iter>
class GlobMatcher {
private:
    /**
     * may be null
     */
    const char *base;   // base dir of glob

    const Iter begin;
    const Iter end;

    const WildMatchOption option;

    unsigned int matchCount{0};

public:
    GlobMatcher(const char *base, Iter begin, Iter end, WildMatchOption option) :
            base(base), begin(begin), end(end), option(option) {}

    unsigned int getMatchCount() const {
        return this->matchCount;
    }

    template <typename Appender>
    GlobMatchResult operator()(Appender &appender) {
        this->matchCount = 0;
        Iter iter = this->begin;
        std::string baseDir = this->resolveBaseDir(iter);
        int s = this->match(baseDir.c_str(), iter, appender);
        return this->toResult(s);
    }

    /**
     * for debuging. not perform tilde expansion
     * @tparam Appender
     * @param appender
     * @return
     */
    template <typename Appender>
    GlobMatchResult matchExactly(Appender &appender) {
        int s = this->match(this->base, this->begin, appender);
        return this->toResult(s);
    }

private:
    GlobMatchResult toResult(int s) const {
        if(s < 0) {
            return GlobMatchResult::LIMIT;
        }
        return this->getMatchCount() > 0 ? GlobMatchResult::MATCH : GlobMatchResult::NOMATCH;
    }

    std::string resolveBaseDir(Iter &iter) const {
        auto old = iter;
        auto latestSep = this->end;

        // resolve base dir
        std::string baseDir;
        for(; iter != this->end; ++iter) {
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

        if(latestSep == this->end) {  // not found '/'
            iter = old;
            baseDir = '.';
        } else {
            iter = latestSep;
            ++iter;
            for(; !baseDir.empty() && baseDir.back() != '/'; baseDir.pop_back());
            if(hasFlag(option, WildMatchOption::TILDE)) {
                Meta::preExpand(baseDir);
            }
        }

        if(this->base && *this->base == '/' && baseDir[0] != '/') {
            std::string tmp = this->base;
            tmp += "/";
            tmp += baseDir;
            baseDir = tmp;
        }
        return baseDir;
    }

    template <typename Appender>
    int match(const char *baseDir, Iter iter, Appender &appender);

    static bool isDir(const std::string &fullpath, struct dirent *entry) {
        if(entry->d_type == DT_DIR) {
            return true;
        }
        if(entry->d_type == DT_UNKNOWN || entry->d_type == DT_LNK) {
            return S_ISDIR(getStMode(fullpath.c_str()));
        }
        return false;
    }
};

template<typename Meta, typename Iter>
template<typename Appender>
int GlobMatcher<Meta, Iter>::match(const char *baseDir, Iter iter, Appender &appender) {
    if(hasFlag(this->option, WildMatchOption::IGNORE_SYS_DIR)) {
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

    int s = 0;
    for(dirent *entry; (entry = readdir(dir)); ) {
        auto matcher = createWildCardMatcher<Meta>(iter, this->end, this->option);
        const WildMatchResult ret = matcher(entry->d_name);
        if(ret == WildMatchResult::FAILED) {
            continue;
        }

        std::string name = strcmp(baseDir, ".") != 0 ? baseDir : "";
        if(!name.empty() && name.back() != '/') {
            name += '/';
        }
        name += entry->d_name;

        if(isDir(name, entry)) {
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
                if(this->match(name.c_str(), matcher.getIter(), appender) < 0) {
                    s = -1;
                    break;
                }
            }
        }
        if(matcher.isEnd()) {
            if(!appender(std::move(name))) {
                s = -1;
                break;
            }
            this->matchCount++;
        }

        if(ret == WildMatchResult::DOT || ret == WildMatchResult::DOTDOT) {
            break;
        }
    }
    closedir(dir);
    return s;
}

template <typename Meta, typename Iter>
inline auto createGlobMatcher(const char *dir, Iter begin, Iter end, WildMatchOption option = {}) {
    return GlobMatcher<Meta, Iter>(dir, begin, end, option);
}

} // namespace ydsh

#endif //YDSH_MISC_GLOB_HPP
