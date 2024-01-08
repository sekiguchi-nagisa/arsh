/*
 * Copyright (C) 2023 Nagisa Sekiguchi
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

#ifndef ARSH_GLOB_H
#define ARSH_GLOB_H

#include <functional>

#include "misc/flag_util.hpp"
#include "misc/locale.hpp"
#include "misc/resource.hpp"
#include "misc/string_ref.hpp"

#include "cancel.h"

namespace arsh {

class Glob {
public:
  enum class Option : unsigned char {
    TILDE = 1u << 0u,             // apply tilde expansion before globbing
    DOTGLOB = 1u << 1u,           // match file names start with '.'
    FASTGLOB = 1u << 2u,          // posix incompatible optimized search
    ABSOLUTE_BASE_DIR = 1u << 3u, // only allow absolute base dir
    GLOB_LIMIT = 1u << 4u,        // limit the number of readdir
  };

  enum class Status : unsigned char {
    MATCH,
    NOMATCH,
    LIMIT,
    CANCELED,
    NEED_ABSOLUTE_BASE_DIR,
    TILDE_FAIL,
    RESOURCE_LIMIT,
    BAD_PATTERN,
  };

private:
  std::string base; // base dir of glob
  const StringRef pattern;
  const Option option;

  unsigned int readdirCount{0}; // for GLOB_LIMIT option

  unsigned int statCount{0}; // for GLOB_LIMIT option

  unsigned int matchCount{0};

  ObserverPtr<CancelToken> cancel;

  std::function<bool(std::string &)> tildeExpander;

  std::function<bool(std::string &&)> consumer;

  static constexpr unsigned int READDIR_LIMIT = 16 * 1024;

  static constexpr unsigned int STAT_LIMIT = 4096;

public:
  Glob(StringRef pattern, Option option, const char *baseDir = nullptr)
      : base(baseDir ? baseDir : ""), pattern(pattern), option(option) {}

  const auto &getBaseDir() const { return this->base; }

  void setCancelToken(CancelToken &token) { this->cancel = makeObserver(token); }

  unsigned int getMatchCount() const { return this->matchCount; }

  void setTildeExpander(std::function<bool(std::string &)> &&func) {
    this->tildeExpander = std::move(func);
  }

  void setConsumer(std::function<bool(std::string &&)> &&func) { this->consumer = std::move(func); }

  Status operator()();

  /**
   * \brief for debugging. (directly use specified base dir)
   * \return
   */
  Status matchExactly() { return this->invoke(std::string(this->base), this->pattern.begin()); }

private:
  Status invoke(std::string &&baseDir, const char *iter);

  const char *end() const { return this->pattern.end(); }

  /**
   * \brief extract and resolve base dir
   * \param iter
   * \param baseDir
   * \return
   */
  bool resolveBaseDir(const char *&iter, std::string &baseDir) const;

  std::pair<Status, bool> match(const char *baseDir, const char *&iter);
};

template <>
struct allow_enum_bitop<Glob::Option> : std::true_type {};

using GlobCharClassOp = int (*)(int, locale_t);

GlobCharClassOp lookupGlobCharClassOp(StringRef className);

class GlobPatternScanner {
public:
  enum class Status : unsigned char {
    DOT,         // match '.'
    DOTDOT,      // match '..'
    MATCHED,     // match pattern
    UNMATCHED,   // match failed
    BAD_PATTERN, // broken pattern syntax
  };

private:
  const char *iter;
  const char *const end;

public:
  GlobPatternScanner(const char *begin, const char *end) : iter(begin), end(end) {}

  const char *getIter() const { return this->iter; }

  bool isEnd() const { return this->iter == this->end; }

  unsigned int consumeSeps() {
    unsigned int count = 0;
    for (; !this->isEnd() && *this->iter == '/'; ++this->iter) {
      count++;
    }
    return count;
  }

  bool consumeDot() {
    const auto old = this->iter;
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
   * @param name
   * must be null terminated
   * @param option
   * @return
   */
  Status match(const char *name, Glob::Option option);

  enum class CharSetStatus : unsigned char {
    MATCH,
    UNMATCH,
    SYNTAX_ERROR,
    NO_CLASS,  // not follow char class, such as `[:digit:]`
    BAD_CLASS, // invalid char class name
  };

  /**
   * for bracket expression (ex. [^a-z[:digit:]])
   * @param codePoint
   * @param err
   * for error reporting. may be null
   * @return
   */
  CharSetStatus matchCharSet(int codePoint, std::string *err) {
    if (this->isEndOrSep() || *this->iter != '[') {
      if (err) {
        *err = "bracket expression must start with `['";
      }
      return CharSetStatus::SYNTAX_ERROR;
    }
    return this->matchCharSetImpl(codePoint, err);
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
    const auto old = this->iter;
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

  CharSetStatus matchCharSetImpl(int codePoint, std::string *err);

  /**
   * for [:class:] notation
   * @param codePoint
   * @param err
   * for error reporting
   * @return
   */
  CharSetStatus tryMatchCharClass(int codePoint, std::string *err);

  int consumeCharSetPart(bool first, std::string *err);
};

bool appendAndEscapeGlobMeta(StringRef ref, size_t maxSize, std::string &out);

} // namespace arsh

#endif // ARSH_GLOB_H
