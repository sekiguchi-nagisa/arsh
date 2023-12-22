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

#include <termios.h>

#include "cmd.h"
#include "keycode.h"
#include "misc/num_util.hpp"
#include "ordered_map.h"
#include "vm.h"

namespace arsh {

static bool setToReplyMap(ARState &state, OrderedMapObject &mapObj, const ArrayObject &argvObj,
                          unsigned int index, std::string &&buf) {
  auto varObj = argvObj.getValues()[index];
  auto valueObj = Value::createStr(std::move(buf));
  auto ret = mapObj.put(state, std::move(varObj), std::move(valueObj));
  return static_cast<bool>(ret);
}

static bool readLine(ARState &state, int fd, const ArrayObject &argvObj, unsigned int offset,
                     StringRef ifs, int timeoutMSec, bool backslash) {
  // clear REPL/reply before read
  errno = 0;
  state.setGlobal(BuiltinVarOffset::REPLY, Value::createStr());
  reassignReplyVar(state);

  auto &mapObj = typeAs<OrderedMapObject>(state.getGlobal(BuiltinVarOffset::REPLY_VAR));
  const unsigned int size = argvObj.getValues().size();
  unsigned int index = offset;
  const unsigned int varSize = size - index; // if zero, store line to REPLY
  std::string strBuf;
  unsigned int skipCount = 1;
  ssize_t readSize;
  char ch;
  for (bool prevIsBackslash = false;;
       prevIsBackslash = backslash && ch == '\\' && !prevIsBackslash) {
    do {
      readSize = readWithTimeout(fd, &ch, 1, timeoutMSec);
    } while (readSize < 0 && errno == EAGAIN);
    if (readSize <= 0) {
      break;
    }

    if (ch == '\n') {
      if (prevIsBackslash) {
        continue;
      }
      break;
    }
    if (ch == '\\' && !prevIsBackslash && backslash) {
      continue;
    }

    bool fieldSep = matchFieldSep(ifs, ch) && !prevIsBackslash;
    if (fieldSep && skipCount > 0) {
      if (isSpace(ch)) {
        continue;
      }
      if (--skipCount == 1) {
        continue;
      }
    }
    skipCount = 0;
    if (fieldSep && index < size - 1) {
      if (unlikely(!setToReplyMap(state, mapObj, argvObj, index, std::move(strBuf)))) {
        return false;
      }
      strBuf = "";
      index++;
      skipCount = isSpace(ch) ? 2 : 1;
      continue;
    }
    if (unlikely(strBuf.size() == StringObject::MAX_SIZE)) {
      raiseError(state, TYPE::OutOfRangeError, ERROR_STRING_LIMIT);
      return false;
    }
    strBuf += static_cast<char>(ch);
  }

  const int oldErrno = errno;

  // remove last spaces
  if (!strBuf.empty() && hasSpace(ifs)) { // check if field separator has spaces
    while (!strBuf.empty() && isSpace(strBuf.back())) {
      strBuf.pop_back();
    }
  }

  if (varSize == 0) {
    state.setGlobal(BuiltinVarOffset::REPLY, Value::createStr(std::move(strBuf)));
  } else {
    for (; index < size; index++) { // set rest variable
      if (unlikely(!setToReplyMap(state, mapObj, argvObj, index, std::move(strBuf)))) {
        return false;
      }
      strBuf = "";
    }
  }
  errno = oldErrno;
  return readSize == 1;
}

int builtin_read(ARState &state, ArrayObject &argvObj) {
  StringRef prompt;
  StringRef ifs = state.getGlobal(BuiltinVarOffset::IFS).asStrRef();
  bool backslash = true;
  bool noEcho = false;
  int fd = STDIN_FILENO;
  int timeout = -1;

  GetOptState optState(":rp:f:su:t:h");
  for (int opt; (opt = optState(argvObj)) != -1;) {
    switch (opt) {
    case 'p':
      prompt = optState.optArg;
      break;
    case 'f':
      ifs = optState.optArg;
      break;
    case 'r':
      backslash = false;
      break;
    case 's':
      noEcho = true;
      break;
    case 'u': {
      StringRef value = optState.optArg;
      fd = parseFD(value);
      if (fd < 0) {
        ERROR(state, argvObj, "%s: invalid file descriptor", toPrintable(value).c_str());
        return 1;
      }
      break;
    }
    case 't': {
      auto ret = convertToDecimal<int64_t>(optState.optArg.begin(), optState.optArg.end());
      int64_t t = ret.value;
      if (ret) {
        if (t > -1 && t <= INT32_MAX) {
          t *= 1000;
          if (t > -1 && t <= INT32_MAX) {
            timeout = static_cast<int>(t);
            break;
          }
        }
      }
      ERROR(state, argvObj, "%s: invalid timeout specification",
            toPrintable(optState.optArg).c_str());
      return 1;
    }
    case 'h':
      return showHelp(argvObj);
    case ':':
      ERROR(state, argvObj, "-%c: option require argument", optState.optOpt);
      return 2;
    default:
      return invalidOptionError(state, argvObj, optState);
    }
  }

  const bool isTTY = isatty(fd) != 0;

  // show prompt
  if (isTTY) {
    fwrite(prompt.data(), sizeof(char), prompt.size(), stderr);
    fflush(stderr);
  }

  // change tty state
  struct termios oldTTY {};
  if (noEcho && isTTY) {
    struct termios tty {};
    tcgetattr(fd, &tty);
    oldTTY = tty;
    tty.c_lflag &= ~(ECHO | ECHOK | ECHONL);
    tcsetattr(fd, TCSANOW, &tty);
  }

  // read line
  if (!isTTY) {
    timeout = -1; // ignore timeout if not tty
  }

  bool ret = readLine(state, fd, argvObj, optState.index, ifs, timeout, backslash);

  // restore tty setting
  if (noEcho && isTTY) {
    tcsetattr(fd, TCSANOW, &oldTTY);
  }

  // report error
  if (!ret && errno != 0) {
    PERROR(state, argvObj, "%d", fd);
  }
  return ret ? 0 : 1;
}

} // namespace arsh