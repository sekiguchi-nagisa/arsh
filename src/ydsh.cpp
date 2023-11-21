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
#include "logger.h"
#include "misc/files.hpp"
#include "misc/num_util.hpp"
#include "vm.h"
#include <embed.h>
#include <ydsh/ydsh.h>

using namespace ydsh;

static DSError initDSError() {
  return DSError{.kind = DS_ERROR_KIND_SUCCESS,
                 .fileName = nullptr,
                 .lineNum = 0,
                 .chars = 0,
                 .name = nullptr};
}

static DefaultErrorConsumer newErrorConsumer(DSError *e) {
#ifdef FUZZING_BUILD_MODE
  bool ignore = getenv("YDSH_SUPPRESS_COMPILE_ERROR") != nullptr;
  return {e, ignore ? nullptr : stderr};
#else
  return {e, stderr};
#endif
}

static int compile(DSState &state, DefaultModuleProvider &moduleProvider,
                   std::unique_ptr<FrontEnd::Context> &&ctx, CompileOption compileOption,
                   DSError *dsError, ObjPtr<FuncObject> &func) {
  if (dsError) {
    *dsError = initDSError();
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

static int evalScript(DSState &state, DefaultModuleProvider &moduleProvider,
                      std::unique_ptr<FrontEnd::Context> &&ctx, CompileOption compileOption,
                      const DiscardPoint &point, DSError *dsError) {
  ObjPtr<FuncObject> func;
  int ret = compile(state, moduleProvider, std::move(ctx), compileOption, dsError, func);
  if (ret != 0) {
    moduleProvider.discard(point);
  }
  if (!func) {
    return ret;
  }

  if (state.execMode == DS_EXEC_MODE_COMPILE_ONLY) {
    return 0;
  }
  VM::callToplevel(state, func, dsError);
  return state.getMaskedExitStatus();
}

struct BindingConsumer {
  DSState &state;

  explicit BindingConsumer(DSState &st)
    : state(st) {
  }

  void operator()(const Handle &handle, int64_t v) {
    this->state.setGlobal(handle.getIndex(), DSValue::createInt(v));
  }

  void operator()(const Handle &handle, const std::string &v) {
    this->state.setGlobal(handle.getIndex(), DSValue::createStr(v));
  }

  void operator()(const Handle &handle, FILE *fp) {
    assert(fp);
    int fd = fileno(fp);
    assert(fd > -1);
    this->state.setGlobal(handle.getIndex(), DSValue::create<UnixFdObject>(fd));
  }

  void operator()(const Handle &handle, const DSType &type) {
    auto value = DSValue::createDummy(type);
    if (type.isArrayType()) {
      value = DSValue::create<ArrayObject>(type);
    } else if (type.isMapType()) {
      value = DSValue::create<OrderedMapObject>(type, this->state.getRng().next());
    }
    this->state.setGlobal(handle.getIndex(), std::move(value));
  }
};

static void loadEmbeddedScript(DSState *state, const NameScopePtr &builtin) {
  state->rootModScope = builtin; // eval script in builtin module

  int ret = DSState_eval(state, "(builtin)", embed_script, strlen(embed_script), nullptr);
  (void)ret;
  assert(ret == 0);

  // rest some state
  auto &modType = state->typePool.getBuiltinModType();
  auto handle = builtin->lookup(VAR_TERM_HOOK);
  assert(handle);
  state->termHookIndex = handle.asOk()->getIndex();
  state->rootModScope = state->modLoader.createGlobalScope(state->typePool, "(root)", &modType);
  state->modLoader.createModType(state->typePool, *state->rootModScope);
  DSState_eval(state, "(root)", "", 0, nullptr); // dummy
  state->lineNum = 1;
  state->setExitStatus(0);
}

// ###################################
// ##     public api of DSState     ##
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

DSState *DSState_createWithMode(DSExecMode mode) {
#define EACH_DS_EXEC_MODE(OP)                                                                      \
  OP(DS_EXEC_MODE_NORMAL)                                                                          \
  OP(DS_EXEC_MODE_PARSE_ONLY)                                                                      \
  OP(DS_EXEC_MODE_CHECK_ONLY)                                                                      \
  OP(DS_EXEC_MODE_COMPILE_ONLY)

  GUARD_ENUM_RANGE(mode, EACH_DS_EXEC_MODE, nullptr);
#undef EACH_DS_EXEC_MODE

  auto *ctx = new DSState();
  auto builtin = ctx->modLoader.createGlobalScope(ctx->typePool, "(builtin)");
  BindingConsumer bindingConsumer(*ctx);
  bindBuiltins(bindingConsumer, ctx->sysConfig, ctx->typePool, *builtin);

  loadEmbeddedScript(ctx, builtin);

  ctx->execMode = mode;
  return ctx;
}

void DSState_delete(DSState **st) {
  if (st != nullptr) {
    VM::callTermHook(**st);
    delete (*st);
    *st = nullptr;
  }
}

DSExecMode DSState_mode(const DSState *st) {
  GUARD_NULL(st, DS_EXEC_MODE_NORMAL);
  return st->execMode;
}

void DSState_setLineNum(DSState *st, unsigned int lineNum) {
  GUARD_NULL(st);
  st->lineNum = lineNum;
}

unsigned int DSState_lineNum(const DSState *st) {
  GUARD_NULL(st, 0);
  return st->lineNum;
}

void DSState_setShellName(DSState *st, const char *shellName) {
  GUARD_NULL(st);
  if (shellName != nullptr) {
    st->setGlobal(BuiltinVarOffset::POS_0, DSValue::createStr(shellName));
  }
}

int DSState_setArguments(DSState *st, char *const *args) {
  GUARD_NULL(st, 0);

  auto value = DSValue::create<ArrayObject>(st->typePool.get(TYPE::StringArray));
  if (args) {
    auto &argsObj = typeAs<ArrayObject>(value);
    for (unsigned int i = 0; args[i] != nullptr; i++) {
      if (!argsObj.append(*st, DSValue::createStr(args[i]))) {
        return -1;
      }
    }
  }
  st->setGlobal(BuiltinVarOffset::ARGS, std::move(value));
  return 0;
}

int DSState_exitStatus(const DSState *st) {
  GUARD_NULL(st, 0);
  return st->getMaskedExitStatus();
}

int DSState_setDumpTarget(DSState *st, DSDumpKind kind, const char *target) {
  GUARD_NULL(st, -1);

#define EACH_DSDUMP_KIND(OP)                                                                       \
  OP(DS_DUMP_KIND_UAST)                                                                            \
  OP(DS_DUMP_KIND_AST)                                                                             \
  OP(DS_DUMP_KIND_CODE)

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

unsigned int DSState_option(const DSState *st) {
  GUARD_NULL(st, 0);

  unsigned int option = 0;

  // get compile option
  if (st->isInteractive) {
    setFlag(option, DS_OPTION_INTERACTIVE);
  }

  // get runtime option
  if (hasFlag(st->runtimeOption, RuntimeOption::ASSERT)) {
    setFlag(option, DS_OPTION_ASSERT);
  }
  if (hasFlag(st->runtimeOption, RuntimeOption::TRACE_EXIT)) {
    setFlag(option, DS_OPTION_TRACE_EXIT);
  }
  if (hasFlag(st->runtimeOption, RuntimeOption::MONITOR)) {
    setFlag(option, DS_OPTION_JOB_CONTROL);
  }
  return option;
}

void DSState_setOption(DSState *st, unsigned int optionSet) {
  GUARD_NULL(st);

  // set compile option
  if (hasFlag(optionSet, DS_OPTION_INTERACTIVE)) {
    st->isInteractive = true;
    st->jobTable.setNotifyCallback(makeObserver(st->notifyCallback));
  }

  // set runtime option
  if (hasFlag(optionSet, DS_OPTION_ASSERT)) {
    setFlag(st->runtimeOption, RuntimeOption::ASSERT);
  }
  if (hasFlag(optionSet, DS_OPTION_TRACE_EXIT)) {
    setFlag(st->runtimeOption, RuntimeOption::TRACE_EXIT);
  }
  if (hasFlag(optionSet, DS_OPTION_JOB_CONTROL)) {
    setFlag(st->runtimeOption, RuntimeOption::MONITOR);
    setJobControlSignalSetting(*st, true);
  }
  if (hasFlag(optionSet, DS_OPTION_XTRACE)) {
    setFlag(st->runtimeOption, RuntimeOption::XTRACE);
  }
}

void DSState_unsetOption(DSState *st, unsigned int optionSet) {
  GUARD_NULL(st);

  // unset compile option
  if (hasFlag(optionSet, DS_OPTION_INTERACTIVE)) {
    st->isInteractive = false;
    st->jobTable.setNotifyCallback(nullptr);
  }

  // unset runtime option
  if (hasFlag(optionSet, DS_OPTION_ASSERT)) {
    unsetFlag(st->runtimeOption, RuntimeOption::ASSERT);
  }
  if (hasFlag(optionSet, DS_OPTION_TRACE_EXIT)) {
    unsetFlag(st->runtimeOption, RuntimeOption::TRACE_EXIT);
  }
  if (hasFlag(optionSet, DS_OPTION_JOB_CONTROL)) {
    unsetFlag(st->runtimeOption, RuntimeOption::MONITOR);
    setJobControlSignalSetting(*st, false);
  }
}

void DSError_release(DSError *e) {
  int old = errno;
  if (e != nullptr) {
    free(e->fileName);
    e->fileName = nullptr;
    free(e->name);
    e->name = nullptr;
  }
  errno = old;
}

static CompileOption getCompileOption(const DSState &st) {
  CompileOption option{};
  if (st.isInteractive) {
    setFlag(option, CompileOption::PRINT_TOPLEVEL);
  }
  switch (st.execMode) {
  case DS_EXEC_MODE_PARSE_ONLY:
    setFlag(option, CompileOption::PARSE_ONLY);
    break;
  case DS_EXEC_MODE_CHECK_ONLY:
    setFlag(option, CompileOption::CHECK_ONLY);
    break;
  default:
    break;
  }
  return option;
}

static void reportFileError(const char *sourceName, int errNum, DSError *e) {
  fprintf(stderr, "ydsh: cannot load file: %s, by `%s'\n", sourceName, strerror(errNum));
  if (e) {
    *e = {.kind = DS_ERROR_KIND_FILE_ERROR,
          .fileName = strdup(sourceName),
          .lineNum = 0,
          .chars = 0,
          .name = strdup(strerror(errNum))};
  }
  errno = errNum;
}

int DSState_eval(DSState *st, const char *sourceName, const char *data, size_t size, DSError *e) {
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

int DSState_loadModule(DSState *st, const char *fileName, unsigned int option, DSError *e) {
  GUARD_NULL(st, -1);
  GUARD_NULL(fileName, -1);

  CompileOption compileOption = getCompileOption(*st);
  DefaultModuleProvider moduleProvider(st->modLoader, st->typePool, st->rootModScope,
                                       std::make_unique<RuntimeCancelToken>(true));
  auto discardPoint = moduleProvider.getCurrentDiscardPoint();
  CStrPtr scriptDir = hasFlag(option, DS_MOD_FULLPATH) ? nullptr : getCWD();
  auto ret = moduleProvider.load(scriptDir.get(), fileName, ModLoadOption{});
  if (is<ModLoadingError>(ret)) {
    auto error = get<ModLoadingError>(ret);
    if (error.isFileNotFound() && hasFlag(option, DS_MOD_IGNORE_ENOENT)) {
      if (e) {
        *e = initDSError();
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
      *e = initDSError();
    }
    return 0; // do nothing
  }
  assert(is<std::unique_ptr<FrontEnd::Context>>(ret));
  auto &ctx = get<std::unique_ptr<FrontEnd::Context>>(ret);
  if (!hasFlag(option, DS_MOD_SEPARATE_CTX)) {
    setFlag(compileOption, CompileOption::LOAD_TO_ROOT);
  }
  return evalScript(*st, moduleProvider, std::move(ctx), compileOption, discardPoint, e);
}

int DSState_exec(DSState *st, char *const *argv) {
  GUARD_NULL(st, -1);
  GUARD_TRUE(st->execMode != DS_EXEC_MODE_NORMAL, 0);
  GUARD_NULL(argv, -1);

  std::vector<DSValue> values;
  for (; *argv != nullptr; argv++) {
    values.push_back(DSValue::createStr(*argv));
  }
  VM::execCommand(*st, std::move(values), false);
  return st->getMaskedExitStatus();
}

const char *DSState_config(const DSState *st, DSConfig config) {
  GUARD_NULL(st, nullptr);

  const char *key = nullptr;
  switch (config) {
#define GEN_CASE2(E, S)                                                                            \
  case DS_CONFIG_##E:                                                                              \
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

const char *DSState_version(DSVersion *version) {
  if (version != nullptr) {
    version->major = X_INFO_MAJOR_VERSION;
    version->minor = X_INFO_MINOR_VERSION;
    version->patch = X_INFO_PATCH_VERSION;
  }
  return "ydsh, version " X_INFO_VERSION ", build by " X_INFO_CPP " " X_INFO_CPP_V;
}

const char *DSState_copyright() { return "Copyright (C) 2015-2023 Nagisa Sekiguchi"; }

static constexpr unsigned int featureBit() {
  unsigned int featureBit = 0;

#ifdef USE_LOGGING
  setFlag(featureBit, DS_FEATURE_LOGGING);
#endif

#ifdef USE_SAFE_CAST
  setFlag(featureBit, DS_FEATURE_SAFE_CAST);
#endif
  return featureBit;
}

unsigned int DSState_featureBit() {
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

const char *DSState_initExecutablePath(DSState *st) {
  GUARD_NULL(st, nullptr);

  auto handle = st->rootModScope->lookup(VAR_YDSH_BIN);
  assert(handle);
  if (const char *ret = st->getGlobal(handle.asOk()->getIndex()).asCStr(); *ret) {
    return ret;
  }
  if (char *path = getExecutablePath()) {
    st->setGlobal(handle.asOk()->getIndex(), DSValue::createStr(path));
    free(path);
  }
  return nullptr;
}

static const char *defaultPrompt() {
#define XSTR(v) #v
#define STR(v) XSTR(v)

  if (getuid()) {
    return "ydsh-" STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION) "$ ";
  } else {
    return "ydsh-" STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION) "# ";
  }

#undef XSTR
#undef STR
}

ssize_t DSState_readLine(DSState *st, char *buf, size_t bufSize, DSError *e) {
  GUARD_NULL(st, 0);
  st->getCallStack().clearThrownObject();
  st->notifyCallback.showAndClear();
  if (e) {
    *e = initDSError();
  }
  auto &editor = typeAs<LineEditorObject>(getBuiltinGlobal(*st, VAR_LINE_EDIT));
  editor.enableHighlight();
  auto ret = editor.readline(*st, defaultPrompt(), buf, bufSize);
  if (errno == ENOMEM) {
    raiseSystemError(*st, ENOMEM, ERROR_READLINE);
  }
  if (st->hasError()) {
    VM::handleUncaughtException(*st, e);
    st->getCallStack().clearThrownObject();
    errno = EAGAIN;
  }
  return ret;
}
