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

#ifndef MISC_LIB_GLOB_HPP
#define MISC_LIB_GLOB_HPP

#include <dirent.h>

#include <string>

#include "files.h"
#include "flag_util.hpp"
#include "resource.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

enum class GlobMatchOption {
  TILDE = 1u << 0u,          // apply tilde expansion before globbing
  DOTGLOB = 1u << 1u,        // match file names start with '.'
  IGNORE_SYS_DIR = 1u << 2u, // ignore system directory (/dev, /proc, /sys)
  FASTGLOB = 1u << 3u,       // posix incompatible optimized search
};

template <>
struct allow_enum_bitop<GlobMatchOption> : std::true_type {};

enum class WildMatchResult {
  FAILED,  // match failed
  DOT,     // match '.'
  DOTDOT,  // match '..'
  MATCHED, // match pattern
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

  GlobMatchOption option;

public:
  WildCardMatcher(Iter begin, Iter end, GlobMatchOption option)
      : iter(begin), end(end), option(option) {}

  Iter getIter() const { return this->iter; }

  bool isEnd() const { return this->iter == this->end; }

  unsigned int consumeSeps() {
    unsigned int count = 0;
    for (; !this->isEnd() && *this->iter == '/'; ++this->iter) {
      count++;
    }
    return count;
  }

