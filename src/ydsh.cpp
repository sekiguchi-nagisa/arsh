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

#include <algorithm>
#include <cassert>
#include <cstring>

#include <unistd.h>

#include "binder.h"
#include "compiler.h"
#include "logger.h"
#include "misc/files.h"
#include "misc/grapheme.hpp"
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
  return DefaultErrorConsumer(e, ignore ? nullptr : stderr);
#else
  return DefaultErrorConsumer(e, stderr);
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

  explicit BindingConsumer(DSState &st) : state(st) {}

  void operator()(const Handle &handle, int64_t v) {
    this->state.setGlobal(handle.getIndex(), DSValue::createInt(v));
  }

  void operator()(const Handle &handle, const std::string &v) {
    this->state.setGlobal(handle.getIndex(), DSValue::createStr(v));
  }

  void operator()(const Handle &handle, FILE *fp) {
    int fd = fileno(fp);
    assert(fd > -1);
    this->state.setGlobal(handle.getIndex(), DSValue::create<UnixFdObject>(fd));
  }

  void operator()(const Handle &handle, const DSType &type) {
    auto value = DSValue::createDummy(type);
    if (type.isArrayType()) {
      value = DSValue::create<ArrayObject>(type);
    } else if (type.isMapType()) {
      value = DSValue::create<MapObject>(type);
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

// set positional parameters
static void finalizeScriptArg(DSState *st) {
  assert(st);
  auto &array = typeAs<ArrayObject>(st->getGlobal(BuiltinVarOffset::ARGS));

  // update argument size
  const unsigned int size = array.getValues().size();
  st->setGlobal(BuiltinVarOffset::ARGS_SIZE, DSValue::createInt(size));

  unsigned int limit = 9;
  if (size < limit) {
    limit = size;
  }

  // update positional parameter
  unsigned int index = 0;
  for (; index < limit; index++) {
    unsigned int i = toIndex(BuiltinVarOffset::POS_1) + index;
    st->setGlobal(i, array.getValues()[index]);
  }

  for (; index < 9; index++) {
    unsigned int i = toIndex(BuiltinVarOffset::POS_1) + index;
    st->setGlobal(i, DSValue::createStr());
  }
}

void DSState_setArguments(DSState *st, char *const *args) {
  GUARD_NULL(st);

  // clear previous arguments
  typeAs<ArrayObject>(st->getGlobal(BuiltinVarOffset::ARGS)).refValues().clear();

  if (args) {
    for (unsigned int i = 0; args[i] != nullptr; i++) {
      auto &array = typeAs<ArrayObject>(st->getGlobal(BuiltinVarOffset::ARGS));
      array.append(DSValue::createStr(args[i])); // FIXME: check limit
    }
  }
  finalizeScriptArg(st);
}

int DSState_exitStatus(const DSState *st) {
  GUARD_NULL(st, 0);
  return st->getMaskedExitStatus();
}

void DSState_setExitStatus(DSState *st, int status) {
  GUARD_NULL(st);
  st->setExitStatus(status);
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
    file.reset(strlen(target) == 0 ? fdopen(fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0), "w")
                                   : fopen(target, "we"));
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
  if (e != nullptr) {
    free(e->fileName);
    e->fileName = nullptr;
    free(e->name);
    e->name = nullptr;
  }
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

int DSState_eval(DSState *st, const char *sourceName, const char *data, unsigned int size,
                 DSError *e) {
  GUARD_NULL(st, -1);
  GUARD_NULL(data, -1);

  const auto compileOption = getCompileOption(*st);
  DefaultModuleProvider moduleProvider(st->modLoader, st->typePool, st->rootModScope);
  auto discardPoint = moduleProvider.getCurrentDiscardPoint();
  auto lexer = LexerPtr::create(sourceName == nullptr ? "(stdin)" : sourceName,
                                ByteBuffer(data, data + size), getCWD());
  lexer->setLineNumOffset(st->lineNum);
  st->lineNum = 0;
  auto ctx = moduleProvider.newContext(std::move(lexer), toOption(compileOption), nullptr);
  return evalScript(*st, moduleProvider, std::move(ctx), compileOption, discardPoint, e);
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

int DSState_loadModule(DSState *st, const char *fileName, unsigned int option, DSError *e) {
  GUARD_NULL(st, -1);
  GUARD_NULL(fileName, -1);

  CompileOption compileOption = getCompileOption(*st);
  DefaultModuleProvider moduleProvider(st->modLoader, st->typePool, st->rootModScope);
  auto discardPoint = moduleProvider.getCurrentDiscardPoint();
  CStrPtr scriptDir = hasFlag(option, DS_MOD_FULLPATH) ? nullptr : getCWD();
  auto ret =
      moduleProvider.load(scriptDir.get(), fileName, toOption(compileOption), ModLoadOption{});
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

#define EACH_DS_CONFIG(OP)                                                                         \
  OP(COMPILER)                                                                                     \
  OP(REGEX)                                                                                        \
  OP(VERSION)                                                                                      \
  OP(OSTYPE)                                                                                       \
  OP(MACHTYPE)                                                                                     \
  OP(CONFIG_HOME)                                                                                  \
  OP(DATA_HOME)                                                                                    \
  OP(MODULE_HOME)                                                                                  \
  OP(DATA_DIR)                                                                                     \
  OP(MODULE_DIR)

  const char *key = nullptr;
  switch (config) {
#define GEN_CASE2(E)                                                                               \
  case DS_CONFIG_##E:                                                                              \
    key = SysConfig::E;                                                                            \
    break;
    EACH_DS_CONFIG(GEN_CASE2)
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

const char *DSState_copyright() { return "Copyright (C) 2015-2022 Nagisa Sekiguchi"; }

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
    buf = static_cast<char *>(realloc(buf, sizeof(char) * bufSize));
    if (!buf) {
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
  const char *ret = st->getGlobal(handle.asOk()->getIndex()).asCStr();
  if (*ret) {
    return ret;
  }
  char *path = getExecutablePath();
  if (path) {
    st->setGlobal(handle.asOk()->getIndex(), DSValue::createStr(path));
    free(path);
  }
  return nullptr;
}

int DSState_complete(DSState *st, const char *data, unsigned int size) {
  if (st == nullptr || data == nullptr) {
    errno = EINVAL;
    return -1;
  }

  StringRef ref(data, size);
  auto old = st->getGlobal(BuiltinVarOffset::EXIT_STATUS);
  auto ret = doCodeCompletion(*st, "", ref);
  st->setGlobal(BuiltinVarOffset::EXIT_STATUS, std::move(old));
  return ret;
}

int DSState_getCompletion(const DSState *st, unsigned int index, DSCompletion *comp) {
  if (st == nullptr || comp == nullptr) {
    errno = EINVAL;
    return -1;
  }

  auto &compreply = typeAs<ArrayObject>(st->getGlobal(BuiltinVarOffset::COMPREPLY));
  if (index < compreply.getValues().size()) {
    StringRef ref = compreply.getValues()[index].asStrRef();
    comp->value = ref.data();
    comp->size = ref.size();
    comp->attr = 0; // FIXME:
    return 0;
  }
  errno = EINVAL;
  return -1;
}

#define XSTR(v) #v
#define STR(v) XSTR(v)

static const char *defaultPrompt(int n) {
  switch (n) {
  case 1:
    if (getuid()) {
      return "ydsh-" STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION) "$ ";
    } else {
      return "ydsh-" STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION) "# ";
    }
  case 2:
    return "> ";
  default:
    return "";
  }
}

#undef XSTR
#undef STR

static bool callEditHook(DSState &st, DSLineEditOp op, const DSLineEdit &edit) {
  auto func = getBuiltinGlobal(st, VAR_EIDT_HOOK);
  if (func.isInvalid()) {
    return false;
  }
  StringRef data = "";
  if (edit.data) {
    if (op == DSLineEditOp::DS_EDIT_HIGHLIGHT) {
      data = StringRef(edit.data, edit.index);
    } else {
      data = edit.data;
    }
  }
  auto args =
      makeArgs(DSValue::createInt(op), DSValue::createInt(edit.index), DSValue::createStr(data));
  auto old = st.getGlobal(BuiltinVarOffset::EXIT_STATUS);
  auto ret = VM::callFunction(st, std::move(func), std::move(args));
  st.setGlobal(BuiltinVarOffset::EXIT_STATUS, std::move(old));
  if (st.hasError()) {
    return false;
  }
  st.editOpReply = std::move(ret);
  return true;
}

static int getCharLen(DSLineEditOp op, DSLineEdit &edit);

int DSState_lineEdit(DSState *st, DSLineEditOp op, DSLineEdit *edit) {
  errno = EINVAL;
  if (st == nullptr || edit == nullptr) {
    return -1;
  }

#define EACH_DS_LINE_EDIT_OP(OP)                                                                   \
  OP(DS_EDIT_HIST_SIZE)                                                                            \
  OP(DS_EDIT_HIST_GET)                                                                             \
  OP(DS_EDIT_HIST_SET)                                                                             \
  OP(DS_EDIT_HIST_DEL)                                                                             \
  OP(DS_EDIT_HIST_CLEAR)                                                                           \
  OP(DS_EDIT_HIST_INIT)                                                                            \
  OP(DS_EDIT_HIST_ADD)                                                                             \
  OP(DS_EDIT_HIST_LOAD)                                                                            \
  OP(DS_EDIT_HIST_SAVE)                                                                            \
  OP(DS_EDIT_HIST_SEARCH)                                                                          \
  OP(DS_EDIT_PROMPT)                                                                               \
  OP(DS_EDIT_HIGHLIGHT)                                                                            \
  OP(DS_EDIT_NEXT_CHAR_LEN)                                                                        \
  OP(DS_EDIT_PREV_CHAR_LEN)

  GUARD_ENUM_RANGE(op, EACH_DS_LINE_EDIT_OP, -1);
#undef EACH_DS_LINE_EDIT_OP

  // for char len op
  if (op == DS_EDIT_NEXT_CHAR_LEN || op == DS_EDIT_PREV_CHAR_LEN) {
    return getCharLen(op, *edit);
  }

  if (!callEditHook(*st, op, *edit)) {
    if (op == DS_EDIT_PROMPT) {
      edit->data = defaultPrompt(edit->index);
      edit->out = strlen(edit->data);
      return 0;
    } else if (op == DS_EDIT_HIGHLIGHT) {
      edit->out = edit->index;
      return 0;
    }
    errno = EINVAL;
    return -1;
  }

  auto &type = st->typePool.get(st->editOpReply.getTypeID());
  switch (op) {
  case DS_EDIT_HIST_SIZE:
    if (type.is(TYPE::Int)) {
      auto ret = st->editOpReply.asInt();
      unsigned int size = ret <= 0 ? 0 : static_cast<unsigned int>(ret);
      edit->out = size;
      return 0;
    }
    break;
  case DS_EDIT_HIST_GET:
  case DS_EDIT_HIST_SEARCH:
  case DS_EDIT_PROMPT:
  case DS_EDIT_HIGHLIGHT:
    if (type.is(TYPE::String)) {
      StringRef ref;
      if (op == DS_EDIT_PROMPT) {
        st->prompt = st->editOpReply;
        ref = st->prompt.asStrRef();
      } else {
        ref = st->editOpReply.asStrRef();
      }
      edit->data = ref.data();
      edit->out = ref.size();
      return 0;
    }
    break;
  default:
    break;
  }
  errno = EINVAL;
  return -1;
}

static unsigned int graphemeWidth(const DSLineEdit &edit, const GraphemeScanner::Result &ret) {
  const auto eaw =
      DSLineEdit_isFullWidth(&edit) ? UnicodeUtil::FULL_WIDTH : UnicodeUtil::HALF_WIDTH;
  unsigned int width = 0;
  unsigned int flagSeqCount = 0;
  for (unsigned int i = 0; i < ret.codePointCount; i++) {
    int w = UnicodeUtil::width(ret.codePoints[i], eaw);
    if (ret.breakProperties[i] == GraphemeBoundary::BreakProperty::Regional_Indicator) {
      flagSeqCount++;
    }
    if (w > 0) {
      width += w;
    }
  }
  if (flagSeqCount == 2) {
    return DSLineEdit_getFlagSeqWidth(&edit);
  }
  if (width > 2 && DSLineEdit_isZWJFallback(&edit)) {
    return width;
  }
  return width < 2 ? 1 : 2;
}

static int getCharLen(DSLineEditOp op, DSLineEdit &edit) {
  assert(op == DS_EDIT_NEXT_CHAR_LEN || op == DS_EDIT_PREV_CHAR_LEN);

  if (!edit.data) {
    errno = EINVAL;
    return -1;
  }
  StringRef ref(edit.data, edit.index);
  GraphemeScanner scanner(ref);
  GraphemeScanner::Result ret;
  while (scanner.hasNext()) {
    scanner.next(ret);
    if (op == DS_EDIT_NEXT_CHAR_LEN) {
      break;
    }
  }
  edit.out = ret.ref.size();
  edit.out2 = 0;
  if (ret.codePointCount > 0) {
    edit.out2 = graphemeWidth(edit, ret);
  }
  return 0;
}