/*
 * Copyright (C) 2016-2018 Nagisa Sekiguchi
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

#ifndef YDSH_VM_H
#define YDSH_VM_H

#include <cstdio>

#include <ydsh/ydsh.h>

#include "cmd.h"
#include "core.h"
#include "job.h"
#include "misc/noncopyable.h"
#include "misc/split_random.hpp"
#include "misc/time_util.hpp"
#include "paths.h"
#include "scope.h"
#include "signals.h"
#include "state.h"
#include "sysconfig.h"

namespace ydsh {

#define EACH_RUNTIME_OPTION(OP)                                                                    \
  OP(ASSERT, (1u << 0u), "assert")                                                                 \
  OP(CLOBBER, (1u << 1u), "clobber")                                                               \
  OP(DOTGLOB, (1u << 2u), "dotglob")                                                               \
  OP(ERR_RAISE, (1u << 3u), "errraise")                                                            \
  OP(FASTGLOB, (1u << 4u), "fastglob")                                                             \
  OP(FAIL_SIGPIPE, (1u << 5u), "failsigpipe")                                                      \
  OP(HUP_EXIT, (1u << 6u), "huponexit")                                                            \
  OP(MONITOR, (1u << 7u), "monitor")                                                               \
  OP(NULLGLOB, (1u << 8u), "nullglob")                                                             \
  OP(TRACE_EXIT, (1u << 9u), "traceonexit")                                                        \
  OP(XTRACE, (1u << 10u), "xtrace")

// set/unset via 'shctl' command
enum class RuntimeOption : unsigned short {
#define GEN_ENUM(E, V, N) E = (V),
  EACH_RUNTIME_OPTION(GEN_ENUM)
#undef GEN_ENUM
};

enum class VMEvent : unsigned int {
  SIGNAL = 1u << 0u,
  MASK = 1u << 1u,
};

enum class EvalOP : unsigned int {
  PROPAGATE = 1u << 0u, // propagate uncaught exception to caller (except for subshell).
};

enum class EvalRet : unsigned int {
  SUCCESS,       // normal termination
  HAS_ERROR,     // still has uncaught error
  HANDLED_ERROR, // already handled uncaught error
};

template <>
struct allow_enum_bitop<RuntimeOption> : std::true_type {};

template <>
struct allow_enum_bitop<VMEvent> : std::true_type {};

template <>
struct allow_enum_bitop<EvalOP> : std::true_type {};

class VM;

struct PipeSet;

} // namespace ydsh

using namespace ydsh;

struct DSState {
public:
  const SysConfig sysConfig;

  ModuleLoader modLoader;

  TypePool typePool;

  std::vector<NameScopePtr> tempModScope; // for completion

  NameScopePtr rootModScope;

  const ObjPtr<UnixFdObject> emptyFDObj;

  const timestamp initTime; // for builtin printf command

  const bool support_strftime_plus; // if support strftime '%+' specifier

  bool isInteractive{false};

  RuntimeOption runtimeOption{RuntimeOption::HUP_EXIT | RuntimeOption::ASSERT |
                              RuntimeOption::CLOBBER};

  DSExecMode execMode{DS_EXEC_MODE_NORMAL};

  struct DumpTarget {
    FilePtr files[3];
  } dumpTarget;

  /**
   * cache searched result.
   */
  mutable FilePathCache pathCache;

  unsigned int lineNum{1};

  /**
   * if 0, current shell is not sub-shell.
   * otherwise current shell is sub-shell.
   */
  unsigned int subshellLevel{0};

  unsigned int termHookIndex{0};

  bool canHandleSignal{true};

  std::string logicalWorkingDir;

  SignalVector sigVector;

  JobTable jobTable;

  JobNotifyCallback notifyCallback;

private:
  friend class ydsh::VM;

  VMHook *hook{nullptr};

  std::vector<DSValue> globals{64};

  VMState stack;

  timestamp baseTime;

  L64X128MixRNG rng;

