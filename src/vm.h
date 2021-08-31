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

#include <chrono>
#include <cstdio>

#include <ydsh/ydsh.h>

#include "cmd.h"
#include "core.h"
#include "job.h"
#include "misc/noncopyable.h"
#include "scope.h"
#include "signals.h"
#include "state.h"

namespace ydsh {

struct PipeSet;

enum class CompileOption : unsigned short {
  ASSERT = 1u << 0u,
  INTERACTIVE = 1u << 1u,
};

#define EACH_RUNTIME_OPTION(OP)                                                                    \
  OP(TRACE_EXIT, (1u << 0u), "traceonexit")                                                        \
  OP(MONITOR, (1u << 1u), "monitor")                                                               \
  OP(NULLGLOB, (1u << 2u), "nullglob")                                                             \
  OP(DOTGLOB, (1u << 3u), "dotglob")                                                               \
  OP(FASTGLOB, (1u << 4u), "fastglob")

// set/unset via 'shctl' command
enum class RuntimeOption : unsigned short {
#define GEN_ENUM(E, V, N) E = V,
  EACH_RUNTIME_OPTION(GEN_ENUM)
#undef GEN_ENUM
};

enum class VMEvent : unsigned int {
  HOOK = 1u << 0u,
  SIGNAL = 1u << 1u,
  MASK = 1u << 2u,
};

enum class EvalOP : unsigned int {
  PROPAGATE = 1u << 0u,  // propagate uncaught exception to caller (except for subshell).
  SKIP_TERM = 1u << 1u,  // not call termination handler (except for subshell).
  HAS_RETURN = 1u << 2u, // may have return value.
};

enum class EvalRet : unsigned int {
  SUCCESS,       // normal termination
  HAS_ERROR,     // still has uncaught error
  HANDLED_ERROR, // already handled uncaught error
};

template <>
struct allow_enum_bitop<CompileOption> : std::true_type {};

template <>
struct allow_enum_bitop<RuntimeOption> : std::true_type {};

template <>
struct allow_enum_bitop<VMEvent> : std::true_type {};

template <>
struct allow_enum_bitop<EvalOP> : std::true_type {};

class VM;

} // namespace ydsh

using namespace ydsh;

struct DSState {
public:
  ModuleLoader modLoader;

  TypePool typePool;

  IntrusivePtr<NameScope> builtinModScope;

  IntrusivePtr<NameScope> rootModScope;

  const ObjPtr<UnixFdObject> emptyFDObj;

  /**
   * maintains latest result of EDIT_HOOK
   */
  DSValue editOpReply;

  /**
   * maintain latest rendered prompt/
   */
  DSValue prompt;

  CompileOption compileOption{CompileOption::ASSERT};

  RuntimeOption runtimeOption{};

  DSExecMode execMode{DS_EXEC_MODE_NORMAL};

  DumpTarget dumpTarget;

  /**
   * cache searched result.
   */
  FilePathCache pathCache;

  unsigned int lineNum{1};

  /**
   * if 0, current shell is not sub-shell.
   * otherwise current shell is sub-shell.
   */
  unsigned int subshellLevel{0};

  std::string logicalWorkingDir;

  SignalVector sigVector;

  JobTable jobTable;

  /**
   * for OP_STR.
   */
  std::string toStrBuf;

private:
  friend class ydsh::VM;

  VMHook *hook{nullptr};

  std::vector<DSValue> globals{64};

  VMState stack;

  decltype(std::chrono::system_clock::now()) baseTime;

public:
  static VMEvent eventDesc;

  static SigSet pendingSigSet;