  bool consumeDot() {
    auto old = this->iter;
    if (!this->isEnd() && *this->iter == '.') {
      ++this->iter;
      if (this->isEndOrSep()) {
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
    if (*name == '.') {
      if (!name[1] || (name[1] == '.' && !name[2])) { // check '.' or '..'
        switch (this->matchDots(name)) {
        case 1:
          return WildMatchResult::DOT;
        case 2:
          return WildMatchResult::DOTDOT;
        default:
          return WildMatchResult::FAILED;
        }
      }

      if (!this->isEndOrSep() && *this->iter != '.') {
        if (!hasFlag(this->option, GlobMatchOption::DOTGLOB)) {
          return WildMatchResult::FAILED;
        }
      }
    }

    while (*name) {
      if (this->isEndOrSep()) {
        return WildMatchResult::FAILED;
      } else if (Meta::isZeroOrMore(this->iter)) {
        break;
      } else if (*name != *this->iter && !Meta::isAny(this->iter)) {
        return WildMatchResult::FAILED;
      }
      ++name;
      ++this->iter;
    }

    while (*name) {
      if (this->isEndOrSep()) {
        this->iter = oldIter;
        name = cp++;
      } else if (*name == *this->iter || Meta::isAny(this->iter)) {
        ++name;
        ++this->iter;
      } else if (Meta::isZeroOrMore(this->iter)) {
        ++this->iter;
        if (this->isEndOrSep()) {
          return WildMatchResult::MATCHED;
        }
        oldIter = this->iter;
        cp = name + 1;
      } else {
        this->iter = oldIter;
        name = cp++;
      }
    }

    for (; !this->isEndOrSep() && Meta::isZeroOrMore(this->iter); ++this->iter)
      ;
    return this->isEndOrSep() ? WildMatchResult::MATCHED : WildMatchResult::FAILED;
  }

private:
  bool isEndOrSep() const { return this->isEnd() || *this->iter == '/'; }

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
    if (*name == '.' && *this->iter == '.') {
      ++name;
      ++this->iter;
      if (!*name && this->isEndOrSep()) {
        return 1;
      }
      if (*name == '.' && *this->iter == '.') {
        ++name;
        ++this->iter;
        if (!*name && this->isEndOrSep()) {
          return 2;
        }
      }
    }
    this->iter = old;
    return 0;
  }
};

template <typename Meta, typename Iter>
inline auto createWildCardMatcher(Iter begin, Iter end, GlobMatchOption option) {
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
  const char *base; // base dir of glob

  const Iter begin;
  const Iter end;

  const GlobMatchOption option;

  unsigned int matchCount{0};

public:
  GlobMatcher(const char *base, Iter begin, Iter end, GlobMatchOption option)
      : base(base), begin(begin), end(end), option(option) {}

  unsigned int getMatchCount() const { return this->matchCount; }

  template <typename Appender>
  GlobMatchResult operator()(Appender &appender) {
    Iter iter = this->begin;
    std::string baseDir = this->resolveBaseDir(iter);
    return this->invoke(std::move(baseDir), iter, appender);
  }

  /**
   * for debugging. not perform tilde expansion
   * @tparam Appender
   * @param appender
   * @return
   */
  template <typename Appender>
  GlobMatchResult matchExactly(Appender &appender) {
    return this->invoke(std::string(this->base), this->begin, appender);
  }

private:
  static void popDir(std::string &path) {
    if (path == ".") {
      path = "..";
      return;
    } else if (path == "..") {
      path += "/..";
      return;
    }

    auto index = path.find_last_of('/');
    if (index == std::string::npos) {
      path = ".";
    } else if (index == 0) {
      path = "/";
    } else if (std::equal(path.begin() + index + 1, path.end(), "..")) {
      path += "/../";
    } else {
      path.resize(index);
    }
  }

  template <typename Appender>
  GlobMatchResult invoke(std::string &&baseDir, Iter iter, Appender &appender) {
    this->matchCount = 0;
    int s;
    for (; (s = this->match(baseDir.c_str(), iter, appender)) == -2; popDir(baseDir))
      ;
    if (s == -1) {
      return GlobMatchResult::LIMIT;
    }
    return this->getMatchCount() > 0 ? GlobMatchResult::MATCH : GlobMatchResult::NOMATCH;
  }

  std::string resolveBaseDir(Iter &iter) const {
    auto old = iter;
    auto latestSep = this->end;

    // resolve base dir
    std::string baseDir;
    for (; iter != this->end; ++iter) {
      if (Meta::isZeroOrMore(iter) || Meta::isAny(iter)) {
        break;
      } else if (*iter == '/') {
        latestSep = iter;
        if (!baseDir.empty() && baseDir.back() == '/') {
          continue; // skip redundant '/'
        }
      }
      baseDir += *iter;
    }

    if (latestSep == this->end) { // not found '/'
      iter = old;
      baseDir = '.';
    } else {
      iter = latestSep;
      ++iter;
      for (; !baseDir.empty() && baseDir.back() != '/'; baseDir.pop_back())
        ;
      if (hasFlag(option, GlobMatchOption::TILDE)) {
        Meta::preExpand(baseDir);
      }
    }

    if (this->base && *this->base == '/' && baseDir[0] != '/') {
      std::string tmp = this->base;
      tmp += "/";
      tmp += baseDir;
      baseDir = tmp;
    }
    return baseDir;
  }

  template <typename Appender>
  int match(const char *baseDir, Iter &iter, Appender &appender);
};

template <typename Meta, typename Iter>
template <typename Appender>
int GlobMatcher<Meta, Iter>::match(const char *baseDir, Iter &iter, Appender &appender) {
  if (hasFlag(this->option, GlobMatchOption::IGNORE_SYS_DIR)) {
    const char *ignore[] = {"/dev", "/proc", "/sys"};
    for (auto &i : ignore) {
      if (isSameFile(i, baseDir)) {
        return 0;
      }
    }
  }
  DIR *dir = opendir(baseDir);
  if (!dir) {
    return 0;
  }
  auto cleanup = finally([&] {
    int old = errno;
    closedir(dir);
    errno = old;
  });

  for (dirent *entry; (entry = readdir(dir));) {
    auto matcher = createWildCardMatcher<Meta>(iter, this->end, this->option);
    const WildMatchResult ret = matcher(entry->d_name);
    if (ret == WildMatchResult::FAILED) {
      continue;
    }

    std::string name = strcmp(baseDir, ".") != 0 ? baseDir : "";
    if (!name.empty() && name.back() != '/') {
      name += '/';
    }
    name += entry->d_name;

    if (isDirectory(name, entry)) {
      while (true) {
        if (matcher.consumeSeps() > 0) {
          name += '/';
        }
        if (matcher.consumeDot()) {
          name += '.';
        } else {
          break;
        }
      }
      if (!matcher.isEnd()) {
        if (ret == WildMatchResult::DOTDOT && hasFlag(this->option, GlobMatchOption::FASTGLOB)) {
          iter = matcher.getIter();
          return -2;
        }
        Iter next = matcher.getIter();
        int s = this->match(name.c_str(), next, appender);
        if (s == -1) {
          return -1;
        } else if (s == -2) {
          iter = next;
          rewinddir(dir);
          continue;
        }
      }
    }
    if (matcher.isEnd()) {
      if (!appender(std::move(name))) {
        return -1;
      }
      this->matchCount++;
    }

    if (ret == WildMatchResult::DOT || ret == WildMatchResult::DOTDOT) {
      break;
    }
  }
  return 0;
}

template <typename Meta, typename Iter>
inline auto createGlobMatcher(const char *dir, Iter begin, Iter end, GlobMatchOption option = {}) {
  return GlobMatcher<Meta, Iter>(dir, begin, end, option);
}

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_GLOB_HPP
