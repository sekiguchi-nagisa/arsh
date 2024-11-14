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

#ifndef ARSH_VM_H
#define ARSH_VM_H

#include <cstdio>

#include <arsh/arsh.h>

#include "cmd.h"
#include "cmd_desc.h"
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

namespace arsh {

enum class EvalOP : unsigned char {
  PROPAGATE = 1u << 0u, // propagate uncaught exception to caller (except for subshell).
};

template <>
struct allow_enum_bitop<RuntimeOption> : std::true_type {};

template <>
struct allow_enum_bitop<EvalOP> : std::true_type {};

class VM;

struct PipeSet;

} // namespace arsh

using namespace arsh;

struct ARState {
public:
  friend class arsh::VM;

  const SysConfig sysConfig;

  ModuleLoader modLoader;

  TypePool typePool;

  std::vector<NameScopePtr> tempModScope; // for completion

  NameScopePtr rootModScope;

  const ObjPtr<UnixFdObject> emptyFDObj;

  const timestamp initTime; // for builtin printf command

private:
  short readlineCallCount{0};

  RuntimeOption runtimeOption{RuntimeOption::ASSERT | RuntimeOption::CLOBBER |
                              RuntimeOption::FAIL_GLOB | RuntimeOption::FAIL_TILDE |
                              RuntimeOption::GLOBSTAR | RuntimeOption::HUP_EXIT};

public:
  ARExecMode execMode{AR_EXEC_MODE_NORMAL};

  struct DumpTarget {
    FilePtr files[3];
  } dumpTarget;

  /**
   * cache searched result.
   */
  mutable FilePathCache pathCache;

  unsigned int lineNum{1};

  unsigned int termHookIndex{0};

  const bool support_strftime_plus; // if support strftime '%+' specifier

  bool isInteractive{false};

  bool canHandleSignal{true};

  std::string logicalWorkingDir;

  SignalVector sigVector;

  JobTable jobTable;

  JobNotifyCallback notifyCallback;

private:
  VMHook *hook{nullptr};

  std::vector<Value> globals{64};

  VMState stack;

  timestamp baseTime;

  L64X128MixRNG rng;

public:
  static AtomicSigSet pendingSigSet;

  static int popPendingSignal() { return pendingSigSet.popPendingSig(); }

  /**
   *
   * @param sigNum
   * if 0, clear all pending signals
   */
  static void clearPendingSignal(int sigNum = 0) {
    if (sigNum > 0) {
      pendingSigSet.del(sigNum);
    } else {
      pendingSigSet.clear();
    }
  }

  static bool hasSignal(int sigNum) { return pendingSigSet.has(sigNum); }

  static bool isInterrupted() { return hasSignal(SIGINT); }

  static bool hasSignals() { return !pendingSigSet.empty(); }

  NON_COPYABLE(ARState);

  ARState();

  ~ARState() = default;

  bool hasError() const { return this->stack.hasError(); }

  void throwObject(ObjPtr<ErrorObject> &&except) { this->stack.setErrorObj(std::move(except)); }

  // variable manipulation
  void setGlobal(unsigned int index, const Value &obj) { this->setGlobal(index, Value(obj)); }

  void setGlobal(BuiltinVarOffset offset, Value &&obj) {
    this->setGlobal(toIndex(offset), std::move(obj));
  }

  void setGlobal(unsigned int index, Value &&obj) { this->globals[index] = std::move(obj); }

  const Value &getGlobal(unsigned int index) const { return this->globals[index]; }

  const Value &getGlobal(BuiltinVarOffset offset) const { return this->getGlobal(toIndex(offset)); }

  void setLocal(unsigned char index, Value &&obj) { this->stack.setLocal(index, std::move(obj)); }

  const Value &getLocal(unsigned char index) const { return this->stack.getLocal(index); }

  Value moveLocal(unsigned char index) { return this->stack.moveLocal(index); }

  /**
   * get exit status ($? & 0xFF)
   * @return
   */
  int getMaskedExitStatus() const {
    return maskExitStatus(this->getGlobal(BuiltinVarOffset::EXIT_STATUS).asInt());
  }

  void setExitStatus(int64_t status) {
    this->setGlobal(BuiltinVarOffset::EXIT_STATUS, Value::createInt(status));
  }

  void updatePipeStatus(unsigned int size, const Proc *procs, bool mergeExitStatus);

  int64_t subshellLevel() const { return this->getGlobal(BuiltinVarOffset::SUBSHELL).asInt(); }

  void incSubShellLevel() {
    const auto level = this->subshellLevel() + 1;
    this->setGlobal(BuiltinVarOffset::SUBSHELL, Value::createInt(level));
  }

  bool isRootShell() const { return this->subshellLevel() == 0; }

  bool isJobControl() const { return this->isRootShell() && this->has(RuntimeOption::MONITOR); }

  /**
   *
   * @return
   * if success, return 0.
   * if not ARState::isForeground is false, return 1.
   * if error, return -1 and set errno
   */
  int tryToBeForeground() const {
    if (this->isJobControl()) {
      return beForeground(0);
    }
    return 1;
  }