  static int popPendingSignal() {
    int sigNum = DSState::pendingSigSet.popPendingSig();
    if (DSState::pendingSigSet.empty()) {
      unsetFlag(DSState::eventDesc, VMEvent::SIGNAL);
    }
    return sigNum;
  }

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
    if (DSState::pendingSigSet.empty()) {
      unsetFlag(DSState::eventDesc, VMEvent::SIGNAL);
    }
  }

  NON_COPYABLE(DSState);

  DSState();

  ~DSState() = default;

  bool hasError() const { return this->stack.hasError(); }

  /**
   * set thrownObject and update exit status
   * @param except
   * @param afterStatus
   * set exit status to it
   */
  void throwObject(DSValue &&except, int afterStatus) {
    this->stack.setThrownObject(std::move(except));
    this->setExitStatus(afterStatus);
  }

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

  void updatePipeStatus(unsigned int size, const Proc *procs, bool mergeExitStatus) const;

  bool isJobControl() const { return hasFlag(this->runtimeOption, RuntimeOption::MONITOR); }

  bool isRootShell() const {
    auto shellpid = this->getGlobal(BuiltinVarOffset::SHELL_PID).asInt();
    auto pid = this->getGlobal(BuiltinVarOffset::PID).asInt();
    return shellpid == pid;
  }

  bool isForeground() const { return this->isJobControl() && this->isRootShell(); }

  /**
   *
   * @return
   * if success, return 0.
   * if not DSState::isForeground is false, return 1.
   * if error, return -1 and set errno
   */
  int tryToBeForeground() const {
    if (this->isForeground()) {
      return beForeground(0);
    }
    return 1;
  }

  void setVMHook(VMHook *h) {
    this->hook = h;
    if (this->hook != nullptr) {
      setFlag(eventDesc, VMEvent::HOOK);
    } else {
      unsetFlag(eventDesc, VMEvent::HOOK);
    }
  }

  const VMState &getCallStack() const { return this->stack; }
};

namespace ydsh {

class ResolvedCmd {
public:
  enum CmdKind : unsigned char {
    USER_DEFINED, // user-defined command
    MODULE,       // module subcommand
    BUILTIN_S,    // builtin command written by vm api (ex. command, eval, exec)
    BUILTIN,      // builtin command
    EXTERNAL,     // external command
    INVALID,      // invalid command name (contains null character)
    ILLEGAL_UDC,  // uninitialized user-defined command
  };

private:
  CmdKind kind_;
  unsigned int belongModTypeId_; // if not belong to module (external, builtin, ect), indicate 0
  union {
    const DSCode *udc_;
    const ModType *modType_;
    builtin_command_t builtinCmd_;
    const char *filePath_;
  };

public:
  static ResolvedCmd fromUdc(const FuncObject &func, const ModType *belongModType) {
    ResolvedCmd cmd; // NOLINT
    cmd.kind_ = CmdKind::USER_DEFINED;
    cmd.belongModTypeId_ = belongModType ? belongModType->typeId() : 0;
    cmd.udc_ = &func.getCode();
    return cmd;
  }

  static ResolvedCmd fromMod(const ModType &modType, const ModType *belongModType) {
    ResolvedCmd cmd; // NOLINT
    cmd.kind_ = CmdKind::MODULE;
    cmd.belongModTypeId_ = belongModType ? belongModType->typeId() : 0;
    cmd.modType_ = &modType;
    return cmd;
  }

  static ResolvedCmd fromBuiltin(builtin_command_t bcmd) {
    ResolvedCmd cmd; // NOLINT
    cmd.kind_ = CmdKind::BUILTIN;
    cmd.belongModTypeId_ = 0;
    cmd.builtinCmd_ = bcmd;
    return cmd;
  }

  static ResolvedCmd fromBuiltin(const NativeCode &code) {
    ResolvedCmd cmd; // NOLINT
    cmd.kind_ = CmdKind::BUILTIN_S;
    cmd.belongModTypeId_ = 0;
    cmd.udc_ = &code;
    return cmd;
  }

  static ResolvedCmd fromExternal(const char *path) {
    ResolvedCmd cmd; // NOLINT
    cmd.kind_ = CmdKind::EXTERNAL;
    cmd.belongModTypeId_ = 0;
    cmd.filePath_ = path;
    return cmd;
  }

  static ResolvedCmd invalid() {
    ResolvedCmd cmd; // NOLINT
    cmd.kind_ = CmdKind::INVALID;
    cmd.belongModTypeId_ = 0;
    cmd.filePath_ = nullptr;
    return cmd;
  }

