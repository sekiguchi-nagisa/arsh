/*
 * Copyright (C) 2015-2018 Nagisa Sekiguchi
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

#include <cassert>
#include <cstring>

#include <unistd.h>

#include "binder.h"
#include "compiler.h"
#include "line_editor.h"
#include "misc/files.hpp"
#include "misc/num_util.hpp"
#include "vm.h"
#include <arsh/arsh.h>
#include <embed.h>

using namespace arsh;

static ARError initARError() {
  return ARError{.kind = AR_ERROR_KIND_SUCCESS,
                 .fileName = nullptr,
                 .lineNum = 0,
                 .chars = 0,
                 .name = nullptr};
}

static DefaultErrorConsumer newErrorConsumer(ARError *e) {
#ifdef FUZZING_BUILD_MODE
  bool ignore = getenv("ARSH_SUPPRESS_COMPILE_ERROR") != nullptr;
  return {e, ignore ? nullptr : stderr};
#else
  return {e, stderr};
#endif
}

static int compile(ARState &state, DefaultModuleProvider &moduleProvider,
                   std::unique_ptr<FrontEnd::Context> &&ctx, CompileOption compileOption,
                   ARError *dsError, ObjPtr<FuncObject> &func) {
  if (dsError) {
    *dsError = initARError();
  }

  CompileDumpTarget dumpTarget(state.dumpTarget.files);
  auto errorConsumer = newErrorConsumer(dsError);
  Compiler compiler(moduleProvider, std::move(ctx), compileOption, &dumpTarget, errorConsumer);
  int ret = compiler(func);
  if (!state.lineNum) {
    state.lineNum = compiler.lineNum();
  }
  return ret;
}

static int evalScript(ARState &state, DefaultModuleProvider &moduleProvider,
                      std::unique_ptr<FrontEnd::Context> &&ctx, CompileOption compileOption,
                      const DiscardPoint &point, ARError *dsError) {
  ObjPtr<FuncObject> func;
  int ret = compile(state, moduleProvider, std::move(ctx), compileOption, dsError, func);
  if (ret != 0) {
    moduleProvider.discard(point);
  }
  if (!func) {
    return ret;
  }

  if (state.execMode == AR_EXEC_MODE_COMPILE_ONLY) {
    return 0;
  }
  VM::callToplevel(state, func, dsError);
  return state.getMaskedExitStatus();
}

struct BindingConsumer {
  ARState &state;

  explicit BindingConsumer(ARState &st) : state(st) {}

  void operator()(const Handle &handle, int64_t v) {
    this->state.setGlobal(handle.getIndex(), Value::createInt(v));
  }

  void operator()(const Handle &handle, const std::string &v) {
    this->state.setGlobal(handle.getIndex(), Value::createStr(v));
  }

  void operator()(const Handle &handle, FILE *fp) {
    assert(fp);
    int fd = fileno(fp);
    assert(fd > -1);
    this->state.setGlobal(handle.getIndex(), Value::create<UnixFdObject>(fd));
  }

  void operator()(const Handle &handle, const Type &type) {
    auto value = Value::createDummy(type);
    if (type.isArrayType() || type.is(TYPE::Candidates)) {
      value = Value::create<ArrayObject>(type);
    } else if (type.isMapType()) {
      value = Value::create<OrderedMapObject>(type, this->state.getRng().next());
    }
    this->state.setGlobal(handle.getIndex(), std::move(value));
  }
};

static void loadEmbeddedScript(ARState *state, const NameScopePtr &builtin) {
  state->rootModScope = builtin; // eval script in builtin module

  int ret = ARState_eval(state, "(builtin)", embed_script, strlen(embed_script), nullptr);
  (void)ret;
  assert(ret == 0);

  // rest some state
  auto &modType = state->typePool.getBuiltinModType();
  auto handle = builtin->lookup(VAR_TERM_HOOK);
  assert(handle);
  state->termHookIndex = handle.asOk()->getIndex();
  state->rootModScope = state->modLoader.createGlobalScope(state->typePool, "(root)", &modType);
  state->modLoader.createModType(state->typePool, *state->rootModScope);
  ARState_eval(state, "(root)", "", 0, nullptr); // dummy
  state->lineNum = 1;
  state->setExitStatus(0);
}

// ###################################
// ##     public api of ARState     ##
// ###################################

#define GUARD_TRUE(C, ...)                                                                         \
  do {                                                                                             \
    if (C) {                                                                                       \
      return __VA_ARGS__;                                                                          \
    }                                                                                              \
  } while (false)

#define GEN_CASE(E) case E:

#define CHECK_ENUM_RANGE(val, EACH_ENUM)                                                           \
  ({                                                                                               \
    bool __ret = false;                                                                            \
    switch (val) {                                                                                 \
      EACH_ENUM(GEN_CASE)                                                                          \
      __ret = true;                                                                                \
      break;                                                                                       \
    }                                                                                              \
    __ret;                                                                                         \
  })

#define GUARD_ENUM_RANGE(val, EACH_ENUM, ...)                                                      \
  GUARD_TRUE(!CHECK_ENUM_RANGE(val, EACH_ENUM), ##__VA_ARGS__)

#define GUARD_NULL(arg, ...) GUARD_TRUE(arg == nullptr, ##__VA_ARGS__)

ARState *ARState_createWithMode(ARExecMode mode) {
#define EACH_AR_EXEC_MODE(OP)                                                                      \
  OP(AR_EXEC_MODE_NORMAL)                                                                          \
  OP(AR_EXEC_MODE_PARSE_ONLY)                                                                      \
  OP(AR_EXEC_MODE_CHECK_ONLY)                                                                      \
  OP(AR_EXEC_MODE_COMPILE_ONLY)

  GUARD_ENUM_RANGE(mode, EACH_AR_EXEC_MODE, nullptr);
#undef EACH_AR_EXEC_MODE

  auto *ctx = new ARState();
  auto builtin = ctx->modLoader.createGlobalScope(ctx->typePool, "(builtin)");
  BindingConsumer bindingConsumer(*ctx);
  bindBuiltins(bindingConsumer, ctx->sysConfig, ctx->typePool, *builtin);

  loadEmbeddedScript(ctx, builtin);

  ctx->execMode = mode;
  return ctx;
}

void ARState_delete(ARState **st) {
  if (st != nullptr) {
    VM::callTermHook(**st);
    delete (*st);
    *st = nullptr;
  }
}

ARExecMode ARState_mode(const ARState *st) {
  GUARD_NULL(st, AR_EXEC_MODE_NORMAL);
  return st->execMode;
}

void ARState_setLineNum(ARState *st, unsigned int lineNum) {
  GUARD_NULL(st);
  st->lineNum = lineNum;
}

unsigned int ARState_lineNum(const ARState *st) {
  GUARD_NULL(st, 0);
  return st->lineNum;
}

int ARState_setShellName(ARState *st, const char *shellName) {
  GUARD_NULL(st, 0);
  if (shellName != nullptr) {
    if (const StringRef ref = shellName; ref.size() <= SYS_LIMIT_STRING_MAX) {
      st->setGlobal(BuiltinVarOffset::POS_0, Value::createStr(ref));
      return 0;
    }
  }
  return -1;
}

int ARState_setArguments(ARState *st, char *const *args) {
  GUARD_NULL(st, 0);

  auto value = Value::create<ArrayObject>(st->typePool.get(TYPE::StringArray));
  if (args) {
    auto &argsObj = typeAs<ArrayObject>(value);
    for (; *args != nullptr; args++) {
      if (const StringRef arg = *args;
          arg.size() > SYS_LIMIT_STRING_MAX || !argsObj.append(*st, Value::createStr(arg))) {
        return -1;
      }
    }
  }
  st->setGlobal(BuiltinVarOffset::ARGS, std::move(value));
  return 0;
}

int ARState_exitStatus(const ARState *st) {
  GUARD_NULL(st, 0);
  return st->getMaskedExitStatus();
}

int ARState_setDumpTarget(ARState *st, ARDumpKind kind, const char *target) {
  GUARD_NULL(st, -1);

#define EACH_DSDUMP_KIND(OP)                                                                       \
  OP(AR_DUMP_KIND_UAST)                                                                            \
  OP(AR_DUMP_KIND_AST)                                                                             \
  OP(AR_DUMP_KIND_CODE)

  GUARD_ENUM_RANGE(kind, EACH_DSDUMP_KIND, -1);
#undef EACH_DSDUMP_KIND

  FilePtr file;
  if (target != nullptr) {
    if (strlen(target) == 0) {
      file.reset(fdopen(dupFDCloseOnExec(STDOUT_FILENO), "w"));
    } else if (FILE *fp = fopen(target, "w")) {
      int fd = dupFDCloseOnExec(fileno(fp));
      fclose(fp);
      file.reset(fdopen(fd, "w"));
    }
    if (!file) {
      return -1;
    }
  }
  st->dumpTarget.files[kind] = std::move(file);
  return 0;
}

unsigned int ARState_option(const ARState *st) {
  GUARD_NULL(st, 0);

  unsigned int option = 0;

  // get compile option
  if (st->isInteractive) {
    setFlag(option, AR_OPTION_INTERACTIVE);
  }

  // get runtime option
  if (st->has(RuntimeOption::ASSERT)) {
    setFlag(option, AR_OPTION_ASSERT);
  }
  if (st->has(RuntimeOption::TRACE_EXIT)) {
    setFlag(option, AR_OPTION_TRACE_EXIT);
  }
  if (st->has(RuntimeOption::MONITOR)) {
    setFlag(option, AR_OPTION_JOB_CONTROL);
  }
  return option;
}

void ARState_setOption(ARState *st, unsigned int optionSet) {
  GUARD_NULL(st);

  // set compile option
  if (hasFlag(optionSet, AR_OPTION_INTERACTIVE)) {
    st->isInteractive = true;
    st->jobTable.setNotifyCallback(makeObserver(st->notifyCallback));
  }

  // set runtime option
  auto option = st->getOption();
  if (hasFlag(optionSet, AR_OPTION_ASSERT)) {
    setFlag(option, RuntimeOption::ASSERT);
  }
  if (hasFlag(optionSet, AR_OPTION_TRACE_EXIT)) {
    setFlag(option, RuntimeOption::TRACE_EXIT);
  }
  if (hasFlag(optionSet, AR_OPTION_JOB_CONTROL)) {
    setFlag(option, RuntimeOption::MONITOR);
    setJobControlSignalSetting(*st, true);
  }
  if (hasFlag(optionSet, AR_OPTION_XTRACE)) {
    setFlag(option, RuntimeOption::XTRACE);
  }
  st->setOption(option);
}

void ARState_unsetOption(ARState *st, unsigned int optionSet) {
  GUARD_NULL(st);

  // unset compile option
  if (hasFlag(optionSet, AR_OPTION_INTERACTIVE)) {
    st->isInteractive = false;
    st->jobTable.setNotifyCallback(nullptr);
  }

  // unset runtime option
  auto option = st->getOption();
  if (hasFlag(optionSet, AR_OPTION_ASSERT)) {
    unsetFlag(option, RuntimeOption::ASSERT);
  }
  if (hasFlag(optionSet, AR_OPTION_TRACE_EXIT)) {
    unsetFlag(option, RuntimeOption::TRACE_EXIT);
  }
  if (hasFlag(optionSet, AR_OPTION_JOB_CONTROL)) {
    unsetFlag(option, RuntimeOption::MONITOR);
    setJobControlSignalSetting(*st, false);
  }
  st->setOption(option);
}

void ARError_release(ARError *e) {
  int old = errno;
  if (e != nullptr) {
    free(e->fileName);
    e->fileName = nullptr;
    free(e->name);
    e->name = nullptr;
  }
  errno = old;
}

static CompileOption getCompileOption(const ARState &st) {
  CompileOption option{};
  if (st.isInteractive) {
    setFlag(option, CompileOption::PRINT_TOPLEVEL);
  }
  switch (st.execMode) {
  case AR_EXEC_MODE_PARSE_ONLY:
    setFlag(option, CompileOption::PARSE_ONLY);
    break;
  case AR_EXEC_MODE_CHECK_ONLY:
    setFlag(option, CompileOption::CHECK_ONLY);
    break;
  default:
    break;
  }
  return option;
}

static void reportFileError(const char *sourceName, int errNum, ARError *e) {
  fprintf(stderr, "arsh: cannot load file: %s, by `%s'\n", sourceName, strerror(errNum));
  if (e) {
    *e = {.kind = AR_ERROR_KIND_FILE_ERROR,
          .fileName = strdup(sourceName),
          .lineNum = 0,
          .chars = 0,
          .name = strdup(strerror(errNum))};
  }
  errno = errNum;
}

int ARState_eval(ARState *st, const char *sourceName, const char *data, size_t size, ARError *e) {
  GUARD_NULL(st, -1);
  GUARD_NULL(data, -1);

  if (!sourceName) {
    sourceName = "(stdin)";
  }
  if (size > SYS_LIMIT_INPUT_SIZE) {
    reportFileError(sourceName, EFBIG, e);
    return 1;
  }

  const auto compileOption = getCompileOption(*st);
  DefaultModuleProvider moduleProvider(st->modLoader, st->typePool, st->rootModScope,
                                       std::make_unique<RuntimeCancelToken>(true));
  auto discardPoint = moduleProvider.getCurrentDiscardPoint();
  auto lexer = LexerPtr::create(sourceName, ByteBuffer(data, data + size), getCWD());
  lexer->setLineNumOffset(st->lineNum);
  st->lineNum = 0;
  auto ctx = moduleProvider.newContext(std::move(lexer));
  return evalScript(*st, moduleProvider, std::move(ctx), compileOption, discardPoint, e);
}

int ARState_loadModule(ARState *st, const char *fileName, unsigned int option, ARError *e) {
  GUARD_NULL(st, -1);
  GUARD_NULL(fileName, -1);

  CompileOption compileOption = getCompileOption(*st);
  DefaultModuleProvider moduleProvider(st->modLoader, st->typePool, st->rootModScope,
                                       std::make_unique<RuntimeCancelToken>(true));
  auto discardPoint = moduleProvider.getCurrentDiscardPoint();
  CStrPtr scriptDir = hasFlag(option, AR_MOD_FULLPATH) ? nullptr : getCWD();
  auto ret = moduleProvider.load(scriptDir.get(), fileName, ModLoadOption{});
  if (is<ModLoadingError>(ret)) {
    auto error = get<ModLoadingError>(ret);
    if (error.isFileNotFound() && hasFlag(option, AR_MOD_IGNORE_ENOENT)) {
      if (e) {
        *e = initARError();
      }
      return 0;
    }
    int errNum = error.getErrNo();
    if (error.isCircularLoad()) {
      errNum = ETXTBSY;
    } else if (error.isModLimit() || error.isVarLimit()) {
      errNum = EPERM;
    }
    reportFileError(fileName, errNum, e);
    return 1;
  } else if (is<const ModType *>(ret)) {
    if (e) {
      *e = initARError();
    }
    return 0; // do nothing
  }
  assert(is<std::unique_ptr<FrontEnd::Context>>(ret));
  auto &ctx = get<std::unique_ptr<FrontEnd::Context>>(ret);
  if (!hasFlag(option, AR_MOD_SEPARATE_CTX)) {
    setFlag(compileOption, CompileOption::LOAD_TO_ROOT);
  }
  return evalScript(*st, moduleProvider, std::move(ctx), compileOption, discardPoint, e);
}

int ARState_exec(ARState *st, char *const *argv) {
  GUARD_NULL(st, -1);
  GUARD_TRUE(st->execMode != AR_EXEC_MODE_NORMAL, 0);
  GUARD_NULL(argv, -1);

  std::vector<Value> values;
  for (; *argv != nullptr; argv++) {
    const StringRef arg = *argv;
    if (arg.size() > SYS_LIMIT_STRING_MAX || values.size() == SYS_LIMIT_ARRAY_MAX) {
      return -1;
    }
    values.push_back(Value::createStr(arg));
  }
  VM::execCommand(*st, std::move(values), false);
  return st->getMaskedExitStatus();
}

const char *ARState_config(const ARState *st, ARConfig config) {
  GUARD_NULL(st, nullptr);

  const char *key = nullptr;
  switch (config) {
#define GEN_CASE2(E, S)                                                                            \
  case AR_CONFIG_##E:                                                                              \
    key = SysConfig::E;                                                                            \
    break;
    EACH_SYSCONFIG(GEN_CASE2)
#undef GEN_CASE2
  }
  if (auto *value = st->sysConfig.lookup(key)) {
    return value->c_str();
  }
  return nullptr;
}

const char *ARState_version(ARVersion *version) {
  if (version != nullptr) {
    version->major = X_INFO_MAJOR_VERSION;
    version->minor = X_INFO_MINOR_VERSION;
    version->patch = X_INFO_PATCH_VERSION;
  }
  return "arsh, version " X_INFO_VERSION ", build by " X_INFO_CPP " " X_INFO_CPP_V;
}

const char *ARState_copyright() { return "Copyright (C) 2015-2024 Nagisa Sekiguchi"; }

static constexpr unsigned int featureBit() {
  unsigned int featureBit = 0;

#ifdef USE_LOGGING
  setFlag(featureBit, AR_FEATURE_LOGGING);
#endif

#ifdef USE_SAFE_CAST
  setFlag(featureBit, AR_FEATURE_SAFE_CAST);
#endif
  return featureBit;
}

unsigned int ARState_featureBit() {
  constexpr auto flag = featureBit();
  return flag;
}

#ifdef __APPLE__
#include <mach-o/dyld.h>

static char *getExecutablePath() {
  uint32_t bufSize = 0;
  _NSGetExecutablePath(nullptr, &bufSize); // get buffer size
  char *buf = static_cast<char *>(malloc(sizeof(char) * (bufSize + 1)));
  char *real = nullptr;
  if (_NSGetExecutablePath(buf, &bufSize) == 0) {
    real = realpath(buf, nullptr);
  }
  free(buf);
  return real;
}

#else
static char *getExecutablePath() {
  size_t bufSize = 16;
  char *buf = nullptr;
  ssize_t len;
  do {
    bufSize += (bufSize >> 1u);
    if (auto *ptr = static_cast<char *>(realloc(buf, sizeof(char) * bufSize))) {
      buf = ptr;
    } else {
      free(buf);
      return nullptr;
    }
    len = readlink("/proc/self/exe", buf, bufSize);
    if (len < 0) {
      free(buf);
      return nullptr;
    }
  } while (static_cast<size_t>(len) == bufSize);
  buf[len] = '\0';
  return buf;
}

#endif

const char *ARState_initExecutablePath(ARState *st) {
  GUARD_NULL(st, nullptr);

  auto handle = st->rootModScope->lookup(VAR_BIN_NAME);
  assert(handle);
  const char *ret = st->getGlobal(handle.asOk()->getIndex()).asCStr();
  if (*ret) {
    return ret;
  }
  char *path = getExecutablePath();
  if (path) {
    st->setGlobal(handle.asOk()->getIndex(), Value::createStr(path));
    free(path);
  }
  return nullptr;
}

static const char *defaultPrompt() {
#define XSTR(v) #v
#define STR(v) XSTR(v)

  if (getuid()) {
    return "arsh-" STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION) "$ ";
  } else {
    return "arsh-" STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION) "# ";
  }

#undef XSTR
#undef STR
}

ssize_t ARState_readLine(ARState *st, char *buf, size_t bufSize, ARError *e) {
  GUARD_NULL(st, 0);
  st->getCallStack().clearThrownObject();
  st->notifyCallback.showAndClear();
  if (e) {
    *e = initARError();
  }
  /**
   * always be (termianl) foreground process
   * some background process may change termianl foreground setting
   */
  beForeground(getpid());
  auto &editor = typeAs<LineEditorObject>(getBuiltinGlobal(*st, VAR_LINE_EDIT));
  auto ret = editor.readline(*st, defaultPrompt(), buf, bufSize);
  if (errno == ENOMEM) {
    raiseSystemError(*st, ENOMEM, ERROR_READLINE);
  }
  if (st->hasError()) {
    VM::handleUncaughtException(*st, e);
    st->getCallStack().clearThrownObject();
    errno = EAGAIN;
  }

  /**
   * after readline, STDIN should be blocking like other shells
   */
  const int old = errno;
  setFDFlag(STDIN_FILENO, O_NONBLOCK, false);
  errno = old;
  return ret;
}