  void setVMHook(VMHook *h) { this->hook = h; }

  VMHook *getVMHook() const { return this->hook; }

  const VMState &getCallStack() const { return this->stack; }

  VMState &getCallStack() { return this->stack; }

  auto getWorkingDir(bool useLogical = true) const {
    return arsh::getWorkingDir(this->logicalWorkingDir, useLogical);
  }

  L64X128MixRNG &getRng() { return this->rng; }

  bool has(RuntimeOption option) const { return hasFlag(this->runtimeOption, option); }

  RuntimeOption getOption() const { return this->runtimeOption; }

  void setOption(RuntimeOption option) { this->runtimeOption = option; }

  short getReadlineCallCount() const { return this->readlineCallCount; }

  void incReadlineCallCount() { this->readlineCallCount++; }

  void declReadlineCallCount() { this->readlineCallCount--; }
};

namespace arsh {

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
    const ARCode *udc_;
    const ModType *modType_;
    builtin_command_t builtinCmd_;
    Object *cmdObj_;
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

  static ResolvedCmd fromCmdObj(Object *obj) {
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

  const ARCode &udc() const { return *this->udc_; }

  const ModType &modType() const { return *this->modType_; }

  builtin_command_t builtinCmd() const { return this->builtinCmd_; }

  Object *cmdObj() const { return this->cmdObj_; }

  const char *filePath() const { return this->filePath_; }
};

class CmdResolver {
public:
  enum class Op : unsigned short {
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
  Op resolveOp;
  FilePathCache::SearchOp searchOp;

public:
  CmdResolver(Op mask, FilePathCache::SearchOp op) : resolveOp(mask), searchOp(op) {}

  CmdResolver() : CmdResolver(Op::FROM_DEFAULT, FilePathCache::SearchOp::NON) {}

  ~CmdResolver() = default;

  /**
   *
   * @param state
   * @param ref
   * @param modType
   * if specified USE_FQN, always ignore
   * @return
   */
  ResolvedCmd operator()(const ARState &state, StringRef ref,
                         const ModType *modType = nullptr) const;
};

template <>
struct allow_enum_bitop<CmdResolver::Op> : std::true_type {};

enum class CmdCallAttr : unsigned char {
  SET_VAR = 1u << 0u,
  NEED_FORK = 1u << 1u, // for external command
  RAISE = 1u << 2u,
  CLOSURE = 1u << 3u, // for command object call
};

template <>
struct allow_enum_bitop<CmdCallAttr> : std::true_type {};

class RecursionGuard {
private:
  ARState &state;

public:
  explicit RecursionGuard(ARState &st) : state(st) { this->state.getCallStack().incRecDepth(); }

  ~RecursionGuard() { this->state.getCallStack().decRecDepth(); }

  bool checkLimit();
};

using native_func_t = Value (*)(ARState &);

class VM {
private:
  static void pushExitStatus(ARState &state, int64_t status) {
    state.setExitStatus(status);
    state.stack.push(exitStatusToBool(status));
  }

  static bool windStackFrame(ARState &state, unsigned int stackTopOffset, unsigned int paramSize,
                             const ARCode &code) {
    const auto ret = state.stack.wind(stackTopOffset, paramSize, code);
    if (unlikely(!ret)) {
      raiseError(state, TYPE::StackOverflowError, "local stack size reaches limit");
    }
    return ret;
  }

  // runtime api
  static bool instanceOf(const TypePool &pool, const Value &value, const Type &targetType) {
    if (value.isInvalid()) {
      return targetType.isOptionType();
    }
    return targetType.isSameOrBaseTypeOf(pool.get(value.getTypeID()));
  }

  static bool checkCast(ARState &state, const Type &targetType);

  static const char *loadEnv(ARState &state, bool hasDefault);

  static bool storeEnv(ARState &state);

  static void pushNewObject(ARState &state, const Type &type);