public:
  static SigSet pendingSigSet;

  static int popPendingSignal() { return DSState::pendingSigSet.popPendingSig(); }

  /**
   *
   * @param sigNum
   * if 0, clear all pending signals
   */
  static void clearPendingSignal(int sigNum = 0) {
    if (sigNum > 0) {
      DSState::pendingSigSet.del(sigNum);
    } else {
      DSState::pendingSigSet.clear();
    }
  }

  static bool isInterrupted() { return DSState::pendingSigSet.has(SIGINT); }

  static bool hasSignals() { return !pendingSigSet.empty(); }

  NON_COPYABLE(DSState);

  DSState();

  ~DSState() = default;

  bool hasError() const { return this->stack.hasError(); }

  void throwObject(ObjPtr<ErrorObject> &&except) { this->stack.setErrorObj(std::move(except)); }

  // variable manipulation
  void setGlobal(unsigned int index, const DSValue &obj) { this->setGlobal(index, DSValue(obj)); }

  void setGlobal(BuiltinVarOffset offset, DSValue &&obj) {
    this->setGlobal(toIndex(offset), std::move(obj));
  }

  void setGlobal(unsigned int index, DSValue &&obj) { this->globals[index] = std::move(obj); }

  const DSValue &getGlobal(unsigned int index) const { return this->globals[index]; }

  const DSValue &getGlobal(BuiltinVarOffset offset) const {
    return this->getGlobal(toIndex(offset));
  }

  void setLocal(unsigned char index, DSValue &&obj) { this->stack.setLocal(index, std::move(obj)); }

  const DSValue &getLocal(unsigned char index) const { return this->stack.getLocal(index); }

  DSValue moveLocal(unsigned char index) { return this->stack.moveLocal(index); }

  /**
   * get exit status ($? & 0xFF)
   * @return
   */
  int getMaskedExitStatus() const {
    return maskExitStatus(this->getGlobal(BuiltinVarOffset::EXIT_STATUS).asInt());
  }

  void setExitStatus(int64_t status) {
    this->setGlobal(BuiltinVarOffset::EXIT_STATUS, DSValue::createInt(status));
  }

  void updatePipeStatus(unsigned int size, const Proc *procs, bool mergeExitStatus);

  bool isRootShell() const { return this->subshellLevel == 0; }

  bool isJobControl() const {
    return this->isRootShell() && hasFlag(this->runtimeOption, RuntimeOption::MONITOR);
  }

  /**
   *
   * @return
   * if success, return 0.
   * if not DSState::isForeground is false, return 1.
   * if error, return -1 and set errno
   */
  int tryToBeForeground() const {
    if (this->isJobControl()) {
      return beForeground(0);
    }
    return 1;
  }

  void setVMHook(VMHook *h) { this->hook = h; }

  VMHook *getVMHook() { return this->hook; }

  const VMState &getCallStack() const { return this->stack; }

  VMState &getCallStack() { return this->stack; }

  auto getWorkingDir(bool useLogical = true) const {
    return ydsh::getWorkingDir(this->logicalWorkingDir, useLogical);
  }

  L64X128MixRNG &getRng() { return this->rng; }
};

namespace ydsh {

class ResolvedCmd {
public:
  enum CmdKind : unsigned char {
    USER_DEFINED, // user-defined command
    MODULE,       // module subcommand
    BUILTIN_S,    // builtin command written by vm api (ex. command, eval, exec)
    BUILTIN,      // builtin command
    CMD_OBJ,      // command object
    EXTERNAL,     // external command
    FALLBACK,     // for CMD_CALLBACK
    INVALID,      // invalid command name (contains null character)
    ILLEGAL_UDC,  // uninitialized user-defined command
  };

private:
  CmdKind kind_;
  bool nullChar{false};
  ModId belongModId_; // if not belong to module (external, builtin, ect), indicate 0
  union {
    const DSCode *udc_;
    const ModType *modType_;
    builtin_command_t builtinCmd_;
    DSObject *cmdObj_;
    const char *filePath_;
  };

public:
  static ResolvedCmd fromUdc(const FuncObject &func, bool nullChar) {
    ResolvedCmd cmd; // NOLINT
    cmd.kind_ = CmdKind::USER_DEFINED;
    cmd.nullChar = nullChar;
    cmd.belongModId_ = func.getCode().getBelongedModId();
    cmd.udc_ = &func.getCode();
    return cmd;
  }

  static ResolvedCmd fromMod(const ModType &modType, ModId modId, bool nullChar) {
    ResolvedCmd cmd; // NOLINT
    cmd.kind_ = CmdKind::MODULE;
    cmd.nullChar = nullChar;
    cmd.belongModId_ = modId;
    cmd.modType_ = &modType;
    return cmd;
  }

