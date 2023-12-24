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

#include <dirent.h>

#include "glob.h"
#include "misc/files.hpp"

namespace arsh {

// ############################
// ##     PatternScanner     ##
// ############################

class PatternScanner {
private:
  const char *iter;
  const char *const end;

public:
  PatternScanner(const char *begin, const char *end) : iter(begin), end(end) {}

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
  GlobPatternStatus match(const char *name, Glob::Option option);

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
};

GlobPatternStatus PatternScanner::match(const char *name, const Glob::Option option) {
  const char *cp = name;
  auto oldIter = this->end;

  // ignore starting with '.'
  if (*name == '.') {
    if (!name[1] || (name[1] == '.' && !name[2])) { // check '.' or '..'
      switch (this->matchDots(name)) {
      case 1:
        return GlobPatternStatus::DOT;
      case 2:
        return GlobPatternStatus::DOTDOT;
      default:
        return GlobPatternStatus::FAILED;
      }
    }

    if (!this->isEndOrSep() && *this->iter != '.') {
      if (!hasFlag(option, Glob::Option::DOTGLOB)) {
        return GlobPatternStatus::FAILED;
      }
    }
  }

  for (; *name; ++name) {
    if (this->isEndOrSep()) {
      return GlobPatternStatus::FAILED;
    }
    char ch = *this->iter;
    switch (ch) {
    case '?':
      ++this->iter;
      continue;
    case '*':
      goto BREAK;
    case '\\':
      if (this->iter + 1 != this->end) {
        ch = *++this->iter;
      }
      break;
    default:
      break;
    }
    if (*name != ch) {
      return GlobPatternStatus::FAILED;
    }
    ++this->iter;
  }

BREAK:
  while (*name) {
    if (this->isEndOrSep()) {
      this->iter = oldIter;
      name = cp++;
    } else {
      char ch = *this->iter;
      switch (ch) {
      case '?':
        ++name;
        ++this->iter;
        continue;
      case '*':
        ++this->iter;
        if (this->isEndOrSep()) {
          return GlobPatternStatus::MATCHED;
        }
        oldIter = this->iter;
        cp = name + 1;
        continue;
      case '\\':
        if (this->iter + 1 != this->end) {
          ch = *++this->iter;
        }
        break;
      default:
        break;
      }

      if (*name == ch) {
        ++name;
        ++this->iter;
      } else {
        this->iter = oldIter;
        name = cp++;
      }
    }
  }
  for (; !this->isEndOrSep() && *this->iter == '*'; ++this->iter)
    ;
  return this->isEndOrSep() ? GlobPatternStatus::MATCHED : GlobPatternStatus::FAILED;
}

GlobPatternStatus matchGlobMeta(const char *pattern, const char *name, Glob::Option option) {
  const StringRef ref(pattern);
  PatternScanner scanner(ref.begin(), ref.end());
  return scanner.match(name, option);
}

// ##################
// ##     Glob     ##
// ##################

static void popDir(std::string &path) {
  if (path == ".") {
    path = "..";
    return;
  }
  if (path == "..") {
    path += "/..";
    return;
  }

  const StringRef ref(path);
  const auto pos = ref.lastIndexOf("/");
  if (pos == StringRef::npos) {
    path = ".";
  } else if (pos == 0) {
    path = "/";
  } else if (ref.substr(pos + 1) == "..") {
    path += "/../";
  } else {
    path.resize(pos);
  }
}

Glob::Status Glob::operator()() {
  // preapre base dir
  auto iter = this->pattern.begin();
  std::string baseDir;
  const bool r = this->resolveBaseDir(iter, baseDir);
  this->base = baseDir;
  if (!r) {
    return Status::TILDE_FAIL;
  }
  if (hasFlag(this->option, Option::ABSOLUTE_BASE_DIR) && this->base[0] != '/') {
    return Status::NEED_ABSOLUTE_BASE_DIR;
  }
  return this->invoke(std::move(baseDir), iter);
}

Glob::Status Glob::invoke(std::string &&baseDir, const char *iter) {
  // do glob match
  this->matchCount = 0;
  this->statCount = 0;
  this->readdirCount = 0;
  std::pair<Status, bool> s;
  for (; !(s = this->match(baseDir.c_str(), iter)).second; popDir(baseDir))
    ;
  if (s.first != Status::MATCH) {
    return s.first;
  }
  return this->getMatchCount() > 0 ? Status::MATCH : Status::NOMATCH;
}

bool Glob::resolveBaseDir(const char *&iter, std::string &baseDir) const {
  const char *const old = iter; // for backup
  const char *latestSep = this->end();

  // extract until glob meta
  for (; iter != this->end(); ++iter) {
    char ch = *iter;
    switch (ch) {
    case '*':
    case '?':
      goto BREAK;
    case '\\':
      if (iter + 1 != this->end()) {
        ch = *++iter; // skip '\\'
      }
      break;
    case '/':
      latestSep = iter;
      if (!baseDir.empty() && baseDir.back() == '/') {
        continue; // skip redundant '/'
      }
      break;
    default:
      break;
    }
    baseDir += ch;
  }

BREAK:
  if (latestSep == this->end()) { // not found '/'
    iter = old;
    baseDir = ".";
  } else {
    iter = latestSep + 1;
    for (; !baseDir.empty() && baseDir.back() != '/'; baseDir.pop_back())
      ;
    if (hasFlag(this->option, Option::TILDE) && this->tildeExpander) {
      if (!this->tildeExpander(baseDir)) {
        return false;
      }
    }
  }

  // concat specify base dir and resolved dir
  if (!this->base.empty() && this->base[0] == '/' && baseDir[0] != '/') {
    std::string tmp = this->base;
    tmp += "/";
    tmp += baseDir;
    baseDir = std::move(tmp);
  }
  return true;
}

struct DirDeleter {
  void operator()(DIR *dir) const {
    if (dir) {
      const int old = errno;
      closedir(dir);
      errno = old;
    }
  }
};

static std::unique_ptr<DIR, DirDeleter> openDir(const char *path) {
  DIR *dir = opendir(path);
  return std::unique_ptr<DIR, DirDeleter>(dir);
}

std::pair<Glob::Status, bool> Glob::match(const char *baseDir, const char *&iter) {
  const auto dir = openDir(baseDir);
  if (!dir) {
    return {Status::MATCH, true};
  }

  for (dirent *entry; (entry = readdir(dir.get())) != nullptr;) {
    if (hasFlag(this->option, Option::GLOB_LIMIT) && this->readdirCount++ == READDIR_LIMIT) {
      return {Status::RESOURCE_LIMIT, true};
    }

    if (this->cancel && this->cancel()) {
      return {Status::CANCELED, true};
    }

    PatternScanner scanner(iter, this->end());
    const GlobPatternStatus ret = scanner.match(entry->d_name, this->option);
    if (ret == GlobPatternStatus::FAILED) {
      continue;
    }

    std::string name = strcmp(baseDir, ".") != 0 ? baseDir : "";
    if (!name.empty() && name.back() != '/') {
      name += '/';
    }
    name += entry->d_name;

    if (hasFlag(this->option, Option::GLOB_LIMIT) && this->statCount++ == STAT_LIMIT) {
      return {Status::RESOURCE_LIMIT, true};
    }
    if (isDirectory(dir.get(), entry)) {
      while (true) {
        if (scanner.consumeSeps() > 0) {
          name += '/';
        }
        if (scanner.consumeDot()) {
          name += '.';
        } else {
          break;
        }
      }
      if (!scanner.isEnd()) {
        if (ret == GlobPatternStatus::DOTDOT && hasFlag(this->option, Option::FASTGLOB)) {
          iter = scanner.getIter();
          return {Status::MATCH, false};
        }
        auto next = scanner.getIter();
        auto s = this->match(name.c_str(), next);
        if (!s.second) {
          iter = next;
          rewinddir(dir.get());
          continue;
        }
        if (s.first != Status::MATCH) {
          return s;
        }
      }
    }
    if (scanner.isEnd()) {
      if (this->consumer && !this->consumer(std::move(name))) {
        return {Status::LIMIT, true};
      }
      this->matchCount++;
    }

    if (ret == GlobPatternStatus::DOT || ret == GlobPatternStatus::DOTDOT) {
      break;
    }
  }
  return {Status::MATCH, true};
}

bool appendAndEscapeGlobMeta(const StringRef ref, const size_t maxSize, std::string &out) {
  const char *const end = ref.end();
  const char *start = ref.begin();

  for (const char *iter = ref.begin(); iter != end; ++iter) {
    switch (*iter) {
    case '?':
    case '*':
    case '\\':
      if (maxSize > 0 && out.size() <= maxSize - 1) {
        out += '\\';
        if (const StringRef sub(start, iter - start);
            maxSize >= sub.size() && out.size() <= maxSize - sub.size()) {
          out += sub;
          start = iter;
          break;
        }
      }
      return false;
    default:
      break;
    }
  }
  assert(start <= end);
  if (const StringRef sub(start, end - start);
      maxSize >= sub.size() && out.size() <= maxSize - sub.size()) {
    out += sub;
    return true;
  }
  return false;
}

} // namespace arsh