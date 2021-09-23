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

#include <ydsh/ydsh.h>

#include "codegen.h"
#include "complete.h"
#include "error_report.h"
#include "frontend.h"
#include "logger.h"
#include "misc/files.h"
#include "vm.h"

using namespace ydsh;

static ErrorReporter newReporter(DSError *e) {
#ifdef FUZZING_BUILD_MODE
  bool ignore = getenv("YDSH_SUPPRESS_COMPILE_ERROR") != nullptr;
  return ErrorReporter(e, ignore ? fopen("/dev/null", "w") : stderr, ignore);
#else
  return ErrorReporter(e, stderr, false);
#endif
}

static FrontEndOption toOption(DSExecMode mode, CompileOption option) {
  FrontEndOption op{};
  if (mode == DS_EXEC_MODE_PARSE_ONLY) {
    setFlag(op, FrontEndOption::PARSE_ONLY);
  }
  if (hasFlag(option, CompileOption::INTERACTIVE)) {
    setFlag(op, FrontEndOption::TOPLEVEL);
  }
  return op;
}

class Compiler {
private:
  DefaultModuleProvider provider;
  FrontEnd frontEnd;
  ErrorReporter reporter;
  NodeDumper uastDumper;
  NodeDumper astDumper;
  ByteCodeGenerator codegen;
  DSExecMode mode;

public:
  Compiler(ModuleLoader &modLoader, TypePool &pool, const IntrusivePtr<NameScope> &root,
           Lexer &&lexer, const DumpTarget &dumpTarget, DSExecMode execMode,
           CompileOption compileOption, DSError *dsError)
      : provider(modLoader, pool, root),
        frontEnd(this->provider, std::move(lexer), toOption(execMode, compileOption)),
        reporter(newReporter(dsError)), uastDumper(dumpTarget.files[DS_DUMP_KIND_UAST].get()),
        astDumper(dumpTarget.files[DS_DUMP_KIND_AST].get()),
        codegen(pool, hasFlag(compileOption, CompileOption::ASSERT)), mode(execMode) {
    if (dsError != nullptr) {
      *dsError = {.kind = DS_ERROR_KIND_SUCCESS,
                  .fileName = nullptr,
                  .lineNum = 0,
                  .chars = 0,
                  .name = nullptr};
    }
    this->frontEnd.setErrorListener(this->reporter);
    if (this->uastDumper) {
      this->frontEnd.setUASTDumper(this->uastDumper);
    }
    if (this->astDumper) {
      this->frontEnd.setASTDumper(this->astDumper);
    }
  }

  bool frontEndOnly() const {
    return this->mode == DS_EXEC_MODE_PARSE_ONLY || this->mode == DS_EXEC_MODE_CHECK_ONLY;
  }

  unsigned int lineNum() const { return this->frontEnd.getRootLineNum(); }

  void discard(const DiscardPoint &point) { this->provider.discard(point); }

  const std::string &getSourcePath() const {
    return this->frontEnd.getCurrentLexer().getSourceName();
  }

  int operator()(CompiledCode &code);
};

int Compiler::operator()(CompiledCode &code) {
  this->frontEnd.setupASTDump();
  if (!this->frontEndOnly()) {
    this->codegen.initialize(this->frontEnd.getCurrentLexer());
  }
  while (this->frontEnd) {
    auto ret = this->frontEnd();
    if (!ret) {
      return 1;
    }

    if (this->frontEndOnly()) {
      continue;
    }

    switch (ret.kind) {
    case FrontEndResult::ENTER_MODULE:
      this->codegen.enterModule(this->frontEnd.getCurrentLexer());
      break;
    case FrontEndResult::EXIT_MODULE:
      if (!this->codegen.exitModule(cast<SourceNode>(*ret.node))) {
        goto END;
      }
      break;
    case FrontEndResult::IN_MODULE:
      if (!this->codegen.generate(*ret.node)) {
        goto END;
      }
      break;
    default:
      break;
    }
  }
  this->frontEnd.teardownASTDump();
  if (!this->frontEndOnly()) {
    code = this->codegen.finalize(this->frontEnd.getMaxLocalVarIndex());
  }

END:
  if (this->codegen.hasError()) {
    auto &e = this->codegen.getError();
    this->reporter.handleCodeGenError(this->frontEnd.getContext(), e);
    return 1;
  }
  return 0;
}