  static ResolvedCmd fromBuiltin(builtin_command_t bcmd) {
    ResolvedCmd cmd; // NOLINT
    cmd.kind_ = CmdKind::BUILTIN;
    cmd.belongModId_ = BUILTIN_MOD_ID;
    cmd.builtinCmd_ = bcmd;
    return cmd;
  }

  static ResolvedCmd fromBuiltin(const NativeCode &code) {
    ResolvedCmd cmd; // NOLINT
    cmd.kind_ = CmdKind::BUILTIN_S;
    cmd.belongModId_ = BUILTIN_MOD_ID;
    cmd.udc_ = &code;
    return cmd;
  }

  static ResolvedCmd fromCmdObj(DSObject *obj) {
    ResolvedCmd cmd; // NOLINT
    cmd.kind_ = CmdKind::CMD_OBJ;
    cmd.belongModId_ = BUILTIN_MOD_ID;
    cmd.cmdObj_ = obj;
    return cmd;
  }

  static ResolvedCmd fromExternal(const char *path) {
    ResolvedCmd cmd; // NOLINT
    cmd.kind_ = CmdKind::EXTERNAL;
    cmd.belongModId_ = BUILTIN_MOD_ID;
    cmd.filePath_ = path;
    return cmd;
  }

  static ResolvedCmd fallback() {
    ResolvedCmd cmd; // NOLINT
    cmd.kind_ = CmdKind::FALLBACK;
    cmd.belongModId_ = BUILTIN_MOD_ID;
    cmd.filePath_ = nullptr;
    return cmd;
  }

  static ResolvedCmd invalid() {
    ResolvedCmd cmd; // NOLINT
    cmd.kind_ = CmdKind::INVALID;
    cmd.belongModId_ = BUILTIN_MOD_ID;
    cmd.filePath_ = nullptr;
    return cmd;
  }

  static ResolvedCmd illegalUdc() {
    ResolvedCmd cmd; // NOLINT
    cmd.kind_ = CmdKind::ILLEGAL_UDC;
    cmd.belongModId_ = BUILTIN_MOD_ID;
    cmd.udc_ = nullptr;
    return cmd;
  }

  CmdKind kind() const { return this->kind_; }

  bool hasNullChar() const { return this->nullChar; }

  ModId belongModId() const { return this->belongModId_; }

  const DSCode &udc() const { return *this->udc_; }

  const ModType &modType() const { return *this->modType_; }

  builtin_command_t builtinCmd() const { return this->builtinCmd_; }

  DSObject *cmdObj() const { return this->cmdObj_; }

  const char *filePath() const { return this->filePath_; }
};

class CmdResolver {
public:
  enum ResolveOp {
    FROM_UDC = 1u << 0u,
    FROM_BUILTIN = 1u << 1u,
    FROM_EXTERNAL = 1u << 2u,
    FROM_DYNA_UDC = 1u << 3u,
    FROM_FALLBACK = (1u << 4u) | FROM_EXTERNAL,
    FROM_FQN_UDC = (1u << 5u) | FROM_UDC,

    NO_FALLBACK = FROM_UDC | FROM_BUILTIN | FROM_DYNA_UDC | FROM_EXTERNAL,
    NO_UDC = FROM_BUILTIN | FROM_EXTERNAL | FROM_FALLBACK,
    NO_STATIC_UDC = NO_UDC | FROM_DYNA_UDC,
    FROM_DEFAULT = NO_STATIC_UDC | FROM_UDC,
    FROM_DEFAULT_WITH_FQN = FROM_DEFAULT | FROM_FQN_UDC,
  };

private:
  ResolveOp resolveOp;
  FilePathCache::SearchOp searchOp;

public:
  CmdResolver(ResolveOp mask, FilePathCache::SearchOp op) : resolveOp(mask), searchOp(op) {}

  CmdResolver() : CmdResolver(ResolveOp::FROM_DEFAULT, FilePathCache::NON) {}

  ~CmdResolver() = default;

