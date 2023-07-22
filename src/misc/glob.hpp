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

enum class GlobMatchOption : unsigned short {
  TILDE = 1u << 0u,             // apply tilde expansion before globbing
  DOTGLOB = 1u << 1u,           // match file names start with '.'
  IGNORE_SYS_DIR = 1u << 2u,    // ignore system directory (/dev, /proc, /sys)
  FASTGLOB = 1u << 3u,          // posix incompatible optimized search
  ABSOLUTE_BASE_DIR = 1u << 4u, // only allow absolute base dir
  GLOB_LIMIT = 1u << 5u,        // limit the number of readdir
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
  CANCELED,
  NEED_ABSOLUTE_BASE_DIR,
  TILDE_FAIL,
  RESOURCE_LIMIT,
};

template <typename Meta, typename Iter, typename Cancel>
class GlobMatcher {
private:
  /**
   * may be empty
   */
  std::string base; // base dir of glob

  const Iter begin;
  const Iter end;

  const GlobMatchOption option;

  unsigned short readdirCount{0}; // for GLOB_LIMIT option

  unsigned int matchCount{0};

  Cancel cancel; // for cancellation

  static constexpr unsigned int READDIR_LIMIT = 16 * 1024;

public:
  GlobMatcher(const char *base, Iter begin, Iter end, Cancel &&cancel, GlobMatchOption option)
      : base(base ? base : ""), begin(begin), end(end), option(option), cancel(std::move(cancel)) {}

  unsigned int getMatchCount() const { return this->matchCount; }

  const std::string &getBase() const { return this->base; }

  template <typename T>
  static constexpr bool tilde_expander_requirement_v =
      std::is_same_v<bool, std::invoke_result_t<T, std::string &>>;

  template <typename TildeExpander, typename Appender,
            enable_when<tilde_expander_requirement_v<TildeExpander>> = nullptr>
  GlobMatchResult operator()(TildeExpander expander, Appender &appender) {
    Iter iter = this->begin;
    std::string baseDir;
    bool r = this->resolveBaseDir(std::move(expander), iter, baseDir);
    this->base = std::move(baseDir);
    if (!r) {
      return GlobMatchResult::TILDE_FAIL;
    }
    if (hasFlag(this->option, GlobMatchOption::ABSOLUTE_BASE_DIR) && this->base[0] != '/') {
      return GlobMatchResult::NEED_ABSOLUTE_BASE_DIR;
    }
    return this->invoke(std::string(this->base), iter, appender);
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

  enum class Result {
    EXIT,
    LIMIT,
    UNWIND,
    CANCELED,
    RESOURCE_LIMIT,
  };

  template <typename Appender>
  GlobMatchResult invoke(std::string &&baseDir, Iter iter, Appender &appender) {
    this->matchCount = 0;
    Result s;
    for (; (s = this->match(baseDir.c_str(), iter, appender)) == Result::UNWIND; popDir(baseDir))
      ;
    if (s == Result::LIMIT) {
      return GlobMatchResult::LIMIT;
    } else if (s == Result::CANCELED) {
      return GlobMatchResult::CANCELED;
    } else if (s == Result::RESOURCE_LIMIT) {
      return GlobMatchResult::RESOURCE_LIMIT;
    }
    return this->getMatchCount() > 0 ? GlobMatchResult::MATCH : GlobMatchResult::NOMATCH;
  }

  template <typename TildeExpander,
            enable_when<tilde_expander_requirement_v<TildeExpander>> = nullptr>
  bool resolveBaseDir(TildeExpander expander, Iter &iter, std::string &baseDir) const {
    auto old = iter;
    auto latestSep = this->end;

    // resolve base dir
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
      if (hasFlag(this->option, GlobMatchOption::TILDE)) {
        if (!expander(baseDir)) {
          return false;
        }
      }
    }

    if (!this->base.empty() && this->base[0] == '/' && baseDir[0] != '/') {
      std::string tmp = this->base;
      tmp += "/";
      tmp += baseDir;
      baseDir = tmp;
    }
    return true;
  }

  template <typename Appender>
  Result match(const char *baseDir, Iter &iter, Appender &appender);
};

template <typename Meta, typename Iter, typename Cancel>
template <typename Appender>
typename GlobMatcher<Meta, Iter, Cancel>::Result
GlobMatcher<Meta, Iter, Cancel>::match(const char *baseDir, Iter &iter, Appender &appender) {
  if (hasFlag(this->option, GlobMatchOption::IGNORE_SYS_DIR)) {
    const char *ignore[] = {"/dev", "/proc", "/sys"};
    for (auto &i : ignore) {
      if (isSameFile(i, baseDir)) {
        return Result::EXIT;
      }
    }
  }
  DIR *dir = opendir(baseDir);
  if (!dir) {
    return Result::EXIT;
  }
  auto cleanup = finally([dir] {
    int old = errno;
    closedir(dir);
    errno = old;
  });

  for (dirent *entry; (entry = readdir(dir)) != nullptr;) {
    if (hasFlag(this->option, GlobMatchOption::GLOB_LIMIT) &&
        this->readdirCount++ == READDIR_LIMIT) {
      return Result::RESOURCE_LIMIT;
    }

    if (this->cancel()) {
      return Result::CANCELED;
    }

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

    if (isDirectory(dir, entry)) {
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
          return Result::UNWIND;
        }
        Iter next = matcher.getIter();
        auto s = this->match(name.c_str(), next, appender);
        if (s == Result::UNWIND) {
          iter = next;
          rewinddir(dir);
          continue;
        } else if (s != Result::EXIT) {
          return s;
        }
      }
    }
    if (matcher.isEnd()) {
      if (!appender(std::move(name))) {
        return Result::LIMIT;
      }
      this->matchCount++;
    }

    if (ret == WildMatchResult::DOT || ret == WildMatchResult::DOTDOT) {
      break;
    }
  }
  return Result::EXIT;
}

template <typename Meta, typename Iter, typename Cancel>
inline auto createGlobMatcher(const char *dir, Iter begin, Iter end, Cancel &&cancel,
                              GlobMatchOption option) {
  return GlobMatcher<Meta, Iter, Cancel>(dir, begin, end, std::forward<Cancel>(cancel), option);
}

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_GLOB_HPP
