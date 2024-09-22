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

#include "cmd.h"
#include "misc/format.hpp"
#include "misc/num_util.hpp"
#include "paths.h"
#include "vm.h"

namespace arsh {

int builtin_pwd(ARState &state, ArrayObject &argvObj) {
  bool useLogical = true;

  GetOptState optState("LPh");
  for (int opt; (opt = optState(argvObj)) != -1;) {
    switch (opt) {
    case 'L':
      useLogical = true;
      break;
    case 'P':
      useLogical = false;
      break;
    case 'h':
      return showHelp(argvObj);
    default:
      return invalidOptionError(state, argvObj, optState);
    }
  }

  const auto workdir = state.getWorkingDir(useLogical);
  if (!workdir) {
    PERROR(state, argvObj, ".");
    return 1;
  }
  int errNum = 0;
  if (printf("%s\n", workdir.get()) < 0) {
    errNum = errno;
  }
  CHECK_STDOUT_ERROR(state, argvObj, errNum);
  return 0;
}

int builtin_cd(ARState &state, ArrayObject &argvObj) {
  GetOptState optState("PLh");
  bool useLogical = true;
  for (int opt; (opt = optState(argvObj)) != -1;) {
    switch (opt) {
    case 'P':
      useLogical = false;
      break;
    case 'L':
      useLogical = true;
      break;
    case 'h':
      return showHelp(argvObj);
    default:
      return invalidOptionError(state, argvObj, optState);
    }
  }

  StringRef dest;
  bool useOldpwd = false;
  if (const unsigned int index = optState.index; index < argvObj.getValues().size()) {
    dest = argvObj.getValues()[index].asStrRef();
    if (dest == "-") {
      const char *v = getenv(ENV_OLDPWD);
      if (v == nullptr) {
        ERROR(state, argvObj, "OLDPWD not set");
        return 1;
      }
      dest = v;
      useOldpwd = true;
    }
  } else {
    const char *v = getenv(ENV_HOME);
    if (v == nullptr) {
      ERROR(state, argvObj, "HOME not set");
      return 1;
    }
    dest = v;
  }

  int errNum = 0;
  if (useOldpwd) {
    std::string out;
    appendAsPrintable(dest, out);
    if (printf("%s\n", out.c_str()) < 0) {
      errNum = errno;
      goto END;
    }
  }

  if (!changeWorkingDir(state.logicalWorkingDir, dest, useLogical)) {
    PERROR(state, argvObj, "%s", toPrintable(dest).c_str());
    return 1;
  }

END:
  CHECK_STDOUT_ERROR(state, argvObj, errNum);
  return 0;
}

enum class PrintDirOp : unsigned char {
  FULL_PATH = 1u << 0u,
  PER_LINE = 1u << 1u,
  LINENO = 1u << 2u,
};

template <>
struct allow_enum_bitop<PrintDirOp> : std::true_type {};

static std::string formatDir(StringRef dir, const std::string &home) {
  std::string value;
  if (!home.empty() && dir.startsWith(home)) {
    dir.removePrefix(home.size());
    value += "~";
  }
  appendAsPrintable(dir, value);
  return value;
}

#undef TRY
#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (!(E)) {                                                                                    \
      return errno;                                                                                \
    }                                                                                              \
  } while (false)

static int printDirStack(const ArrayObject &dirStack, const char *cwd, const PrintDirOp dirOp) {
  std::string home;
  if (!hasFlag(dirOp, PrintDirOp::FULL_PATH)) {
    home = "~";
    if (expandTilde(home, true, nullptr, StringObject::MAX_SIZE) != TildeExpandStatus::OK) {
      return ENOENT;
    }
  }
  errno = 0;
  assert(dirStack.size() <= SYS_LIMIT_DIRSTACK_SIZE);
  const auto size = static_cast<int>(dirStack.size());
  if (hasFlag(dirOp, PrintDirOp::PER_LINE)) {
    const unsigned int count =
        hasFlag(dirOp, PrintDirOp::LINENO) ? countDigits(dirStack.size()) : 0;
    std::string prefix;
    if (count) {
      prefix = padLeft(0, count, ' ');
      if (!prefix.empty()) {
        prefix += "  ";
      }
    }
    TRY(printf("%s%s\n", prefix.c_str(), formatDir(cwd, home).c_str()) > -1);
    for (int i = size - 1; i > -1; i--) {
      prefix = "";
      if (count) {
        prefix = padLeft(size - i, count, ' ');
        if (!prefix.empty()) {
          prefix += "  ";
        }
      }
      TRY(printf("%s%s\n", prefix.c_str(),
                 formatDir(dirStack.getValues()[i].asStrRef(), home).c_str()) > -1);
    }
  } else {
    TRY(fputs(formatDir(cwd, home).c_str(), stdout) != EOF);
    for (int i = size - 1; i > -1; i--) {
      TRY(fputc(' ', stdout) != EOF);
      TRY(fputs(formatDir(dirStack.getValues()[i].asStrRef(), home).c_str(), stdout) != EOF);
    }
    TRY(fputc('\n', stdout) != EOF);
  }
  return 0;
}

