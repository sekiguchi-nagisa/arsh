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
#include "ordered_map.h"
#include "vm.h"

namespace ydsh {

static int printBacktrace(const VMState &state) {
  state.fillStackTrace([](StackTraceElement &&s) {
    printf("from %s:%d '%s()'\n", s.getSourceName().c_str(), s.getLineNum(),
           s.getCallerName().c_str());
    return true;
  });
  return 0;
}

static int printFuncName(const VMState &state) {
  auto *code = state.getFrame().code;
  const char *name = nullptr;
  if (!code->is(CodeKind::NATIVE) && !code->is(CodeKind::TOPLEVEL)) {
    name = cast<CompiledCode>(code)->getName();
  }
  printf("%s\n", name != nullptr ? name : "<toplevel>");
  return name != nullptr ? 0 : 1;
}

static constexpr struct {
  RuntimeOption option;
  const char *name;
} runtimeOptions[] = {
#define GEN_OPT(E, V, N) {RuntimeOption::E, N},
    EACH_RUNTIME_OPTION(GEN_OPT)
#undef GEN_OPT
};

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

  for (auto &e : runtimeOptions) {
    if (optName == e.name) {
      return e.option;
    }
  }
  return RuntimeOption{};
}

static unsigned int computeMaxOptionNameSize() {
  unsigned int maxSize = 0;
  for (auto &e : runtimeOptions) {
    unsigned int size = strlen(e.name) + 2;
    if (size > maxSize) {
      maxSize = size;
    }
  }
  return maxSize;
}

static void showOptions(const DSState &state) {
  const unsigned int maxNameSize = computeMaxOptionNameSize();
  for (auto &e : runtimeOptions) {
    printf("%-*s%s\n", static_cast<int>(maxNameSize), e.name,
           hasFlag(state.runtimeOption, e.option) ? "on" : "off");
  }
}

static int restoreOptions(DSState &state, const ArrayObject &argvObj, StringRef restoreStr) {
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
      ERROR(argvObj, "invalid option format: %s", toPrintable(sub).c_str());
      return 1;
    }
    const auto option = recognizeRuntimeOption(sub);
    if (empty(option)) {
      ERROR(argvObj, "unrecognized runtime option: %s", toPrintable(sub).c_str());
      return 1;
    }

    // set or unset
    if (option == RuntimeOption::MONITOR) {
      setJobControlSignalSetting(state, set);
    }
    if (set) {
      setFlag(state.runtimeOption, option);
    } else {
      unsetFlag(state.runtimeOption, option);
    }
  }
  return 0;
}

static int setOption(DSState &state, const ArrayObject &argvObj, const unsigned int offset,
                     const bool set) {
  const unsigned int size = argvObj.size();
  if (offset == size) {
    if (set) {
      showOptions(state);
      return 0;
    } else {
      ERROR(argvObj, "`unset' subcommand requires argument");
      return 2;
    }
  }

  bool dump = false;
  bool restore = false;
  GetOptState optState(2, false);
  if (set) {
    switch (optState(argvObj, ":dr:")) {
    case 'd':
      dump = true;
      break;
    case 'r':
      restore = true;
      break;
    case '?':
      return invalidOptionError(argvObj, optState);
    case ':':
      ERROR(argvObj, "-%c: option requires argument", optState.optOpt);
      return 1;
    default:
      break;
    }
  }

  if (dump) {
    std::string value;
    for (auto &e : runtimeOptions) {
      value += e.name;
      value += "=";
      value += hasFlag(state.runtimeOption, e.option) ? "on" : "off";
      value += " ";
    }
    state.setGlobal(BuiltinVarOffset::REPLY, DSValue::createStr(std::move(value)));
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
      ERROR(argvObj, "unrecognized runtime option: %s", toPrintable(name).c_str());
      return 1;
    }
    if (option == RuntimeOption::MONITOR && !foundMonitor) {
      foundMonitor = true;
      setJobControlSignalSetting(state, set);
    }
    if (set) {
      setFlag(state.runtimeOption, option);
    } else {
      unsetFlag(state.runtimeOption, option);
    }
  }
  return 0;
}

static int showModule(const DSState &state, const ArrayObject &argvObj, const unsigned int offset) {
  const unsigned int size = argvObj.size();
  if (offset == size) {
    for (auto &e : state.modLoader) {
      printf("%s\n", e.first.get());
    }
    return 0;
  }

  FakeModuleLoader loader(state.sysConfig);
  auto cwd = getCWD();
  int lastStatus = 0;
  for (unsigned int i = offset; i < size; i++) {
    auto ref = argvObj.getValues()[i].asStrRef();
    if (ref.hasNullChar()) {
      ERROR(argvObj, "contains null characters: %s", toPrintable(ref).c_str());
      lastStatus = 1;
      continue;
    }

    FilePtr file;
    auto ret = loader.load(cwd.get(), ref.data(), file, ModLoadOption::IGNORE_NON_REG_FILE);
    if (is<const char *>(ret)) {
      const char *path = get<const char *>(ret);
      printf("%s\n", path);
      fflush(stdout); // due to preserve output order
      lastStatus = 0;
    } else {
      assert(is<ModLoadingError>(ret));
      auto &e = get<ModLoadingError>(ret);
      assert(e.getErrNo() > 0); // always return valid errno
      errno = e.getErrNo();
      PERROR(argvObj, "%s", ref.data());
      lastStatus = 1;
    }
  }
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

static void setAndPrintConf(OrderedMapObject &mapObj, unsigned int maxKeyLen, StringRef key,
                            const std::string &value) {
  printf("%-*s%s\n", static_cast<int>(maxKeyLen + 4), key.toString().c_str(), value.c_str());
  auto pair = mapObj.insert(DSValue::createStr(key), DSValue::createStr(value));
  assert(pair.second);
  (void)pair;
}

static int showInfo(DSState &state) {
  auto &mapObj = typeAs<OrderedMapObject>(state.getGlobal(BuiltinVarOffset::REPLY_VAR));
  if (unlikely(!mapObj.checkIteratorInvalidation(state, true))) {
    return 1;
  }
  mapObj.clear();

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

  for (auto &e : table) {
    auto *ptr = state.sysConfig.lookup(e);
    assert(ptr);
    setAndPrintConf(mapObj, maxKeyLen, e, *ptr);
  }
  return 0;
}

int builtin_shctl(DSState &state, ArrayObject &argvObj) {
  GetOptState optState;
  for (int opt; (opt = optState(argvObj, "h")) != -1;) {
    if (opt == 'h') {
      return showHelp(argvObj);
    } else {
      return invalidOptionError(argvObj, optState);
    }
  }

  if (unsigned int index = optState.index; index < argvObj.size()) {
    auto ref = argvObj.getValues()[index].asStrRef();
    if (ref == "backtrace") {
      return printBacktrace(state.getCallStack());
    } else if (ref == "is-sourced") {
      return isSourced(state.getCallStack());
    } else if (ref == "is-interactive") {
      return state.isInteractive ? 0 : 1;
    } else if (ref == "function") {
      return printFuncName(state.getCallStack());
    } else if (ref == "set") {
      return setOption(state, argvObj, index + 1, true);
    } else if (ref == "unset") {
      return setOption(state, argvObj, index + 1, false);
    } else if (ref == "module") {
      return showModule(state, argvObj, index + 1);
    } else if (ref == "info") {
      return showInfo(state);
    } else {
      ERROR(argvObj, "undefined subcommand: %s", toPrintable(ref).c_str());
      return 2;
    }
  }
  return 0;
}

} // namespace ydsh