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

#include "misc/locale.hpp" // for macOS

#include "cancel.h"
#include "paths.h"

namespace arsh {

class GlobPatternWrapper {
private:
  std::string baseDir; // must be unescaped string
  std::string pattern; // must be glob pattern

public:
  static GlobPatternWrapper create(std::string &&value);

  const auto &getBaseDir() const { return this->baseDir; }

  const auto &getPattern() const { return this->pattern; }

  TildeExpandStatus expandTilde(DirStackProvider *provider) {
    return arsh::expandTilde(this->baseDir, true, provider);
  }

  /**
   * for error message
   * @param maxSize
   * @param out
   */
  bool join(size_t maxSize, std::string &out) const {
    if (!this->baseDir.empty()) {
      assert(this->baseDir.back() == '/');
      if (!checkedAppend(this->baseDir, maxSize, out)) {
        return false;
      }
    }
    return checkedAppend(this->pattern, maxSize, out);
  }

  std::string join() const {
    std::string out;
    this->join(out.max_size(), out);
    return out;
  }
};

class Glob {
public:
  enum class Option : unsigned char {
    DOTGLOB = 1u << 0u,    // match file names start with '.'
    FASTGLOB = 1u << 1u,   // posix incompatible optimized search
    GLOB_LIMIT = 1u << 2u, // limit the number of readdir
    GLOBSTAR = 1u << 3u,   // recognize '**' as recursive pattern
  };

  enum class Status : unsigned char {
    MATCH,
    NOMATCH,
    LIMIT,
    CANCELED,
    RESOURCE_LIMIT,
    RECURSION_DEPTH_LIMIT,
    BAD_PATTERN,
  };

private:
  const std::string base; // base dir of glob
  const StringRef pattern;
  const Option option;

  unsigned int readdirCount{0}; // for GLOB_LIMIT option

  unsigned int statCount{0}; // for GLOB_LIMIT option

  unsigned int matchCount{0};

  unsigned int callDepth{0};

  int errNum{0}; // for RESOURCE_LIMIT status

  ObserverPtr<CancelToken> cancel;

  std::function<bool(std::string &&)> consumer;

  static constexpr unsigned int READDIR_LIMIT = 16 * 1024;

  static constexpr unsigned int STAT_LIMIT = 4096;

  static constexpr unsigned int DEPTH_LIMIT = 2048;

public:
  Glob(StringRef pattern, Option option, const char *baseDir = nullptr)
      : base(baseDir ? baseDir : ""), pattern(pattern), option(option) {}

  Glob(const GlobPatternWrapper &wrapper, Option option)
      : Glob(wrapper.getPattern(), option, wrapper.getBaseDir().c_str()) {}

  void setCancelToken(CancelToken &token) { this->cancel = makeObserver(token); }

  unsigned int getMatchCount() const { return this->matchCount; }

  int getErrNum() const { return this->errNum; }

  void setConsumer(std::function<bool(std::string &&)> &&func) { this->consumer = std::move(func); }

  Status operator()(std::string *err) {
    auto iter = this->pattern.begin();
    std::string baseDir = this->resolveBaseDir(iter);
    return this->invoke(std::move(baseDir), iter, err);
  }

  /**
   * \brief for debugging. (directly use specified base dir)
   * \return
   */
  Status matchExactly() {
    return this->invoke(std::string(this->base), this->pattern.begin(), nullptr);
  }

  /**
   *
   * @param pattern
   * after extraction, maintains remain pattern
   * @return
   * if no dir, return empty
   */
  static std::string extractDirFromPattern(StringRef &pattern);

private:
  Status invoke(std::string &&baseDir, const char *iter, std::string *err);

  const char *end() const { return this->pattern.end(); }

  bool consumeDoubleStars(const char *&iter) const {
    unsigned int count = 0;
    auto old = iter;
    while (iter != this->end() && *iter == '*' && iter + 1 != this->end() && *(iter + 1) == '*') {
      iter += 2;
      if (iter == this->end()) { // **
        old = iter;
        count++;
        break;
      }
      if (*iter == '/') {
        count++;
        do {
          iter++;
        } while (iter != this->end() && *iter == '/');
        old = iter;
        if (iter == this->end()) { // **///  -> remain '/'
          old--;
          break;
        }
      }
    }
    iter = old;
    return count > 0;
  }

  /**
   * extract and resolve base dir
   * @param iter
   * @return
   */
  std::string resolveBaseDir(const char *&iter) const;

  std::pair<Status, bool> match(const std::string &baseDir, const char *&iter,
                                bool allowEmptyPattern, std::string *err);

  bool addResult(std::string &&path) {
    if (this->consumer && !this->consumer(std::move(path))) {
      return false;
    }
    this->matchCount++;
    return true;
  }

  Status matchDoubleStar(const std::string &baseDir, size_t targetOffset, const char *iter,
                         std::string *err);

  Status matchDoubleStar(const std::string &baseDir, const char *iter, std::string *err) {
    size_t offset = baseDir.size();
    if (baseDir == ".") {
      offset = 0;
    } else if (!baseDir.empty() && baseDir.back() != '/') {
      offset++;
    }
    return this->matchDoubleStar(baseDir, offset, iter, err);
  }
};

template <>
struct allow_enum_bitop<Glob::Option> : std::true_type {};

using GlobCharClassOp = int (*)(int, locale_t);

GlobCharClassOp lookupGlobCharClassOp(StringRef className);

class GlobPatternScanner {
public:
  enum class Status : unsigned char {
    DOT,                  // match '.'
    DOTDOT,               // match '..'
    MATCHED,              // match pattern
    UNMATCHED,            // match failed
    UNMATCHED_DOT,        // match failed ('.' or '..')
    UNMATCHED_DOT_PREFIX, // match failed (start with '.')
    BAD_PATTERN,          // broken pattern syntax
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
    for (; this->expect('/'); ++this->iter) {
      count++;
    }
    return count;
  }

  bool consumeDot() {
    const auto old = this->iter;
    if (this->expect('.')) {
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
   * @param allowEmptyPattern
   * @param err
   * @return
   */
  Status match(const char *name, Glob::Option option, bool allowEmptyPattern, std::string *err);

  Status match(const char *name, Glob::Option option, std::string *err) {
    return this->match(name, option, false, err);
  }

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
    if (this->expect('[')) {
      return this->matchCharSetImpl(codePoint, err);
    }
    if (err) {
      *err = "bracket expression must start with `['";
    }
    return CharSetStatus::SYNTAX_ERROR;
  }

private:
  bool isEndOrSep() const { return this->isEnd() || *this->iter == '/'; }

  bool expect(const char ch) const { return !this->isEnd() && *this->iter == ch; }

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
    if (*name == '.' && this->expect('.')) {
      ++name;
      ++this->iter;
      if (!*name && this->isEndOrSep()) {
        return 1;
      }
      if (*name == '.' && this->expect('.')) {
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
