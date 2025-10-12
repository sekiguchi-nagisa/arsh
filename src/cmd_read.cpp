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

static bool setToReplyMap(ARState &state, const ArrayObject &argvObj, unsigned int index,
                          std::string &&buf) {
  auto varObj = argvObj[index];
  auto valueObj = Value::createStr(std::move(buf));
  const auto ret = typeAs<OrderedMapObject>(state.getGlobal(BuiltinVarOffset::REPLY_VAR))
                       .put(state, std::move(varObj), std::move(valueObj));
  return static_cast<bool>(ret);
}

struct ReadLineParam {
  int fd{STDIN_FILENO}; // for input
  int timeoutMSec{-1};
  bool backslash{true};
  char delim{'\n'};
  bool ignoreDelim{false};
  int nbytes{-1};
  StringRef ifs;
};

static int readByteWithRetryExceptSIGINT(const ReadLineParam &param, char &ch) {
  int readSize;
  while (true) {
    readSize = static_cast<int>(
        readWithTimeout(param.fd, &ch, 1, {.retry = false, .timeoutMSec = param.timeoutMSec}));
    if (readSize < 0) {
      if (readSize == -2) { // timeout
        errno = 0;
      }
      if (errno == EAGAIN) {
        continue;
      }
      if (errno == EINTR && !ARState::isInterrupted()) {
        continue; // retry except for SIGINT
      }
    }
    break;
  }
  return readSize;
}

static bool readLine(ARState &state, const ArrayObject &argvObj, unsigned int index,
                     const ReadLineParam &param) {
  // clear REPL/reply before read
  state.setGlobal(BuiltinVarOffset::REPLY, Value::createStr());
  reassignReplyVar(state);

  const unsigned int size = argvObj.size();
  const bool useReply = size - index == 0;
  std::string strBuf;
  unsigned int skipCount = 1;
  int lastReadSize = 0;
  unsigned int readCount = 0;
  char ch;
  for (bool prevIsBackslash = false;
       param.nbytes < 0 || readCount < static_cast<unsigned int>(param.nbytes);
       prevIsBackslash = param.backslash && ch == '\\' && !prevIsBackslash) {
    if (lastReadSize = readByteWithRetryExceptSIGINT(param, ch); lastReadSize <= 0) {
      break;
    }
    readCount++;

    if (ch == param.delim && !param.ignoreDelim) {
      if (prevIsBackslash) {
        if (ch != '\n') {
          strBuf += ch;
        } else {
          readCount--;
        }
        continue;
      }
      break;
    }
    if (ch == '\n' && prevIsBackslash) {
      readCount--;
      continue; // skip escaped newline
    }
    if (ch == '\\' && !prevIsBackslash && param.backslash) {
      readCount--;
      continue;
    }

    const bool fieldSep = matchFieldSep(param.ifs, ch) && !prevIsBackslash;
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
      if (unlikely(!setToReplyMap(state, argvObj, index, std::move(strBuf)))) {
        return false;
      }
      strBuf = "";
      index++;
      skipCount = isSpace(ch) ? 2 : 1;
      continue;
    }
    if (unlikely(strBuf.size() == StringObject::MAX_SIZE)) {
      raiseStringLimit(state);
      return false;
    }
    strBuf += ch;
  }

  const int oldErrno = errno;

  // remove last spaces
  if (!strBuf.empty() && hasSpace(param.ifs)) { // check if field separator has spaces
    while (!strBuf.empty() && isSpace(strBuf.back())) {
      strBuf.pop_back();
    }
  }

  if (useReply) {
    state.setGlobal(BuiltinVarOffset::REPLY, Value::createStr(std::move(strBuf)));
  } else {
    for (; index < size; index++) { // set rest variable
      if (unlikely(!setToReplyMap(state, argvObj, index, std::move(strBuf)))) {
        return false;
      }
      strBuf = "";
    }
  }
  errno = oldErrno;
  return lastReadSize == 1;
}

int builtin_read(ARState &state, ArrayObject &argvObj) {
  StringRef prompt;
  bool noEcho = false;
  ReadLineParam param{};
  param.ifs = state.getGlobal(BuiltinVarOffset::IFS).asStrRef();

  GetOptState optState(":rp:d:f:su:t:n:N:h");
  for (int opt; (opt = optState(argvObj)) != -1;) {
    switch (opt) {
    case 'd':
      param.delim = optState.optArg.empty() ? '\0' : optState.optArg[0];
      break;
    case 'p':
      prompt = optState.optArg;
      break;
    case 'f':
      param.ifs = optState.optArg;
      break;
    case 'r':
      param.backslash = false;
      break;
    case 's':
      noEcho = true;
      break;
    case 'u': {
      StringRef value = optState.optArg;
      param.fd = parseFD(value);
      if (param.fd < 0) {
        ERROR(state, argvObj, "%s: invalid file descriptor", toPrintable(value).c_str());
        return 1;
      }
      break;
    }
    case 't': {
      auto ret = convertToNum10<int64_t>(optState.optArg.begin(), optState.optArg.end());
      int64_t t = ret.value;
      if (ret) {
        if (t > -1 && t <= INT32_MAX) {
          t *= 1000;
          if (t > -1 && t <= INT32_MAX) {
            param.timeoutMSec = static_cast<int>(t);
            break;
          }
        }
      }
      ERROR(state, argvObj, "%s: invalid timeout specification",
            toPrintable(optState.optArg).c_str());
      return 1;
    }
    case 'n':
    case 'N': {
      auto ret = convertToNum10<int32_t>(optState.optArg.begin(), optState.optArg.end());
      if (ret && ret.value > -1) {
        param.nbytes = ret.value;
        param.ignoreDelim = opt == 'N';
        break;
      }
      ERROR(state, argvObj, "%s: must be positive int32", toPrintable(optState.optArg).c_str());
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

  const bool isTTY = isatty(param.fd) != 0;

  // show prompt
  if (isTTY) {
    fwrite(prompt.data(), sizeof(char), prompt.size(), stderr);
    fflush(stderr);
  }

  // change tty state
  struct termios oldTTY {};
  if (noEcho && isTTY) {
    struct termios tty {};
    tcgetattr(param.fd, &tty);
    oldTTY = tty;
    tty.c_lflag &= ~(ECHO | ECHOK | ECHONL);
    tcsetattr(param.fd, TCSANOW, &tty);
  }

  // read line
  if (!isTTY) {
    param.timeoutMSec = -1; // ignore timeout if not tty
  }

  if (argvObj.size() - optState.index == 0 || param.ignoreDelim) {
    param.ifs = ""; // if no var name (store to REPLY), not perform field splitting
  }
  bool ret = readLine(state, argvObj, optState.index, param);

  // restore tty setting
  if (noEcho && isTTY) {
    tcsetattr(param.fd, TCSANOW, &oldTTY);
  }

  // report error
  if (!ret && errno != 0) {
    PERROR(state, argvObj, "%d", param.fd);
  }
  return ret ? 0 : 1;
}

} // namespace arsh