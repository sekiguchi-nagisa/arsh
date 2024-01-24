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
#include "misc/unicode.hpp"

namespace arsh {

// ################################
// ##     GlobPatternScanner     ##
// ################################

GlobPatternScanner::Status GlobPatternScanner::match(const char *name, const Glob::Option option,
                                                     std::string *err) {
  // ignore starting with '.'
  if (*name == '.') {
    if (!name[1] || (name[1] == '.' && !name[2])) { // check '.' or '..'
      switch (this->matchDots(name)) {
      case 1:
        return Status::DOT;
      case 2:
        return Status::DOTDOT;
      default:
        return Status::UNMATCHED;
      }
    }

    if (!this->isEndOrSep() && *this->iter != '.') {
      if (!hasFlag(option, Glob::Option::DOTGLOB)) {
        return Status::UNMATCHED;
      }
    }
  }

  const char *oldName = nullptr;
  auto oldIter = this->end;
  const char *const endName = name + strlen(name);
  while (*name) {
    if (!this->isEndOrSep()) {
      char ch = *this->iter;
      switch (ch) {
      case '?':
      case '[': {
        int codePoint = 0;
        if (const unsigned int byteSize = UnicodeUtil::utf8ToCodePoint(name, endName, codePoint)) {
          name += byteSize;
        } else { // invalid byte
          ++name;
        }
        if (ch == '?') {
          ++this->iter;
          continue;
        }
        switch (this->matchCharSetImpl(codePoint, err)) {
        case CharSetStatus::MATCH:
          continue;
        case CharSetStatus::UNMATCH:
          break;
        case CharSetStatus::SYNTAX_ERROR:
        case CharSetStatus::NO_CLASS:
        case CharSetStatus::BAD_CLASS:
          return Status::BAD_PATTERN; // force terminate matching
        }
        goto BACKTRACK;
      }
      case '*':
        ++this->iter;
        if (this->isEndOrSep()) {
          return Status::MATCHED;
        }
        oldIter = this->iter;
        oldName = name + 1;
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
        continue;
      }
    }

  BACKTRACK:
    if (oldName) {
      this->iter = oldIter;
      name = oldName++;
      continue;
    }
    return Status::UNMATCHED;
  }
  for (; this->expect('*'); ++this->iter)
    ;
  return this->isEndOrSep() ? Status::MATCHED : Status::UNMATCHED;
}

static constexpr const char *needCloseBracket = "bracket expression must end with `]'";
static constexpr const char *charClassInRange = "`[:' is not allowed in character range";

GlobPatternScanner::CharSetStatus GlobPatternScanner::matchCharSetImpl(const int codePoint,
                                                                       std::string *err) {
  assert(this->expect('['));
  ++this->iter;
  bool negate = false;
  unsigned int matchCount = 0;
  for (bool first = true; !this->isEndOrSep() && *this->iter != ']'; first = false) {
    if (first && (*this->iter == '!' || *this->iter == '^')) {
      negate = true;
      ++this->iter;
      if (this->isEndOrSep()) {
        break;
      }
    }

    bool foundCharClass = false;
    if (*this->iter == '[') {
      switch (this->tryMatchCharClass(codePoint, err)) {
      case CharSetStatus::MATCH:
        matchCount++;
        foundCharClass = true;
        break;
      case CharSetStatus::UNMATCH:
        foundCharClass = true;
        break;
      case CharSetStatus::NO_CLASS:
        break;
      case CharSetStatus::SYNTAX_ERROR:
      case CharSetStatus::BAD_CLASS:
        return CharSetStatus::SYNTAX_ERROR;
      }
    }
    if (foundCharClass) {
      const auto old = this->iter;
      if (this->expect('-')) {
        ++this->iter;
        if (!this->expect(']')) {
          if (err) {
            *err = charClassInRange;
          }
          return CharSetStatus::SYNTAX_ERROR;
        }
      }
      this->iter = old;
      continue;
    }

    const int lower = this->consumeCharSetPart(first, err);
    if (lower < 0) {
      return CharSetStatus::SYNTAX_ERROR;
    }
    int upper = lower;
    if (this->isEndOrSep()) {
      if (err) {
        *err = needCloseBracket;
      }
      return CharSetStatus::SYNTAX_ERROR;
    }
    if (*this->iter == '-') {
      ++this->iter;
      if (this->isEndOrSep() || *this->iter == ']') {
        --this->iter;
      } else {
        upper = this->consumeCharSetPart(false, err);
        if (upper < 0) {
          return CharSetStatus::SYNTAX_ERROR;
        }
      }
    }
    if (lower > upper) {
      if (err) {
        *err = "character range is out of order";
      }
      return CharSetStatus::SYNTAX_ERROR;
    }
    if (codePoint >= lower && codePoint <= upper) {
      matchCount++;
    }
  }
  if (this->expect(']')) {
    ++this->iter;
    bool r = matchCount > 0;
    if (negate) {
      r = !r;
    }
    return r ? CharSetStatus::MATCH : CharSetStatus::UNMATCH;
  }
  if (err) {
    *err = needCloseBracket;
  }
  return CharSetStatus::SYNTAX_ERROR;
}

static int isword_l(int ch, locale_t locale) { return isalnum_l(ch, locale) || ch == '_'; }

static int isascii_l(int ch, locale_t) { return isascii(ch); }

GlobCharClassOp lookupGlobCharClassOp(const StringRef className) {
#define EACH_CHAR_CLASS_OP(OP)                                                                     \
  OP(alnum)                                                                                        \
  OP(alpha)                                                                                        \
  OP(ascii)                                                                                        \
  OP(blank)                                                                                        \
  OP(cntrl)                                                                                        \
  OP(digit)                                                                                        \
  OP(graph)                                                                                        \
  OP(lower)                                                                                        \
  OP(print)                                                                                        \
  OP(punct)                                                                                        \
  OP(space)                                                                                        \
  OP(upper)                                                                                        \
  OP(word)                                                                                         \
  OP(xdigit)

  static const StrRefMap<GlobCharClassOp> map = {
#define GEN_ENTRY(E) {#E, is##E##_l},
      EACH_CHAR_CLASS_OP(GEN_ENTRY)
#undef GEN_ENTRY
  };

  if (const auto iter = map.find(className); iter != map.end()) {
    return iter->second;
  }
  return nullptr;
}

GlobPatternScanner::CharSetStatus GlobPatternScanner::tryMatchCharClass(const int codePoint,
                                                                        std::string *err) {
  const auto oldIter = this->iter;
  assert(*this->iter == '[');
  ++this->iter;            // skip '['
  if (this->expect(':')) { // start with '[:' is char class
    ++this->iter;
    std::string className;
    while (!this->isEndOrSep() && *this->iter != ':') {
      if (const char ch = *this->iter; ch >= 'a' && ch <= 'z') {
        className += ch;
        ++this->iter;
      } else {
        break;
      }
    }

    bool close = false;
    if (this->expect(':')) {
      ++this->iter;
      if (this->expect(']')) {
        ++this->iter;
        close = true;
      }
    }
    if (!close) {
      if (err) {
        *err = "character class must end with `:]'";
      }
      return CharSetStatus::SYNTAX_ERROR;
    }

    if (auto *op = lookupGlobCharClassOp(className)) {
      return op(codePoint, POSIX_LOCALE_C.get()) ? CharSetStatus::MATCH : CharSetStatus::UNMATCH;
    }
    this->iter = oldIter;
    if (err) {
      *err = "undefined character class: ";
      *err += className;
    }
    return CharSetStatus::BAD_CLASS;
  }
  this->iter = oldIter;
  return CharSetStatus::NO_CLASS;
}

int GlobPatternScanner::consumeCharSetPart(const bool first, std::string *err) {
  assert(!this->isEndOrSep());
  switch (*this->iter) {
  case '-':
    ++this->iter;
    if (first || this->expect(']')) {
      return '-';
    }
    --this->iter;
    if (err) {
      *err = "unescaped `-' is only available in first or last";
    }
    return -1;
  case ']':
    if (first) {
      ++this->iter;
      return ']';
    }
    break; // unreachable
  case '[': {
    const auto old = this->iter;
    ++this->iter;
    if (this->expect(':')) { // not allow '[:'
      if (err) {
        *err = charClassInRange;
      }
      return -1;
    }
    this->iter = old;
    break;
  }
  case '\\':
    ++this->iter;
    if (this->isEndOrSep()) {
      --this->iter;
      if (err) {
        *err = "need character after `\\'";
      }
      return -1;
    }
    break;
  default:
    break;
  }
  int codePoint = -1;
  if (const unsigned int byteSize =
          UnicodeUtil::utf8ToCodePoint(this->iter, this->end, codePoint)) {
    this->iter += byteSize;
    return codePoint;
  }
  if (err) {
    *err = "invalid utf-8 sequence";
  }
  return -1;
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

Glob::Status Glob::operator()(std::string *err) {
  // prepare base dir
  auto iter = this->pattern.begin();
  std::string baseDir;
  if (!this->resolveBaseDir(iter, baseDir)) {
    if (err) {
      *err = baseDir;
    }
    return Status::TILDE_FAIL;
  }
  if (hasFlag(this->option, Option::ABSOLUTE_BASE_DIR) && baseDir[0] != '/') {
    if (err) {
      *err = baseDir;
    }
    return Status::NEED_ABSOLUTE_BASE_DIR;
  }
  return this->invoke(std::move(baseDir), iter, err);
}

Glob::Status Glob::invoke(std::string &&baseDir, const char *iter, std::string *err) {
  // do glob match
  this->matchCount = 0;
  this->statCount = 0;
  this->readdirCount = 0;
  std::pair<Status, bool> s;
  for (; !(s = this->match(baseDir.c_str(), iter, err)).second; popDir(baseDir))
    ;
  if (s.first != Status::MATCH) {
    return s.first;
  }
  return this->getMatchCount() > 0 ? Status::MATCH : Status::NOMATCH;
}

std::string Glob::extractDirFromPattern(StringRef &pattern) {
  std::string baseDir;

  const char *iter = pattern.begin();
  const char *const end = pattern.end();
  const char *latestSep = end;

  // extract until glob meta
  for (; iter != end; ++iter) {
    char ch = *iter;
    switch (ch) {
    case '*':
    case '?':
    case '[':
      goto BREAK;
    case '\\':
      if (iter + 1 != end) {
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
  if (latestSep == end) { // not found '/'
    return "";
  }

  iter = latestSep + 1;
  for (; !baseDir.empty() && baseDir.back() != '/'; baseDir.pop_back())
    ;
  pattern = {iter, static_cast<size_t>(end - iter)};
  return baseDir;
}

bool Glob::resolveBaseDir(const char *&iter, std::string &baseDir) const {
  StringRef tmpPattern = this->pattern;
  baseDir = extractDirFromPattern(tmpPattern);
  iter = tmpPattern.begin();
  if (hasFlag(this->option, Option::TILDE) && this->tildeExpander && !baseDir.empty()) {
    if (!this->tildeExpander(baseDir)) {
      return false;
    }
  }

  // concat specify base dir and resolved dir
  if (!this->base.empty() && this->base[0] == '/' && baseDir[0] != '/') {
    std::string tmp = this->base;
    if (tmp.back() != '/') {
      tmp += "/";
    }
    tmp += baseDir;
    baseDir = std::move(tmp);
  }
  if (baseDir.empty()) {
    baseDir = ".";
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

std::pair<Glob::Status, bool> Glob::match(const char *baseDir, const char *&iter,
                                          std::string *err) {
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

    GlobPatternScanner scanner(iter, this->end());
    const auto ret = scanner.match(entry->d_name, this->option, err);
    if (ret == GlobPatternScanner::Status::UNMATCHED) {
      continue;
    }
    if (ret == GlobPatternScanner::Status::BAD_PATTERN) {
      return {Status::BAD_PATTERN, true};
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
        if (ret == GlobPatternScanner::Status::DOTDOT && hasFlag(this->option, Option::FASTGLOB)) {
          iter = scanner.getIter();
          return {Status::MATCH, false};
        }
        auto next = scanner.getIter();
        auto s = this->match(name.c_str(), next, err);
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

    if (ret == GlobPatternScanner::Status::DOT || ret == GlobPatternScanner::Status::DOTDOT) {
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
    case '[':
    case ']':
    case '^':
    case '!':
    case '-':
    case '\\':
      if (const StringRef sub(start, iter - start); checkedAppend(sub, maxSize, out)) {
        start = iter;
        if (out.size() < maxSize) {
          out += '\\';
          break;
        }
      }
      return false;
    default:
      break;
    }
  }
  assert(start <= end);
  return checkedAppend(StringRef(start, end - start), maxSize, out);
}

} // namespace arsh