  /**
   * stack state in function apply    stack grow ===>
   *
   * +-----------+---------+--------+   +--------+
   * | stack top | funcObj | param1 | ~ | paramN |
   * +-----------+---------+--------+   +--------+
   *                       | offset |   |        |
   */
  static bool prepareFuncCall(ARState &state, unsigned int paramSize) {
    const ARCode *code;
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
  static bool prepareMethodCall(ARState &state, unsigned short index,
                                unsigned short actualParamSize) {
    auto value = state.getGlobal(index);
    if (unlikely(!value)) {
      raiseError(state, TYPE::IllegalAccessError,
                 "attempt to call uninitialized method or constructor");
      return false;
    }
    const auto &func = typeAs<FuncObject>(value);
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
  static bool prepareUserDefinedCommandCall(ARState &state, const ARCode &code,
                                            ObjPtr<ArrayObject> &&argvObj, Value &&redirConfig,
                                            CmdCallAttr attr);

  static bool attachAsyncJob(ARState &state, Value &&desc, unsigned int procSize, const Proc *procs,
                             ForkKind forkKind, PipeSet &pipeSet, Value &ret);

  static bool forkAndEval(ARState &state, Value &&desc);

  static bool forkAndExec(ARState &state, const char *filePath, const ArrayObject &argvObj,
                          Value &&redirConfig);

  static bool prepareSubCommand(ARState &state, const ModType &modType,
                                ObjPtr<ArrayObject> &&argvObj, Value &&redirConfig);

  static bool callCommand(ARState &state, CmdResolver resolver, ObjPtr<ArrayObject> &&argvObj,
                          Value &&redirConfig, CmdCallAttr attr = {});

  static bool callCommand(ARState &state, const ResolvedCmd &cmd, ObjPtr<ArrayObject> &&argvObj,
                          Value &&redirConfig, CmdCallAttr attr);

  struct BuiltinCmdResult {
    enum Kind : unsigned char {
      DISPLAY, // display command info
      CALL,    // call command
    } kind;
    bool r;
    int status;

    static BuiltinCmdResult display(int status) {
      return {
          .kind = DISPLAY,
          .r = true,
          .status = status,
      };
    }

    static BuiltinCmdResult call(bool r) {
      return {
          .kind = CALL,
          .r = r,
          .status = 0,
      };
    }
  };

  static BuiltinCmdResult builtinCommand(ARState &state, ObjPtr<ArrayObject> &&argvObj,
                                         Value &&redir, CmdCallAttr attr);

  static int builtinExec(ARState &state, ArrayObject &argvObj, Value &&redir);

  static bool returnFromUserDefinedCommand(ARState &state, int64_t status);

  /**
   * @param state
   * @param desc
   * @param lastPipe
   * if true, evaluate last pipe in parent shell
   * @param forkKind
   * @return
   * if has error, return false.
   */
  static bool callPipeline(ARState &state, Value &&desc, bool lastPipe, ForkKind forkKind);

  static bool applyTildeExpansion(ARState &state, StringRef path, bool assign);

  static bool addGlobbingPath(ARState &state, ArrayObject &argv, const Value *begin,
                              const Value *end);

  static bool applyBraceExpansion(ARState &state, ArrayObject &argv, const Value *begin,
                                  const Value *end);

  static bool addExpandingPath(ARState &state, unsigned int size, ExpandOp expandOp);

  static bool kickSignalHandler(ARState &state, int sigNum, Value &&func);

  static bool kickTermHook(ARState &state, Value &&func);

  static void kickVMHook(ARState &state);

  /**
   * @param state
   * @return
   * if has exception, return false.
   */
  static bool mainLoop(ARState &state);

  static void rethrowFromFinally(ARState &state);

  /**
   * if found exception handler, return true.
   * otherwise return false.
   * @param state
   * @return
   */
  static bool handleException(ARState &state);

  /**
   * actual entry point of interpreter.
   * @param state
   * @param op
   * @param dsError
   * if not null, set error info
   * @return
   * if has error or not value, return null
   * otherwise, return value
   */
  static Value startEval(ARState &state, EvalOP op, ARError *dsError);

public:
  // entry point
  /**
   * entry point of toplevel code evaluation.
   * @param state
   * @param func
   * must be toplevel compiled function.
   * @param dsError
   * if not null, set error information
   */
  static void callToplevel(ARState &state, const ObjPtr<FuncObject> &func, ARError *dsError);

  /**
   * execute command.
   * @param state
   * @param argv
   * @return
   * if exit status is 0, return 'true'.
   * otherwise, return 'false'
   */
  static Value callCommand(ARState &state, ObjPtr<ArrayObject> &&argv);

  /**
   * @param state
   * @param funcObj
   * @param args
   * @return
   * return value of function (if no return value, return null).
   */
  static Value callFunction(ARState &state, Value &&funcObj, CallArgs &&args);

  /**
   * call method (builtin or user-defined)
   * @param state
   * @param handle
   * @param recv
   * @param args
   * @return return value of function (if no return value, return null).
   */
  static Value callMethod(ARState &state, const MethodHandle &handle, Value &&recv,
                          CallArgs &&args);

  /**
   * for user-defined constructor call
   * @param state
   * @param handle must be constructor
   * @param args
   * @return
   */
  static Value callConstructor(ARState &state, const MethodHandle &handle, CallArgs &&args) {
    return callMethod(state, handle, Value(), std::move(args));
  }

  /**
   * print uncaught exception information. (clear thrown object)
   * @param state
   * @param dsError
   * if not null, set error information
   * @param inTermHook
   */
  static void handleUncaughtException(ARState &state, ARError *dsError, bool inTermHook = false);

  /**
   * call user-defined termination handler specified by TERM_HOOK.
   * after call TERM_HOOK, send SIGHUP to manged jobs
   * @param state
   */
  static void prepareTermination(ARState &state);
};

} // namespace arsh

#endif // ARSH_VM_H
