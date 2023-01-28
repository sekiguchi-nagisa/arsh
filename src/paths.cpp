/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#include <pwd.h>

#include "constant.h"
#include "misc/files.h"
#include "misc/num_util.hpp"
#include "paths.h"

namespace ydsh {

// ###########################
// ##     FilePathCache     ##
// ###########################

FilePathCache::~FilePathCache() {
  for (auto &pair : this->map) {
    free(const_cast<char *>(pair.first));
  }
}

const char *FilePathCache::searchPath(const char *cmdName, FilePathCache::SearchOp op) {
  // if found '/', return fileName
  if (strchr(cmdName, '/') != nullptr) {
    return cmdName;
  }

  // search cache
  if (!hasFlag(op, DIRECT_SEARCH)) {
    auto iter = this->map.find(cmdName);
    if (iter != this->map.end()) {
      return iter->second.c_str();
    }
  }

  // get PATH
  const char *pathPrefix = getenv(ENV_PATH);
  if (pathPrefix == nullptr || hasFlag(op, USE_DEFAULT_PATH)) {
    pathPrefix = VAL_DEFAULT_PATH;
  }

  // resolve path
  for (StringRef pathValue = pathPrefix; !pathValue.empty();) {
    StringRef remain;
    auto pos = pathValue.find(":");
    if (pos != StringRef::npos) {
      remain = pathValue.substr(pos + 1);
      pathValue = pathValue.slice(0, pos);
    }

    if (!pathValue.empty()) {
      auto resolvedPath = pathValue.toString();
      if (resolvedPath.back() != '/') {
        resolvedPath += '/';
      }
      resolvedPath += cmdName;

      if (mode_t mode = getStMode(resolvedPath.c_str());
          S_ISREG(mode) & S_IS_PERM_(mode, S_IXUSR)) {
        if (hasFlag(op, DIRECT_SEARCH)) {
          this->prevPath = std::move(resolvedPath);
          return this->prevPath.c_str();
        }
        // set to cache
        if (this->map.size() == MAX_CACHE_SIZE) {
          free(const_cast<char *>(this->map.begin()->first));
          this->map.erase(this->map.begin());
        }
        auto pair = this->map.emplace(strdup(cmdName), std::move(resolvedPath));
        assert(pair.second);
        return pair.first->second.c_str();
      }
    }
    pathValue = remain;
  }

  // not found
  return nullptr;
}

void FilePathCache::removePath(const char *cmdName) {
  if (cmdName != nullptr) {
    auto iter = this->map.find(cmdName);
    if (iter != this->map.end()) {
      free(const_cast<char *>(iter->first));
      this->map.erase(iter);
    }
  }
}

bool FilePathCache::isCached(const char *cmdName) const {
  return this->map.find(cmdName) != this->map.end();
}

void FilePathCache::clear() {
  for (auto &pair : this->map) {
    free(const_cast<char *>(pair.first));
  }
  this->map.clear();
}

/**
 * path must be full path
 */
static std::vector<std::string> createPathStack(const char *path) {
  std::vector<std::string> stack;
  if (*path == '/') {
    stack.emplace_back("/");
    path++;
  }

  for (const char *ptr; (ptr = strchr(path, '/')) != nullptr;) {
    const unsigned int size = ptr - path;
    if (size == 0) {
      path++;
      continue;
    }
    stack.emplace_back(path, size);
    path += size;
  }
  if (*path != '\0') {
    stack.emplace_back(path);
  }
  return stack;
}

std::string expandDots(const char *basePath, const char *path) {
  std::string str;

  if (path == nullptr || *path == '\0') {
    return str;
  }

  std::vector<std::string> resolvedPathStack;
  auto pathStack(createPathStack(path));

  // fill resolvedPathStack
  if (!pathStack.empty() && pathStack.front() != "/") {
    if (basePath != nullptr && *basePath == '/') {
      resolvedPathStack = createPathStack(basePath);
    } else {
      auto ptr = getCWD();
      if (!ptr) {
        return str;
      }
      resolvedPathStack = createPathStack(ptr.get());
    }
  }

  for (auto &e : pathStack) {
    if (e == "..") {
      if (!resolvedPathStack.empty()) {
        resolvedPathStack.pop_back();
      }
    } else if (e != ".") {
      resolvedPathStack.push_back(std::move(e));
    }
  }

  // create path
  const unsigned int size = resolvedPathStack.size();
  if (size == 1) {
    str += '/';
  }
  for (unsigned int i = 1; i < size; i++) {
    str += '/';
    str += resolvedPathStack[i];
  }
  return str;
}

TildeExpandStatus expandTilde(std::string &str, bool useHOME, DirStackProvider *provider) {
  if (str.empty() || str.front() != '~') {
    return TildeExpandStatus::NO_TILDE;
  }

  const char *path = str.c_str();
  std::string expanded;
  for (; *path != '/' && *path != '\0'; path++) {
    expanded += *path;
  }
  StringRef prefix = expanded;
  prefix.removePrefix(1);

  // expand tilde
  if (prefix.empty()) { // ~
    const char *value = useHOME ? getenv(ENV_HOME) : nullptr;
    if (!value) { // use HOME, but HOME is not set, fallback to getpwuid(getuid())
      if (struct passwd *pw = getpwuid(getuid())) {
        value = pw->pw_dir;
      }
    }
    if (value) {
      expanded = value;
    } else {
      return TildeExpandStatus::NO_USER;
    }
  } else if (const char ch = prefix[0]; ch == '+' || ch == '-' || isDecimal(ch)) {
    if (!provider) {
      return TildeExpandStatus::NO_DIR_STACK;
    }

    if (prefix == "+") { // ~+
      /**
       * if env undef or empty, return error
       * otherwise use env value even if value indicates invalid path
       */
      if (const char *value = getenv(ENV_PWD); value && *value) {
        expanded = value;
      } else {
        return TildeExpandStatus::UNDEF_OR_EMPTY;
      }
    } else if (prefix == "-") { // ~-
      /**
       * if env undef or empty, return error
       * otherwise use env value even if value indicates invalid path
       */
      if (const char *value = getenv(ENV_OLDPWD); value && *value) {
        expanded = value;
      } else {
        return TildeExpandStatus::UNDEF_OR_EMPTY;
      }
    } else { // ~+N, ~-N, ~N
      StringRef num = prefix;
      if (ch == '+' || ch == '-') { // skip
        num.removePrefix(1);
      }
      assert(!num.empty() && isDecimal(num[0]));
      auto pair = convertToDecimal<uint64_t>(num.begin(), num.end());
      if (!pair.second) {
        return TildeExpandStatus::INVALID_NUM;
      }
      const size_t size = provider->size();
      if (pair.first > size) {
        return TildeExpandStatus::OUT_OF_RANGE;
      }
      const size_t index =
          ch == '-' ? static_cast<size_t>(pair.first) : size - static_cast<size_t>(pair.first);
      auto ret = provider->get(index);
      if (ret.empty()) {
        return TildeExpandStatus::UNDEF_OR_EMPTY;
      } else if (ret.hasNullChar()) {
        return TildeExpandStatus::HAS_NULL;
      }
      expanded.assign(ret.data(), ret.size());
    }
  } else { // expand user
    if (struct passwd *pw = getpwnam(expanded.c_str() + 1); pw) {
      expanded = pw->pw_dir;
    } else {
      return TildeExpandStatus::NO_USER;
    }
  }

  // append rest
  if (*path != '\0') {
    expanded += path;
  }
  std::swap(str, expanded);
  return TildeExpandStatus::OK;
}

CStrPtr getWorkingDir(const std::string &logicalWorkingDir, bool useLogical) {
  if (useLogical) {
    auto mode = getStMode(logicalWorkingDir.c_str());
    if (!mode) {
      return nullptr;
    }
    if (!S_ISDIR(mode)) {
      errno = ENOTDIR;
      return nullptr;
    }
    return CStrPtr(strdup(logicalWorkingDir.c_str()));
  }
  return getCWD();
}

bool changeWorkingDir(std::string &logicalWorkingDir, StringRef dest, const bool useLogical) {
  if (dest.hasNullChar()) {
    errno = EINVAL;
    return false;
  }

  const bool tryChdir = !dest.empty();
  const char *ptr = dest.data();
  std::string actualDest;
  if (tryChdir) {
    if (useLogical) {
      actualDest = expandDots(logicalWorkingDir.c_str(), ptr);
      ptr = actualDest.c_str();
    }
    if (chdir(ptr) != 0) {
      return false;
    }
  }

  // update OLDPWD
  const char *oldpwd = getenv(ENV_PWD);
  if (oldpwd == nullptr) {
    oldpwd = "";
  }
  setenv(ENV_OLDPWD, oldpwd, 1);

  // update PWD
  if (tryChdir) {
    if (useLogical) {
      setenv(ENV_PWD, actualDest.c_str(), 1);
      logicalWorkingDir = std::move(actualDest);
    } else {
      auto cwd = getCWD();
      if (cwd != nullptr) {
        setenv(ENV_PWD, cwd.get(), 1);
        logicalWorkingDir = cwd.get();
      }
    }
  }
  return true;
}

} // namespace ydsh