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

enum class GlobPatternStatus : unsigned char {
  FAILED,  // match failed
  DOT,     // match '.'
  DOTDOT,  // match '..'
  MATCHED, // match pattern
};

/**
 * \brief for debug
 * \param pattern
 * \param name
 * must not contain '/'
 * \param option
 * \return
 */
GlobPatternStatus matchGlobMeta(const char *pattern, const char *name, Glob::Option option);

bool appendAndEscapeGlobMeta(StringRef ref, size_t maxSize, std::string &out);

} // namespace arsh

#endif // ARSH_GLOB_H