static int compile(DSState &state, const IntrusivePtr<NameScope> &modScope, Lexer &&lexer,
                   const DiscardPoint &discardPoint, DSError *dsError, CompiledCode &code) {
  Compiler compiler(state.modLoader, state.typePool, modScope, std::move(lexer), state.dumpTarget,
                    state.execMode, state.compileOption, dsError);
  int ret = compiler(code);
  if (ret == 0) {
    if (!modScope->inBuiltinModule() && !modScope->inRootModule()) {
      state.modLoader.createModType(state.typePool, *modScope, compiler.getSourcePath());
    }
  } else {
    compiler.discard(discardPoint);
  }
  state.lineNum = compiler.lineNum();
  return ret;
}

static int evalScript(DSState &state, const IntrusivePtr<NameScope> &scope, Lexer &&lexer,
                      const DiscardPoint &point, DSError *dsError) {
  CompiledCode code;
  int ret =
      compile(state, scope ? scope : state.rootModScope, std::move(lexer), point, dsError, code);
  if (!code) {
    return ret;
  }

  if (state.dumpTarget.files[DS_DUMP_KIND_CODE]) {
    auto *fp = state.dumpTarget.files[DS_DUMP_KIND_CODE].get();
    fprintf(fp, "### dump compiled code ###\n");
    ByteCodeDumper(fp, state.typePool, state.rootModScope->getMaxGlobalVarIndex())(code);
  }

  if (state.execMode == DS_EXEC_MODE_COMPILE_ONLY) {
    return 0;
  }
  callToplevel(state, code, dsError);
  return state.getMaskedExitStatus();
}

static void loadEmbeddedScript(DSState *state) {
  state->rootModScope = state->builtinModScope; // eval script in builtin module

  const char *embed_script = getEmbeddedScript();
  int ret = DSState_eval(state, "(builtin)", embed_script, strlen(embed_script), nullptr);
  (void)ret;
  assert(ret == 0);

  // rest some state
  auto &modType =
      state->modLoader.createModType(state->typePool, *state->builtinModScope, "(builtin)");
  state->rootModScope = state->modLoader.createGlobalScope(state->typePool, "(root)", &modType);
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
  bindBuiltinVariables(ctx, ctx->typePool, *ctx->builtinModScope);
  loadEmbeddedScript(ctx);

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
      array.append(DSValue::createStr(args[i]));
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
  if (hasFlag(st->compileOption, CompileOption::ASSERT)) {
    setFlag(option, DS_OPTION_ASSERT);
  }
  if (hasFlag(st->compileOption, CompileOption::INTERACTIVE)) {
    setFlag(option, DS_OPTION_INTERACTIVE);
  }

  // get runtime option
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
  if (hasFlag(optionSet, DS_OPTION_ASSERT)) {
    setFlag(st->compileOption, CompileOption::ASSERT);
  }
  if (hasFlag(optionSet, DS_OPTION_INTERACTIVE)) {
    setFlag(st->compileOption, CompileOption::INTERACTIVE);
  }

  // set runtime option
  if (hasFlag(optionSet, DS_OPTION_TRACE_EXIT)) {
    setFlag(st->runtimeOption, RuntimeOption::TRACE_EXIT);
  }
  if (hasFlag(optionSet, DS_OPTION_JOB_CONTROL)) {
    setFlag(st->runtimeOption, RuntimeOption::MONITOR);
    setJobControlSignalSetting(*st, true);
  }
}