int builtin_dirs(ARState &state, ArrayObject &argvObj) {
  auto &dirStack = typeAs<ArrayObject>(state.getGlobal(BuiltinVarOffset::DIRSTACK));
  if (unlikely(!dirStack.checkIteratorInvalidation(state, "DIRSTACK"))) {
    return 1;
  }
  if (dirStack.size() > SYS_LIMIT_DIRSTACK_SIZE) {
    dirStack.refValues().resize(SYS_LIMIT_DIRSTACK_SIZE); // truncate
  }

  PrintDirOp dirOp{};
  GetOptState optState("clpvh");
  for (int opt; (opt = optState(argvObj)) != -1;) {
    switch (opt) {
    case 'c':
      dirStack.refValues().clear();
      return 0;
    case 'l':
      setFlag(dirOp, PrintDirOp::FULL_PATH);
      break;
    case 'p':
      setFlag(dirOp, PrintDirOp::PER_LINE);
      break;
    case 'v':
      setFlag(dirOp, PrintDirOp::FULL_PATH | PrintDirOp::PER_LINE | PrintDirOp::LINENO);
      break;
    case 'h':
      return showHelp(argvObj);
    default:
      return invalidOptionError(state, argvObj, optState);
    }
  }

  const auto cwd = state.getWorkingDir();
  if (!cwd) {
    PERROR(state, argvObj, "cannot resolve current working dir");
    return 1;
  }
  const int errNum = printDirStack(dirStack, cwd.get(), dirOp);
  CHECK_STDOUT_ERROR(state, argvObj, errNum);
  return 0;
}

int builtin_pushd_popd(ARState &state, ArrayObject &argvObj) {
  auto &dirStack = typeAs<ArrayObject>(state.getGlobal(BuiltinVarOffset::DIRSTACK));
  if (unlikely(!dirStack.checkIteratorInvalidation(state, "DIRSTACK"))) {
    return 1;
  }
  if (dirStack.size() > SYS_LIMIT_DIRSTACK_SIZE) {
    dirStack.refValues().resize(SYS_LIMIT_DIRSTACK_SIZE); // truncate
  }

  GetOptState optState("h");
  for (int opt; (opt = optState(argvObj)) != -1;) {
    if (opt == 'h') {
      return showHelp(argvObj);
    }
    if (const auto ref = argvObj.getValues()[optState.index].asStrRef();
        ref.size() > 1 && isDecimal(ref[1])) {
      break;
    }
    return invalidOptionError(state, argvObj, optState);
  }

  uint64_t rotateIndex = 0;
  bool rotate = false;
  StringRef dest;
  if (optState.index < argvObj.size()) {
    dest = argvObj.getValues()[optState.index].asStrRef();
    if (dest.startsWith("+") || dest.startsWith("-")) {
      const auto pair = convertToNum10<uint64_t>(dest.begin() + 1, dest.end());
      if (!pair) {
        ERROR(state, argvObj, "%s: invalid number", toPrintable(dest).c_str());
        return 1;
      }
      if (pair.value > dirStack.size()) {
        ERROR(state, argvObj, "%s: directory stack index out of range (up to %zu)",
              toPrintable(dest).c_str(), dirStack.size());
        return 1;
      }
      if (dest[0] == '-') {
        rotateIndex = pair.value;
      } else { // +
        rotateIndex = dirStack.size() - pair.value;
      }
      rotate = true;
    }
  }
  if (argvObj.getValues()[0].asStrRef() == "pushd") {
    const auto cwd = state.getWorkingDir();
    if (!cwd) {
      PERROR(state, argvObj, "cannot resolve current working dir");
      return 1;
    }
    if (!rotate) {
      if (optState.index < argvObj.size()) { // if specify DIR, push current and change to DIR
        if (dirStack.size() + 1 > SYS_LIMIT_DIRSTACK_SIZE) {
          ERROR(state, argvObj, "directory stack size reaches limit (up to %zu)",
                SYS_LIMIT_DIRSTACK_SIZE);
          return 1;
        }
      } else { // swap stack top and current
        if (dirStack.size() == 0) {
          ERROR(state, argvObj, "no other directory");
          return 1;
        }
        dest = dirStack.getValues().back().asStrRef();
      }
      if (!changeWorkingDir(state.logicalWorkingDir, dest, true)) {
        PERROR(state, argvObj, "%s", toPrintable(dest).c_str());
        return 1;
      }
      if (optState.index == argvObj.size()) {
        dirStack.refValues().pop_back();
      }
      dirStack.append(Value::createStr(cwd.get()));
    } else if (rotateIndex < dirStack.size()) {
      dest = dirStack.getValues()[rotateIndex].asStrRef();
      if (!changeWorkingDir(state.logicalWorkingDir, dest, true)) {
        PERROR(state, argvObj, "%s", toPrintable(dest).c_str());
        return 1;
      }
      const size_t limit = dirStack.size();
      dirStack.refValues().insert(dirStack.refValues().begin(), Value::createStr(cwd.get()));
      for (size_t count = static_cast<size_t>(rotateIndex) + 1; count < limit; count++) {
        auto top = dirStack.refValues().back();
        dirStack.refValues().pop_back();
        dirStack.refValues().insert(dirStack.refValues().begin(), std::move(top));
      }
      dirStack.refValues().pop_back();
    }
  } else { // popd
    if (dirStack.size() == 0) {
      ERROR(state, argvObj, "directory stack empty");
      return 1;
    }
    if (rotate && rotateIndex < dirStack.size()) {
      dirStack.refValues().erase(dirStack.refValues().begin() + static_cast<ssize_t>(rotateIndex));
    } else {
      dest = dirStack.getValues().back().asStrRef();
      if (!changeWorkingDir(state.logicalWorkingDir, dest, true)) {
        PERROR(state, argvObj, "%s", toPrintable(dest).c_str());
        return 1;
      }
      dirStack.refValues().pop_back();
    }
  }
  const auto cwd = state.getWorkingDir();
  assert(cwd);
  const int errNum = printDirStack(dirStack, cwd.get(), PrintDirOp{});
  CHECK_STDOUT_ERROR(state, argvObj, errNum);
  return 0;
}

} // namespace arsh