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

namespace ydsh {

int builtin_cd(DSState &state, ArrayObject &argvObj) {
  GetOptState optState;
  bool useLogical = true;
  for (int opt; (opt = optState(argvObj, "PLh")) != -1;) {
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
      return invalidOptionError(argvObj, optState);
    }
  }

  unsigned int index = optState.index;
  StringRef dest;
  bool useOldpwd = false;
  if (index < argvObj.getValues().size()) {
    dest = argvObj.getValues()[index].asStrRef();
    if (dest == "-") {
      const char *v = getenv(ENV_OLDPWD);
      if (v == nullptr) {
        ERROR(argvObj, "OLDPWD not set");
        return 1;
      }
      dest = v;
      useOldpwd = true;
    }
  } else {
    const char *v = getenv(ENV_HOME);
    if (v == nullptr) {
      ERROR(argvObj, "HOME not set");
      return 1;
    }
    dest = v;
  }

  if (useOldpwd) {
    printf("%s\n", toPrintable(dest).c_str());
  }

  if (!changeWorkingDir(state.logicalWorkingDir, dest, useLogical)) {
    PERROR(argvObj, "%s", toPrintable(dest).c_str());
    return 1;
  }
  return 0;
}

enum class PrintDirOp : unsigned int {
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
  value += dir;
  value = toPrintable(value);
  return value;
}

static bool printDirstack(const ArrayObject &dirstack, const char *cwd, const PrintDirOp dirOp) {
  std::string home;
  if (!hasFlag(dirOp, PrintDirOp::FULL_PATH)) {
    home = "~";
    if (!expandTilde(home)) {
      return false;
    }
  }
  assert(dirstack.size() <= SYS_LIMIT_DIRSTACK_SIZE);
  const auto size = static_cast<int>(dirstack.size());
  if (hasFlag(dirOp, PrintDirOp::PER_LINE)) {
    unsigned int count = hasFlag(dirOp, PrintDirOp::LINENO) ? countDigits(dirstack.size()) : 0;
    std::string prefix;
    if (count) {
      prefix = padLeft(0, count, ' ');
      if (!prefix.empty()) {
        prefix += "  ";
      }
    }
    printf("%s%s\n", prefix.c_str(), formatDir(cwd, home).c_str());
    for (int i = size - 1; i > -1; i--) {
      prefix = "";
      if (count) {
        prefix = padLeft(size - i, count, ' ');
        if (!prefix.empty()) {
          prefix += "  ";
        }
      }
      printf("%s%s\n", prefix.c_str(), formatDir(dirstack.getValues()[i].asStrRef(), home).c_str());
    }
  } else {
    fputs(formatDir(cwd, home).c_str(), stdout);
    for (int i = size - 1; i > -1; i--) {
      fputc(' ', stdout);
      fputs(formatDir(dirstack.getValues()[i].asStrRef(), home).c_str(), stdout);
    }
    fputc('\n', stdout);
  }
  fflush(stdout);
  return true;
}

int builtin_dirs(DSState &state, ArrayObject &argvObj) {
  auto &dirstack = typeAs<ArrayObject>(state.getGlobal(BuiltinVarOffset::DIRSTACK));
  if (dirstack.size() > SYS_LIMIT_DIRSTACK_SIZE) {
    dirstack.refValues().resize(SYS_LIMIT_DIRSTACK_SIZE); // truncate dirstack
  }

  PrintDirOp dirOp{};
  GetOptState optState;
  for (int opt; (opt = optState(argvObj, "clpvh")) != -1;) {
    switch (opt) {
    case 'c':
      dirstack.refValues().clear();
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
      return invalidOptionError(argvObj, optState);
    }
  }
  auto cwd = state.getWorkingDir();
  if (!cwd) {
    ERROR(argvObj, "cannot resolve current working dir");
    return 1;
  }
  printDirstack(dirstack, cwd.get(), dirOp);
  return 0;
}

