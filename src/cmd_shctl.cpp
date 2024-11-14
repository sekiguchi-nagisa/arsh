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
#include "cmd_desc.h"
#include "misc/pty.hpp"
#include "ordered_map.h"
#include "vm.h"

namespace arsh {

static int printBacktrace(const ARState &state, const ArrayObject &argvObj, StringRef subCmd) {
  int errNum = 0;
  state.getCallStack().fillStackTrace([&errNum](StackTraceElement &&s) {
    if (printf("from %s:%d '%s()'\n", s.getSourceName().c_str(), s.getLineNum(),
               s.getCallerName().c_str()) < 0) {
      errNum = errno;
      return false;
    }
    return true;
  });
  CHECK_STDOUT_ERROR2(state, argvObj, subCmd, errNum);
  return 0;
}

static int printFuncName(const ARState &state, const ArrayObject &argvObj, StringRef subCmd) {
  const auto *code = state.getCallStack().getFrame().code;
  const char *name = nullptr;
  if (!code->is(CodeKind::NATIVE) && !code->is(CodeKind::TOPLEVEL)) {
    name = cast<CompiledCode>(code)->getName();
  }
  int errNum = 0;
  if (printf("%s\n", name != nullptr ? name : "<toplevel>") < 0) {
    errNum = errno;
  }
  CHECK_STDOUT_ERROR2(state, argvObj, subCmd, errNum);
  return name != nullptr ? 0 : 1;
}

static RuntimeOption recognizeRuntimeOption(StringRef name) {
  // normalize option name (remove _ -, lower-case)
  std::string optName;
  for (char ch : name) {
    if (ch >= 'a' && ch <= 'z') {
      optName += ch;
    } else if (ch >= 'A' && ch <= 'Z') {
      optName += static_cast<char>(ch - 'A' + 'a');
    } else if (ch == '_' || ch == '-') {
      continue;
    } else {
      return RuntimeOption{};
    }
  }

  for (auto &e : getRuntimeOptionEntries()) {
    if (optName == e.name) {
      return e.option;
    }
  }
  return RuntimeOption{};
}

static unsigned int computeMaxOptionNameSize() {
  unsigned int maxSize = 0;
  for (auto &e : getRuntimeOptionEntries()) {
    unsigned int size = strlen(e.name) + 2;
    if (size > maxSize) {
      maxSize = size;
    }
  }
  return maxSize;
}

static int showOptions(const ARState &state) {
  const unsigned int maxNameSize = computeMaxOptionNameSize();
  for (auto &e : getRuntimeOptionEntries()) {
    errno = 0;
    if (printf("%-*s%s\n", static_cast<int>(maxNameSize), e.name,
               state.has(e.option) ? "on" : "off") < 0) {
      return errno;
    }
  }
  return 0;
}

static int restoreOptions(ARState &state, const ArrayObject &argvObj, StringRef restoreStr) {
  for (StringRef::size_type pos = 0; pos != StringRef::npos;) {
    auto r = restoreStr.find(' ', pos);
    auto sub = restoreStr.slice(pos, r);
    pos = r != StringRef::npos ? r + 1 : r;

    if (sub.empty()) {
      continue;
    }

    bool set = true;
    if (sub.endsWith("=on")) {
      sub.removeSuffix(3);
    } else if (sub.endsWith("=off")) {
      set = false;
      sub.removeSuffix(4);
    } else {
      ERROR2(state, argvObj, "set", "invalid option format: %s", toPrintable(sub).c_str());
      return 1;
    }
    const auto option = recognizeRuntimeOption(sub);
    if (empty(option)) {
      ERROR2(state, argvObj, "set", "unrecognized runtime option: %s", toPrintable(sub).c_str());
      return 1;
    }

    // set or unset
    if (option == RuntimeOption::MONITOR) {
      setJobControlSignalSetting(state, set);
    }
    auto opt = state.getOption();
    if (set) {
      setFlag(opt, option);
    } else {
      unsetFlag(opt, option);
    }
    state.setOption(opt);
  }
  return 0;
}

static int setOption(ARState &state, const ArrayObject &argvObj, const unsigned int offset,
                     const bool set) {
  const unsigned int size = argvObj.size();
  const StringRef subCmd = set ? "set" : "unset";
  if (offset == size) {
    if (set) {
      int errNum = showOptions(state);
      CHECK_STDOUT_ERROR2(state, argvObj, subCmd, errNum);
      return 0;
    } else {
      ERROR2(state, argvObj, subCmd, "requires argument");
      return 2;
    }
  }

  bool dump = false;
  bool restore = false;
  GetOptState optState(":dr:", 2, false);
  if (set) {
    switch (optState(argvObj)) {
    case 'd':
      dump = true;
      break;
    case 'r':
      restore = true;
      break;
    case '?':
      return invalidOptionError(state, argvObj, optState);
    case ':':
      ERROR2(state, argvObj, subCmd, "-%c: option requires argument", optState.optOpt);
      return 1;
    default:
      break;
    }
  }

  if (dump) {
    std::string value;
    for (auto &e : getRuntimeOptionEntries()) {
      value += e.name;
      value += "=";
      value += state.has(e.option) ? "on" : "off";
      value += " ";
    }
    state.setGlobal(BuiltinVarOffset::REPLY, Value::createStr(std::move(value)));
    return 0;
  }
  if (restore) {
    return restoreOptions(state, argvObj, optState.optArg);
  }

  // set/unset option
  bool foundMonitor = false;
  for (unsigned int i = offset; i < size; i++) {
    auto name = argvObj.getValues()[i].asStrRef();
    auto option = recognizeRuntimeOption(name);
    if (empty(option)) {
      ERROR2(state, argvObj, subCmd, "unrecognized runtime option: %s", toPrintable(name).c_str());
      return 1;
    }
    if (option == RuntimeOption::MONITOR && !foundMonitor) {
      foundMonitor = true;
      setJobControlSignalSetting(state, set);
    }
    auto opt = state.getOption();
    if (set) {
      setFlag(opt, option);
    } else {
      unsetFlag(opt, option);
    }
    state.setOption(opt);
  }
  return 0;
}

static int showModule(const ARState &state, const ArrayObject &argvObj, const unsigned int offset,
                      StringRef subCmd) {
  const unsigned int size = argvObj.size();
  if (offset == size) {
    int errNum = 0;
    for (auto &e : state.modLoader) {
      errno = 0;
      if (printf("%s\n", e.first.get()) < 0) {
        errNum = errno;
        break;
      }
    }
    CHECK_STDOUT_ERROR2(state, argvObj, subCmd, errNum);
    return 0;
  }

  FakeModuleLoader loader(state.sysConfig);
  auto cwd = getCWD();
  int lastStatus = 0;
  int errNum = 0;
  for (unsigned int i = offset; i < size; i++) {
    auto ref = argvObj.getValues()[i].asStrRef();
    if (ref.hasNullChar()) {
      ERROR2(state, argvObj, subCmd, "contains null characters: %s", toPrintable(ref).c_str());
      lastStatus = 1;
      continue;
    }

    FilePtr file;
    auto ret = loader.load(cwd.get(), ref.data(), file, ModLoadOption::IGNORE_NON_REG_FILE);
    if (is<const char *>(ret)) {
      const char *path = get<const char *>(ret);
      errno = 0;
      if (printf("%s\n", path) < 0 || fflush(stdout) == EOF /* due to preserve output order */) {
        errNum = errno;
        break;
      }
      lastStatus = 0;
    } else {
      assert(is<ModLoadingError>(ret));
      auto &e = get<ModLoadingError>(ret);
      assert(e.getErrNo() > 0); // always return valid errno
      errno = e.getErrNo();
      PERROR2(state, argvObj, subCmd, "%s", ref.data());
      lastStatus = 1;
    }
  }
  CHECK_STDOUT_ERROR2(state, argvObj, subCmd, errNum);
  return lastStatus;
}

static int isSourced(const VMState &st) {
  if (st.getFrame().code->is(CodeKind::NATIVE)) {
    return 1;
  }

  auto *top = cast<CompiledCode>(st.getFrame().code);
  auto *bottom = top;
  st.walkFrames([&](const ControlFrame &frame) {
    auto *c = frame.code;
    if (!c->is(CodeKind::NATIVE)) {
      bottom = cast<CompiledCode>(c);
    }
    return true;
  });
  return top->getBelongedModId() == bottom->getBelongedModId() ? 1 : 0;
}

static int setAndPrintConf(OrderedMapObject &mapObj, unsigned int maxKeyLen, StringRef key,
                           const std::string &value) {
  errno = 0;
  int s =
      printf("%-*s%s\n", static_cast<int>(maxKeyLen + 4), key.toString().c_str(), value.c_str());
  if (s < 0) {
    return errno;
  }
  auto pair = mapObj.insert(Value::createStr(key), Value::createStr(value));
  assert(pair.second);
  (void)pair;
  return 0;
}

static int showInfo(ARState &state, const ArrayObject &argvObj, StringRef subCmd) {
  const char *table[] = {
#define GEN_STR(E, S) S,
      EACH_SYSCONFIG(GEN_STR)
#undef GEN_STR
  };

  unsigned int maxKeyLen = 0;
  for (auto &e : table) {
    unsigned int len = strlen(e);
    if (len > maxKeyLen) {
      maxKeyLen = len;
    }
  }

  reassignReplyVar(state);
  auto &mapObj = typeAs<OrderedMapObject>(state.getGlobal(BuiltinVarOffset::REPLY_VAR));
  int errNum = 0;
  for (auto &e : table) {
    auto *ptr = state.sysConfig.lookup(e);
    assert(ptr);
    errNum = setAndPrintConf(mapObj, maxKeyLen, e, *ptr);
    if (errNum != 0) {
      break;
    }
  }
  CHECK_STDOUT_ERROR2(state, argvObj, subCmd, errNum);
  return 0;
}

static int checkWinSize(ARState &state, const ArrayObject &argvObj, StringRef subCmd) {
  if (WinSize size; syncWinSize(state, -1, &size)) {
    int errNum = 0;
    if (printf("LINES=%d\nCOLUMNS=%d\n", size.rows, size.cols) < 0) {
      errNum = errno;
    }
    CHECK_STDOUT_ERROR2(state, argvObj, subCmd, errNum);
    return 0;
  }
  PERROR2(state, argvObj, subCmd, "get pty window size failed");
  return 1;
}

static const SHCTLSubCmdEntry *lookupSubCmd(StringRef subCmd) {
  for (auto &e : getSHCTLSubCmdEntries()) {
    if (subCmd == e.name) {
      return &e;
    }
  }
  return nullptr;
}

int builtin_shctl(ARState &state, ArrayObject &argvObj) {
  GetOptState optState("h");
  for (int opt; (opt = optState(argvObj)) != -1;) {
    if (opt == 'h') {
      return showHelp(argvObj);
    } else {
      return invalidOptionError(state, argvObj, optState);
    }
  }

  if (unsigned int index = optState.index; index < argvObj.size()) {
    auto subCmd = argvObj.getValues()[index].asStrRef();
    auto *entry = lookupSubCmd(subCmd);
    if (!entry) {
      ERROR(state, argvObj, "undefined subcommand: %s", toPrintable(subCmd).c_str());
      return 2;
    }
    switch (entry->kind) {
    case SHCTLSubCmdEntry::Kind::INTERACTIVE:
      return state.isInteractive ? 0 : 1;
    case SHCTLSubCmdEntry::Kind::SOURCED:
      return isSourced(state.getCallStack());
    case SHCTLSubCmdEntry::Kind::BACKTRACE:
      return printBacktrace(state, argvObj, subCmd);
    case SHCTLSubCmdEntry::Kind::FUNCTION:
      return printFuncName(state, argvObj, subCmd);
    case SHCTLSubCmdEntry::Kind::MODULE:
      return showModule(state, argvObj, index + 1, subCmd);
    case SHCTLSubCmdEntry::Kind::SET:
    case SHCTLSubCmdEntry::Kind::UNSET:
      return setOption(state, argvObj, index + 1, entry->kind == SHCTLSubCmdEntry::Kind::SET);
    case SHCTLSubCmdEntry::Kind::INFO:
      return showInfo(state, argvObj, subCmd);
    case SHCTLSubCmdEntry::Kind::WINSIZE:
      return checkWinSize(state, argvObj, subCmd);
    }
  }
  return 0;
}

} // namespace arsh