  static ResolvedCmd illegalUdc() {
    ResolvedCmd cmd; // NOLINT
    cmd.kind_ = CmdKind::ILLEGAL_UDC;
    cmd.belongModTypeId_ = 0;
    cmd.udc_ = nullptr;
    return cmd;
  }

  CmdKind kind() const { return this->kind_; }

  unsigned int belongModTypeId() const { return this->belongModTypeId_; }

  const DSCode &udc() const { return *this->udc_; }

  const ModType &modType() const { return *this->modType_; }

  builtin_command_t builtinCmd() const { return this->builtinCmd_; }

  const char *filePath() const { return this->filePath_; }
};

class CmdResolver {
public:
  enum ResolveOp {
    FROM_UDC = 1u << 0u,
    FROM_BUILTIN = 1u << 1u,
    FROM_EXTERNAL = 1u << 2u,
    FROM_FALLBACK = (1u << 3u) | FROM_EXTERNAL,
    USE_FQN = (1u << 4u) | FROM_UDC,

    NO_FALLBACK = FROM_UDC | FROM_BUILTIN | FROM_EXTERNAL,
    NO_UDC = FROM_BUILTIN | FROM_EXTERNAL | FROM_FALLBACK,
    FROM_DEFAULT = FROM_UDC | NO_UDC,
    FROM_DEFAULT_WITH_FQN = FROM_DEFAULT | USE_FQN,
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
   * @param modType
   * if specified USE_FQN, always ignore
   * @return
   */
  ResolvedCmd operator()(DSState &state, StringRef cmdName, const ModType *modType = nullptr) const;
};

template <>
struct allow_enum_bitop<CmdResolver::ResolveOp> : std::true_type {};

enum class CmdCallAttr : unsigned int {
  SET_VAR = 1u << 0u,
  NEED_FORK = 1u << 1u,
};

template <>
struct allow_enum_bitop<CmdCallAttr> : std::true_type {};

class VM {
private:
  static void pushExitStatus(DSState &state, int64_t status) {
    state.setExitStatus(status);
    state.stack.push(exitStatusToBool(status));
  }

  static bool windStackFrame(DSState &state, unsigned int stackTopOffset, unsigned int paramSize,
                             const DSCode &code) {
    auto ret = state.stack.wind(stackTopOffset, paramSize, code);
    if (!ret) {
      raiseError(state, TYPE::StackOverflowError, "local stack size reaches limit");
    }
    return ret;
  }

  // runtime api
  static bool instanceOf(const TypePool &pool, const DSValue &value, const DSType &targetType) {
    return targetType.isSameOrBaseTypeOf(pool.get(value.getTypeID()));
  }

  static bool checkCast(DSState &state, const DSType &targetType);

  static bool checkAssertion(DSState &state);

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
    auto &func = typeAs<FuncObject>(state.stack.peekByOffset(paramSize));
    return windStackFrame(state, paramSize + 1, paramSize, func.getCode());
  }

  /**
   * stack state in method call    stack grow ===>
   *
   * +-----------+------------------+   +--------+
   * | stack top | param1(receiver) | ~ | paramN |
   * +-----------+------------------+   +--------+
   *             | offset           |   |        |
   */
  static bool prepareMethodCall(DSState &state, unsigned short index, unsigned short paramSize) {
    const unsigned int actualParamSize = paramSize + 1; // include receiver
    (void)index;
    (void)state;
    (void)actualParamSize;
    //        /*return */windStackFrame(state, actualParamSize, actualParamSize,
    //                              /*state.stack.peekByOffset(paramSize)->getType()->getMethodRef(index)*/nullptr);
    fatal("FIXME: normal method call is not implemented!!\n");
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
                                            DSValue &&restoreFD, CmdCallAttr attr);

  static bool attachAsyncJob(DSState &state, unsigned int procSize, const Proc *procs,
                             ForkKind forkKind, PipeSet &pipeSet, DSValue &ret);

  static bool forkAndEval(DSState &state);

  static int forkAndExec(DSState &state, const char *filePath, char *const *argv,
                         DSValue &&redirConfig);

  static bool prepareSubCommand(DSState &state, const ModType &modType, DSValue &&argvObj,
                                DSValue &&restoreFD);