void DSState_unsetOption(DSState *st, unsigned int optionSet) {
  GUARD_NULL(st);

  // unset compile option
  if (hasFlag(optionSet, DS_OPTION_ASSERT)) {
    unsetFlag(st->compileOption, CompileOption::ASSERT);
  }
  if (hasFlag(optionSet, DS_OPTION_INTERACTIVE)) {
    unsetFlag(st->compileOption, CompileOption::INTERACTIVE);
  }

  // unset runtime option
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

int DSState_eval(DSState *st, const char *sourceName, const char *data, unsigned int size,
                 DSError *e) {
  GUARD_NULL(st, -1);
  GUARD_NULL(data, -1);

  Lexer lexer(sourceName == nullptr ? "(stdin)" : sourceName, ByteBuffer(data, data + size),
              getCWD());
  lexer.setLineNumOffset(st->lineNum);

  DiscardPoint discardPoint{
      .mod = st->modLoader.getDiscardPoint(),
      .scope = st->rootModScope->getDiscardPoint(),
      .type = st->typePool.getDiscardPoint(),
  };
  return evalScript(*st, nullptr, std::move(lexer), discardPoint, e);
}

static void reportFileError(const char *sourceName, bool isIO, int errNum, DSError *e) {
  fprintf(stderr, "ydsh: %s: %s, by `%s'\n", isIO ? "cannot read file" : "cannot open file",
          sourceName, strerror(errNum));
  if (e) {
    *e = {.kind = DS_ERROR_KIND_FILE_ERROR,
          .fileName = strdup(sourceName),
          .lineNum = 0,
          .chars = 0,
          .name = strdup(strerror(errNum))};
  }
  errno = errNum;
}

int DSState_loadAndEval(DSState *st, const char *sourceName, DSError *e) {
  return DSState_loadModule(st, sourceName, DS_MOD_FULLPATH | DS_MOD_SEPARATE_CTX, e);
}

int DSState_loadModule(DSState *st, const char *fileName, unsigned int option, DSError *e) {
  GUARD_NULL(st, -1);
  GUARD_NULL(fileName, -1);

  DiscardPoint discardPoint{
      .mod = st->modLoader.getDiscardPoint(),
      .scope = st->rootModScope->getDiscardPoint(),
      .type = st->typePool.getDiscardPoint(),
  };

  FilePtr filePtr;
  CStrPtr scriptDir = hasFlag(option, DS_MOD_FULLPATH) ? nullptr : getCWD();
  auto ret = st->modLoader.load(scriptDir.get(), fileName, filePtr, ModLoadOption{});
  if (is<ModLoadingError>(ret)) {
    int errNum = get<ModLoadingError>(ret).getErrNo();
    if (get<ModLoadingError>(ret).isCircularLoad()) {
      errNum = ETXTBSY;
    }
    if (errNum == ENOENT && hasFlag(option, DS_MOD_IGNORE_ENOENT)) {
      return 0;
    }
    reportFileError(fileName, false, errNum, e);
    return 1;
  } else if (is<unsigned int>(ret)) {
    return 0; // do nothing.
  }
  fileName = get<const char *>(ret);
  char *real = strdup(fileName);
  assert(*real == '/');
  const char *ptr = strrchr(real, '/');
  real[ptr == real ? 1 : (ptr - real)] = '\0';
  scriptDir.reset(real);

  // read data
  assert(filePtr);
  ByteBuffer buf;
  if (!readAll(filePtr, buf)) {
    reportFileError(fileName, true, errno, e);
    return 1;
  }
  filePtr.reset(nullptr);

  auto &modType = st->typePool.getBuiltinModType();
  auto scope = hasFlag(option, DS_MOD_SEPARATE_CTX)
                   ? st->modLoader.createGlobalScopeFromFullpath(st->typePool, fileName, modType)
                   : nullptr;
  return evalScript(*st, scope, Lexer(fileName, std::move(buf), std::move(scriptDir)), discardPoint,
                    e);
}

int DSState_exec(DSState *st, char *const *argv) {
  GUARD_NULL(st, -1);
  GUARD_TRUE(st->execMode != DS_EXEC_MODE_NORMAL, 0);
  GUARD_NULL(argv, -1);

  std::vector<DSValue> values;
  for (; *argv != nullptr; argv++) {
    values.push_back(DSValue::createStr(*argv));
  }
  execCommand(*st, std::move(values), false);
  return st->getMaskedExitStatus();
}

const char *DSState_version(DSVersion *version) {
  if (version != nullptr) {
    version->major = X_INFO_MAJOR_VERSION;
    version->minor = X_INFO_MINOR_VERSION;
    version->patch = X_INFO_PATCH_VERSION;
  }
  return "ydsh, version " X_INFO_VERSION ", build by " X_INFO_CPP " " X_INFO_CPP_V;
}

const char *DSState_copyright() { return "Copyright (C) 2015-2021 Nagisa Sekiguchi"; }

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

  auto *handle = st->rootModScope->lookup(VAR_YDSH_BIN);
  assert(handle);
  const char *ret = st->getGlobal(handle->getIndex()).asCStr();
  if (*ret) {
    return ret;
  }
  char *path = getExecutablePath();
  if (path) {
    st->setGlobal(handle->getIndex(), DSValue::createStr(path));
    free(path);
  }
  return nullptr;
}