int builtin_pushd_popd(DSState &state, ArrayObject &argvObj) {
  auto &dirstack = typeAs<ArrayObject>(state.getGlobal(BuiltinVarOffset::DIRSTACK));
  if (dirstack.size() > SYS_LIMIT_DIRSTACK_SIZE) {
    dirstack.refValues().resize(SYS_LIMIT_DIRSTACK_SIZE); // truncate dirstack
  }

  GetOptState optState;
  for (int opt; (opt = optState(argvObj, "h")) != -1;) {
    if (opt == 'h') {
      return showHelp(argvObj);
    } else {
      if (auto ref = argvObj.getValues()[optState.index].asStrRef();
          ref.size() > 1 && isDecimal(ref[1])) {
        break;
      }
      return invalidOptionError(argvObj, optState);
    }
  }

  uint64_t rotateIndex = 0;
  bool rotate = false;
  StringRef dest;
  if (optState.index < argvObj.size()) {
    dest = argvObj.getValues()[optState.index].asStrRef();
    if (dest.startsWith("+") || dest.startsWith("-")) {
      auto pair = convertToDecimal<uint64_t>(dest.begin() + 1, dest.end());
      if (!pair.second) {
        ERROR(argvObj, "%s: invalid number", toPrintable(dest).c_str());
        return 1;
      }
      if (pair.first > dirstack.size()) {
        ERROR(argvObj, "%s: directory stack index of range (up to %zu)", toPrintable(dest).c_str(),
              dirstack.size());
        return 1;
      }
      if (dest[0] == '-') {
        rotateIndex = pair.first;
      } else { // +
        rotateIndex = dirstack.size() - pair.first;
      }
      rotate = true;
    }
  }
  if (argvObj.getValues()[0].asStrRef() == "pushd") {
    auto cwd = state.getWorkingDir();
    if (!cwd) {
      ERROR(argvObj, "cannot resolve current working dir");
      return 1;
    }
    if (!rotate) {
      if (optState.index < argvObj.size()) { // if specify DIR, push current and change to DIR
        if (dirstack.size() + 1 == SYS_LIMIT_DIRSTACK_SIZE) {
          ERROR(argvObj, "directory stack size reaches limit (up to %zu)", SYS_LIMIT_DIRSTACK_SIZE);
          return 1;
        }
      } else { // swap stack top and current
        if (dirstack.size() == 0) {
          ERROR(argvObj, "no other directory");
          return 1;
        }
        dest = dirstack.getValues().back().asStrRef();
      }
      if (!changeWorkingDir(state.logicalWorkingDir, dest, true)) {
        PERROR(argvObj, "%s", toPrintable(dest).c_str());
        return 1;
      }
      if (optState.index == argvObj.size()) {
        dirstack.refValues().pop_back();
      }
      dirstack.append(DSValue::createStr(cwd.get()));
    } else if (rotateIndex < dirstack.size()) {
      dest = dirstack.getValues()[rotateIndex].asStrRef();
      if (!changeWorkingDir(state.logicalWorkingDir, dest, true)) {
        PERROR(argvObj, "%s", toPrintable(dest).c_str());
        return 1;
      }
      const size_t limit = dirstack.size();
      dirstack.refValues().insert(dirstack.refValues().begin(), DSValue::createStr(cwd.get()));
      for (size_t count = static_cast<size_t>(rotateIndex) + 1; count < limit; count++) {
        auto top = dirstack.refValues().back();
        dirstack.refValues().pop_back();
        dirstack.refValues().insert(dirstack.refValues().begin(), std::move(top));
      }
      dirstack.refValues().pop_back();
    }
  } else { // popd
    if (dirstack.size() == 0) {
      ERROR(argvObj, "directory stack empty");
      return 1;
    }
    if (rotate && rotateIndex < dirstack.size()) {
      dirstack.refValues().erase(dirstack.refValues().begin() + rotateIndex);
    } else {
      dest = dirstack.getValues().back().asStrRef();
      if (!changeWorkingDir(state.logicalWorkingDir, dest, true)) {
        PERROR(argvObj, "%s", toPrintable(dest).c_str());
        return 1;
      }
      dirstack.refValues().pop_back();
    }
  }
  auto cwd = state.getWorkingDir();
  assert(cwd);
  printDirstack(dirstack, cwd.get(), PrintDirOp{});
  return 0;
}

} // namespace ydsh