  /**
   *
   * @param state
   * @param cmdName
   * must be String
   * @param modType
   * if specified USE_FQN, always ignore
   * @return
   */
  ResolvedCmd operator()(const DSState &state, const DSValue &cmdName,
                         const ModType *modType = nullptr) const;
};

template <>
struct allow_enum_bitop<CmdResolver::ResolveOp> : std::true_type {};

enum class CmdCallAttr : unsigned int {
  SET_VAR = 1u << 0u,
  NEED_FORK = 1u << 1u, // for external command
  RAISE = 1u << 2u,
  CLOSURE = 1u << 3u, // for command object call
};

template <>
struct allow_enum_bitop<CmdCallAttr> : std::true_type {};

// for command argument construction
class CmdArgsBuilder {
private:
  DSState &state;
  ObjPtr<ArrayObject> argv;
  DSValue redir; // may be null

public:
  explicit CmdArgsBuilder(DSState &state, ObjPtr<ArrayObject> argv, DSValue &&redir)
      : state(state), argv(std::move(argv)), redir(std::move(redir)) {}

  /**
   *
   * @param state
   * @param arg
   * @return
   * if has error, return false
   */
  bool add(DSValue &&arg);

  DSValue takeRedir() && { return std::move(this->redir); }
};

class RecursionGuard {
private:
  DSState &state;

public:
  explicit RecursionGuard(DSState &st) : state(st) { this->state.getCallStack().incRecDepth(); }

  ~RecursionGuard() { this->state.getCallStack().decRecDepth(); }

  bool checkLimit();
};

using native_func_t = DSValue (*)(DSState &);

class VM {
private:
  static void pushExitStatus(DSState &state, int64_t status) {
    state.setExitStatus(status);
    state.stack.push(exitStatusToBool(status));
  }

  static bool windStackFrame(DSState &state, unsigned int stackTopOffset, unsigned int paramSize,
                             const DSCode &code) {
    auto ret = state.stack.wind(stackTopOffset, paramSize, code);
    if (unlikely(!ret)) {
      raiseError(state, TYPE::StackOverflowError, "local stack size reaches limit");
    }
    return ret;
  }

  // runtime api
  static bool instanceOf(const TypePool &pool, const DSValue &value, const DSType &targetType) {
    if (value.isInvalid()) {
      return targetType.isOptionType();
    }
    return targetType.isSameOrBaseTypeOf(pool.get(value.getTypeID()));
  }

  static bool checkCast(DSState &state, const DSType &targetType);

  static const char *loadEnv(DSState &state, bool hasDefault);

  static bool storeEnv(DSState &state);

  static void pushNewObject(DSState &state, const DSType &type);

  /**
   * stack state in function apply    stack grow ===>
   *
   * +-----------+---------+--------+   +--------+
   * | stack top | funcObj | param1 | ~ | paramN |
   * +-----------+---------+--------+   +--------+
   *                       | offset |   |        |
   */
  static bool prepareFuncCall(DSState &state, unsigned int paramSize) {
    const DSCode *code;
    auto *obj = state.stack.peekByOffset(paramSize).get();
    if (isa<FuncObject>(obj)) {
      code = &cast<FuncObject>(obj)->getCode();
    } else {
      assert(isa<ClosureObject>(obj));
      code = &cast<ClosureObject>(obj)->getFuncObj().getCode();
    }
    return windStackFrame(state, paramSize + 1, paramSize, *code);
  }

  /**
   * stack state in method call    stack grow ===>
   *
   * +-----------+------------------+   +--------+
   * | stack top | param1(receiver) | ~ | paramN |
   * +-----------+------------------+   +--------+
   *             | offset           |   |        |
   *
   *
   * @param state
   * @param index
   * @param actualParamSize
   * actual param size (also include receiver)
   * @return
   */
  static bool prepareMethodCall(DSState &state, unsigned short index,
                                unsigned short actualParamSize) {
    auto value = state.getGlobal(index);
    if (unlikely(!value)) {
      raiseError(state, TYPE::IllegalAccessError,
                 "attempt to call uninitialized method or constructor");
      return false;
    }
    auto &func = typeAs<FuncObject>(value);
    return windStackFrame(state, actualParamSize, actualParamSize, func.getCode());
  }

  /**
   * stack state in function apply    stack grow ===>
   *
   * +-----------+---------------+--------------+
   * | stack top | param1(redir) | param2(argv) |
   * +-----------+---------------+--------------+
   *             |     offset    |
   */
  static bool prepareUserDefinedCommandCall(DSState &state, const DSCode &code, DSValue &&argvObj,
                                            DSValue &&redirConfig, CmdCallAttr attr);