  static bool callCommand(DSState &state, CmdResolver resolver, DSValue &&argvObj,
                          DSValue &&redirConfig, CmdCallAttr attr = {});

  static bool callCommand(DSState &state, const ResolvedCmd &cmd, DSValue &&argvObj,
                          DSValue &&redirConfig, CmdCallAttr attr);

  static bool builtinCommand(DSState &state, DSValue &&argvObj, DSValue &&redir, CmdCallAttr attr);

  static void builtinExec(DSState &state, DSValue &&array, DSValue &&redir);

  /**
   *
   * @param lastPipe
   * if true, evaluate last pipe in parent shell
   * @return
   * if has error, return false.
   */
  static bool callPipeline(DSState &state, bool lastPipe, ForkKind forkKind);

  static void addCmdArg(DSState &state, bool skipEmptyStr);

  /**
   *
   * @param state
   * @param size
   * @param tilde
   * if true, apply tilde expansion
   * @return
   * if has error, return false.
   */
  static bool addGlobbingPath(DSState &state, unsigned int size, bool tilde);

  static bool kickSignalHandler(DSState &state, int sigNum, DSValue &&func);

  static bool checkVMEvent(DSState &state);

  /**
   *
   * @return
   * if has exception, return false.
   */
  static bool mainLoop(DSState &state);

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

  /**
   * print uncaught exception information.
   * @param except
   * uncaught exception
   * @param dsError
   * if not null, set error information
   * @return
   * if except is null, return always DS_ERROR_KIND_SUCCESS and not set error info
   */
  static DSErrorKind handleUncaughtException(DSState &state, const DSValue &except,
                                             DSError *dsError);

  /**
   * call user-defined termination handler specified by TERM_HOOK.
   * @param kind
   * @param except
   */
  static void callTermHook(DSState &state, DSErrorKind kind, DSValue &&except);

public:
  // entry point
  /**
   * entry point of toplevel code evaluation.
   * @param code
   * must be toplevel compiled code.
   * @param dsError
   * if not null, set error information
   * @return
   * if had uncaught exception, return false.
   */
  static bool callToplevel(DSState &state, const CompiledCode &code, DSError *dsError);

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
   * call method.
   * @param handle
   * @param recv
   * @param args
   * @return
   * return value of method (if no return value, return null).
   */
  static DSValue callMethod(DSState &state, const MethodHandle &handle, DSValue &&recv,
                            std::pair<unsigned int, std::array<DSValue, 3>> &&args);

  /**
   *
   * @param funcObj
   * @param args
   * @return
   * return value of method (if no return value, return null).
   */
  static DSValue callFunction(DSState &state, DSValue &&funcObj,
                              std::pair<unsigned int, std::array<DSValue, 3>> &&args);
};

// entry point of code evaluation
/**
 * entry point of toplevel code evaluation.
 * @param code
 * must be toplevel compiled code.
 * @param dsError
 * if not null, set error information
 * @return
 * if had uncaught exception, return false
 */
inline bool callToplevel(DSState &state, const CompiledCode &code, DSError *dsError) {
  return VM::callToplevel(state, code, dsError);
}

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
inline DSValue execCommand(DSState &state, std::vector<DSValue> &&argv, bool propagate) {
  return VM::execCommand(state, std::move(argv), propagate);
}

/**
 * call method.
 * @param handle
 * @param recv
 * @param args
 * @return
 * return value of method (if no return value, return null).
 */
inline DSValue callMethod(DSState &state, const MethodHandle &handle, DSValue &&recv,
                          std::pair<unsigned int, std::array<DSValue, 3>> &&args) {
  return VM::callMethod(state, handle, std::move(recv), std::move(args));
}

/**
 *
 * @param funcObj
 * @param args
 * @return
 * return value of method (if no return value, return null).
 */
inline DSValue callFunction(DSState &state, DSValue &&funcObj,
                            std::pair<unsigned int, std::array<DSValue, 3>> &&args) {
  return VM::callFunction(state, std::move(funcObj), std::move(args));
}

} // namespace ydsh

#endif // YDSH_VM_H
