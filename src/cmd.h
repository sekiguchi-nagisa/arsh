/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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

#ifndef YDSH_CMD_H
#define YDSH_CMD_H

#include <algorithm>

#include "misc/opt.hpp"
#include "misc/string_ref.hpp"

struct DSState;

namespace ydsh {

class ArrayObject;

struct GetOptState : opt::GetOptState {
  /**
   * index of next processing argument
   */
  unsigned int index;

  explicit GetOptState(const char *optStr) : GetOptState(optStr, 1, false) {
    this->remapHelp = true;
  }

  GetOptState(const char *optStr, unsigned int index, bool remapHelp)
      : opt::GetOptState(optStr), index(index) {
    this->remapHelp = remapHelp;
  }

  int operator()(const ArrayObject &obj);
};

int showUsage(const ArrayObject &obj);

int showHelp(const ArrayObject &obj);

int invalidOptionError(const DSState &st, const ArrayObject &obj, const GetOptState &s);

int parseFD(StringRef value);

/**
 * return exit status.
 * argvObj must be Array_Object
 */
using builtin_command_t = int (*)(DSState &state, ArrayObject &argvObj);

builtin_command_t lookupBuiltinCommand(StringRef commandName);

// common function for field splitting
inline bool isSpace(char ch) { return ch == ' ' || ch == '\t' || ch == '\n'; }

inline bool hasSpace(StringRef ifs) { return std::any_of(ifs.begin(), ifs.end(), isSpace); }

inline bool matchFieldSep(StringRef ifs, char ch) {
  return std::any_of(ifs.begin(), ifs.end(), [&](auto e) { return e == ch; });
}

/**
 * see. http://man7.org/linux/man-pages/man3/exit.3.html
 * @param status
 * @return
 * 0-255
 */
inline int maskExitStatus(int64_t status) {
  return static_cast<int>(static_cast<uint64_t>(status) & 0xFF);
}

} // namespace ydsh

#define ERROR(ctx, obj, fmt, ...)                                                                  \
  printErrorAt(ctx, obj.getValues()[0].asStrRef(), 0, fmt, ##__VA_ARGS__)

#define PERROR(ctx, obj, fmt, ...)                                                                 \
  printErrorAt(ctx, obj.getValues()[0].asStrRef(), errno, fmt, ##__VA_ARGS__)

#define CHECK_STDOUT_ERROR(ctx, obj, errNum)                                                       \
  do {                                                                                             \
    errno = (errNum);                                                                              \
    if (errno != 0 || fflush(stdout) == EOF) {                                                     \
      PERROR(ctx, obj, "io error");                                                                \
      return 1;                                                                                    \
    }                                                                                              \
  } while (false)

#endif // YDSH_CMD_H