  static bool attachAsyncJob(DSState &state, DSValue &&desc, unsigned int procSize,
                             const Proc *procs, ForkKind forkKind, PipeSet &pipeSet, DSValue &ret);

  static bool forkAndEval(DSState &state, DSValue &&desc);

  static bool forkAndExec(DSState &state, const char *filePath, char *const *argv,
                          DSValue &&redirConfig);

  static bool prepareSubCommand(DSState &state, const ModType &modType, DSValue &&argvObj,
                                DSValue &&redirConfig);

  static bool callCommand(DSState &state, CmdResolver resolver, DSValue &&argvObj,
                          DSValue &&redirConfig, CmdCallAttr attr = {});

  static bool callCommand(DSState &state, const ResolvedCmd &cmd, DSValue &&argvObj,
                          DSValue &&redirConfig, CmdCallAttr attr);

  static bool builtinCommand(DSState &state, DSValue &&argvObj, DSValue &&redir, CmdCallAttr attr);

  static void builtinExec(DSState &state, DSValue &&array, DSValue &&redir);

  static bool returnFromUserDefinedCommand(DSState &state, int64_t status);

  /**
   *
   * @param lastPipe
   * if true, evaluate last pipe in parent shell
   * @return
   * if has error, return false.
   */
  static bool callPipeline(DSState &state, DSValue &&desc, bool lastPipe, ForkKind forkKind);

  static bool addGlobbingPath(DSState &state, ArrayObject &arv, const DSValue *begin,
                              const DSValue *end, bool tilde);

  static bool applyBraceExpansion(DSState &state, ArrayObject &argv, const DSValue *begin,
                                  const DSValue *end, ExpandOp expandOp);

  static bool addExpandingPath(DSState &state, unsigned int size, ExpandOp expandOp);

  static bool kickSignalHandler(DSState &state, int sigNum, DSValue &&func);

  static void kickVMHook(DSState &state);

  /**
   *
   * @return
   * if has exception, return false.
   */
  static bool mainLoop(DSState &state);

  static void rethrowFromFinally(DSState &state);

  /**
   * if found exception handler, return true.
   * otherwise return false.
   */
  static bool handleException(DSState &state);

  /**
   * actual entry point of interpreter.
   * @param op
   * @param dsError
   * if not null, set error info
   * @param value
   * if has return value, set to this
   * @return
   * if has error or not value, return null
   * otherwise, return value
   */
  static EvalRet startEval(DSState &state, EvalOP op, DSError *dsError, DSValue &value);

  static unsigned int prepareArguments(VMState &state, DSValue &&recv,
                                       std::pair<unsigned int, std::array<DSValue, 3>> &&args);

public:
  // entry point
  /**
   * entry point of toplevel code evaluation.
   * @param func
   * must be toplevel compiled function.
   * @param dsError
   * if not null, set error information
   * @return
   * if had uncaught exception, return false.
   */
  static bool callToplevel(DSState &state, const ObjPtr<FuncObject> &func, DSError *dsError);

  /**
   * execute command.
   * @param argv
   * DSValue must be String_Object
   * @param propagate
   * if true, not handle uncaught exception
   * @return
   * if exit status is 0, return true.
   * otherwise, return false
   */
  static DSValue execCommand(DSState &state, std::vector<DSValue> &&argv, bool propagate);

  /**
   *
   * @param funcObj
   * @param args
   * @return
   * return value of method (if no return value, return null).
   */
  static DSValue callFunction(DSState &state, DSValue &&funcObj, CallArgs &&args);

  /**
   * print uncaught exception information. (not clear thrown object)
   * @param state
   * @param dsError
   * if not null, set error information
   * @return
   * if except is null, return always DS_ERROR_KIND_SUCCESS and not set error info
   */
  static DSErrorKind handleUncaughtException(DSState &state, DSError *dsError);

  /**
   * call user-defined termination handler specified by TERM_HOOK.
   * after call TERM_HOOK, clear thrown object and TERM_HOOK
   * @param state
   * @return
   * if call term hook, return true.
   * if not call (not found hook), return false
   */
  static bool callTermHook(DSState &state);
};

} // namespace ydsh

#endif // YDSH_VM_H