unsigned int DSState_complete(DSState *st, DSCompletionOp op, unsigned int index,
                              const char **value) {
  GUARD_NULL(st, 0);

  auto &compreply = typeAs<ArrayObject>(st->getGlobal(BuiltinVarOffset::COMPREPLY));

  switch (op) {
  case DS_COMP_INVOKE: {
    StringRef ref;
    if (value != nullptr && *value != nullptr) {
      ref = StringRef(*value, index);
    }
    auto old = st->getGlobal(BuiltinVarOffset::EXIT_STATUS);
    unsigned int size = doCodeCompletion(*st, nullptr, ref);
    st->setGlobal(BuiltinVarOffset::EXIT_STATUS, std::move(old));
    return size;
  }
  case DS_COMP_GET:
    if (value == nullptr) {
      break;
    }
    *value = nullptr;
    if (index < compreply.getValues().size()) {
      *value = compreply.getValues()[index].asCStr();
    }
    break;
  case DS_COMP_SIZE:
    return compreply.getValues().size();
  case DS_COMP_CLEAR:
    compreply.refValues().clear();
    break;
  }
  return 0;
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

unsigned int DSState_lineEdit(DSState *st, DSLineEditOp op, int index, const char **buf) {
  GUARD_NULL(st, 0);

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
  OP(DS_EDIT_PROMPT)

  GUARD_ENUM_RANGE(op, EACH_DS_LINE_EDIT_OP, 0);
#undef EACH_DS_LINE_EDIT_OP

  auto func = getBuiltinGlobal(*st, VAR_EIDT_HOOK);
  if (func.isInvalid()) {
    if (op == DS_EDIT_PROMPT && buf) {
      *buf = defaultPrompt(index);
    }
    return 0;
  }

  const char *value = nullptr;
  if (buf) {
    value = *buf;
    *buf = nullptr;
  }
  auto args = makeArgs(DSValue::createInt(op), DSValue::createInt(index),
                       DSValue::createStr((value && *value) ? value : ""));
  auto old = st->getGlobal(BuiltinVarOffset::EXIT_STATUS);
  st->editOpReply = callFunction(*st, std::move(func), std::move(args));
  st->setGlobal(BuiltinVarOffset::EXIT_STATUS, std::move(old));
  if (st->hasError()) {
    return 0;
  }

  auto &type = st->typePool.get(st->editOpReply.getTypeID());
  switch (op) {
  case DS_EDIT_HIST_SIZE:
    if (type.is(TYPE::Int)) {
      auto ret = st->editOpReply.asInt();
      return ret <= 0 ? 0 : static_cast<unsigned int>(ret);
    }
    return 0;
  case DS_EDIT_HIST_GET:
  case DS_EDIT_HIST_SEARCH:
  case DS_EDIT_PROMPT:
    if (!type.is(TYPE::String) || buf == nullptr) {
      return 0;
    }
    if (op == DS_EDIT_PROMPT) {
      st->prompt = st->editOpReply;
      *buf = st->prompt.asCStr();
    } else {
      *buf = st->editOpReply.asCStr();
    }
    break;
  default:
    break;
  }
  return 1;
}
