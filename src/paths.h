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

#ifndef YDSH_PATHS_H
#define YDSH_PATHS_H

#include "misc/flag_util.hpp"
#include "misc/resource.hpp"
#include "misc/string_ref.hpp"

namespace ydsh {

class FilePathCache {
private:
  /**
   * contains previously resolved path (for direct search)
   */
  std::string prevPath;

  CStringHashMap<std::string> map;

  static constexpr unsigned int MAX_CACHE_SIZE = 100;

public:
  NON_COPYABLE(FilePathCache);

  FilePathCache() = default;

  ~FilePathCache();

  enum SearchOp {
    NON = 0u,
    USE_DEFAULT_PATH = 1u << 0u,
    DIRECT_SEARCH = 1u << 1u,
  };

  /**
   * search file path by using PATH
   * if cannot resolve path (file not found), return null.
   */
  const char *searchPath(const char *cmdName, SearchOp op = NON);

  void removePath(const char *cmdName);

  bool isCached(const char *cmdName) const;

  /**
   * clear all cache
   */
  void clear();

  /**
   * get begin iterator of map
   */
  auto begin() const { return this->map.cbegin(); }

  /**
   * get end iterator of map
   */
  auto end() const { return this->map.cend(); }
};

template <>
struct allow_enum_bitop<FilePathCache::SearchOp> : std::true_type {};

/**
 * expand dot '.' '..'
 * @param basePath
 * may be null. must be full path.
 * @param path
 * may be null
 * @return
 * if expansion failed, return empty string
 */
std::string expandDots(const char *basePath, const char *path);

struct DirStackProvider {
  virtual ~DirStackProvider() = default;

  virtual size_t size() const = 0;

  /**
   *
   * @param index
   * @return
   * if index out of range, return empty string
   */
  virtual StringRef get(size_t index) = 0;
};

enum class TildeExpandStatus {
  OK,
  NO_TILDE,       // not start with tilde
  NO_USER,        // not found corresponding user
  NO_DIR_STACK,   // dir stack is not provided
  UNDEF_OR_EMPTY, // undefined env or empty
  INVALID_NUM,    // invalid number format
  OUT_OF_RANGE,   // out of range index
  HAS_NULL,
};

/**
 *
 * @param str
 * @param useHOME
 * if true, use `HOME' environmental variable for `~' expansion
 * @param provider
 * may be null
 * @return
 * if tilde expansion succeed, return OK
 */
TildeExpandStatus expandTilde(std::string &str, bool useHOME, DirStackProvider *provider);

/**
 *
 * @param logicalWorkingDir
 * @param useLogical
 * @return
 * if has error, return null and set errno.
 */
CStrPtr getWorkingDir(const std::string &logicalWorkingDir, bool useLogical);

/**
 * change current working directory and update OLDPWD, PWD.
 * if dest is null, do nothing and return true.
 */
bool changeWorkingDir(std::string &logicalWorkingDir, StringRef dest, bool useLogical);

} // namespace ydsh

#endif // YDSH_PATHS_H
