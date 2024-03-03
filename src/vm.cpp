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

#include <pwd.h>

#include <cerrno>
#include <cstdlib>
#include <ctime>

#include "arg_parser.h"
#include "logger.h"
#include "misc/files.hpp"
#include "misc/num_util.hpp"
#include "opcode.h"
#include "ordered_map.h"
#include "redir.h"
#include "vm.h"

// #####################
// ##     ARState     ##
// #####################

SigSet ARState::pendingSigSet;

/**
 * if environmental variable SHLVL dose not exist, set 0.
 */
static int64_t getShellLevel() {
  const char *shlvl = getenv(ENV_SHLVL);
  int64_t level = 0;
  if (shlvl != nullptr) {
    const auto pair = convertToDecimal<int64_t>(shlvl);
    if (pair && pair.value > -1) {
      level = pair.value;
    }
  }
  return level;
}

static int64_t originalShellLevel() {
  static auto level = getShellLevel();
  return level;
}

static void setPWDs() {
  const auto v = getCWD();
  const char *cwd = v ? v.get() : ".";

  const char *pwd = getenv(ENV_PWD);
  if (strcmp(cwd, ".") == 0 || !pwd || *pwd != '/' || !isSameFile(pwd, cwd)) {
    setenv(ENV_PWD, cwd, 1);
    pwd = cwd;
  }

  if (const char *oldPwd = getenv(ENV_OLDPWD);
      !oldPwd || *oldPwd != '/' || !S_ISDIR(getStMode(oldPwd))) {
    setenv(ENV_OLDPWD, pwd, 1);
  }
}

static void initEnv() {
  // set locale
  setLocaleSetting();

  // set environmental variables

  // update shell level
  if (const auto level = originalShellLevel(); level < INT64_MAX) {
    setenv(ENV_SHLVL, std::to_string(level + 1).c_str(), 1);
  } else {
    setenv(ENV_SHLVL, "1", 1);
  }

  // not overwrite existing environmental variable for compatibility (ex. sudo)
  if (const struct passwd *pw = getpwuid(getuid()); likely(pw != nullptr)) {
    // set HOME
    setenv(ENV_HOME, pw->pw_dir, 0 /*not overwrite */);

    // set LOGNAME
    setenv(ENV_LOGNAME, pw->pw_name, 0 /*not overwrite */);

    // set USER
    setenv(ENV_USER, pw->pw_name, 0 /*not overwrite */);
  } else {
#ifndef __EMSCRIPTEN__
    fatal_perror("getpwuid failed");
#endif
  }

  // set PWD/OLDPWD
  setPWDs();
}

static bool check_strftime_plus(timestamp ts) {
  const auto time = timestampToTimespec(ts);
  struct tm tm {};
  if (gmtime_r(&time.tv_sec, &tm)) {
    char buf[64];
    const auto s = strftime_l(buf, std::size(buf), "%+", &tm, POSIX_LOCALE_C.get());
    if (const auto ret = StringRef(buf, s); !ret.empty() && ret != "%+") {
      return true;
    }
  }
  return false;
}

ARState::ARState()
    : modLoader(this->sysConfig),
      emptyFDObj(toObjPtr<UnixFdObject>(Value::create<UnixFdObject>(-1))),
      initTime(getCurrentTimestamp()), support_strftime_plus(check_strftime_plus(this->initTime)),
      baseTime(this->initTime), rng(this->baseTime.time_since_epoch().count()) {
  // init envs
  initEnv();
  const char *pwd = getenv(ENV_PWD);
  assert(pwd);
  if (*pwd == '/') {
    this->logicalWorkingDir = expandDots(nullptr, pwd);
  }
}

void ARState::updatePipeStatus(unsigned int size, const Proc *procs, bool mergeExitStatus) {
  if (auto &obj = typeAs<ArrayObject>(this->getGlobal(BuiltinVarOffset::PIPESTATUS));
      obj.getRefcount() > 1) {
    auto &type = this->typePool.get(obj.getTypeID());
    this->setGlobal(BuiltinVarOffset::PIPESTATUS, Value::create<ArrayObject>(type));
  } else { // reuse existing object
    obj.refValues().clear();
    obj.refValues().reserve(size + (mergeExitStatus ? 1 : 0));
  }

  auto &obj = typeAs<ArrayObject>(this->getGlobal(BuiltinVarOffset::PIPESTATUS));
  for (unsigned int i = 0; i < size; i++) {
    obj.append(Value::createInt(procs[i].exitStatus())); // not check iterator invalidation
  }
  if (mergeExitStatus) {
    obj.append(this->getGlobal(BuiltinVarOffset::EXIT_STATUS)); // not check iterator invalidation
  }
  ASSERT_ARRAY_SIZE(obj);
}

namespace arsh {

bool VM::checkCast(ARState &state, const DSType &targetType) {
  if (!instanceOf(state.typePool, state.stack.peek(), targetType)) {
    auto &stackTopType = state.typePool.get(state.stack.pop().getTypeID());
    std::string str("cannot cast `");
    str += stackTopType.getNameRef();
    str += "' to `";
    str += targetType.getNameRef();
    str += "'";
    raiseError(state, TYPE::TypeCastError, std::move(str));
    return false;
  }
  return true;
}

const char *VM::loadEnv(ARState &state, bool hasDefault) {
  Value dValue;
  if (hasDefault) {
    dValue = state.stack.pop();
  }
  const auto nameObj = state.stack.pop();
  const auto nameRef = nameObj.asStrRef();
  assert(!nameRef.hasNullChar());
  const char *name = nameRef.data();
  const char *env = getenv(name);
  if (env == nullptr && hasDefault) {
    const auto ref = dValue.asStrRef();
    if (ref.hasNullChar()) {
      std::string message = ERROR_SET_ENV;
      message += name;
      raiseError(state, TYPE::IllegalAccessError, std::move(message));
      return nullptr;
    }
    setenv(name, ref.data(), 1);
    env = getenv(name);
  }

  if (env == nullptr) {
    std::string message = ERROR_UNDEF_ENV;
    message += name;
    raiseError(state, TYPE::IllegalAccessError, std::move(message));
  }
  return env;
}

bool VM::storeEnv(ARState &state) {
  const auto value = state.stack.pop();
  const auto name = state.stack.pop();
  const auto nameRef = name.asStrRef();
  const auto valueRef = value.asStrRef();
  assert(!nameRef.hasNullChar());
  if (setenv(valueRef.hasNullChar() ? "" : nameRef.data(), valueRef.data(), 1) == 0) {
    return true;
  }
  std::string str = ERROR_SET_ENV;
  str += nameRef;
  raiseError(state, TYPE::IllegalAccessError, std::move(str));
  return false;
}

void VM::pushNewObject(ARState &state, const DSType &type) {
  Value value;
  switch (type.typeKind()) {
  case TypeKind::Array:
    value = Value::create<ArrayObject>(type);
    break;
  case TypeKind::Map:
    value = Value::create<OrderedMapObject>(type, state.getRng().next());
    break;
  case TypeKind::Tuple:
    value = Value::create<BaseObject>(cast<TupleType>(type));
    break;
  case TypeKind::Record:
  case TypeKind::CLIRecord:
    value = Value::create<BaseObject>(cast<RecordType>(type));
    break;
  default:
    value = Value::createDummy(type);
    break;
  }
  state.stack.push(std::move(value));
}

bool VM::prepareUserDefinedCommandCall(ARState &state, const DSCode &code,
                                       ObjPtr<ArrayObject> &&argvObj, Value &&redirConfig,
                                       const CmdCallAttr attr) {
  // set parameter
  state.stack.reserve(UDC_PARAM_N);
  state.stack.push(Value::createNum(toUnderlying(attr)));
  state.stack.push(std::move(redirConfig));
  state.stack.push(argvObj);

  const unsigned int stackTopOffset = UDC_PARAM_N + (hasFlag(attr, CmdCallAttr::CLOSURE) ? 1 : 0);
  if (unlikely(!windStackFrame(state, stackTopOffset, UDC_PARAM_N, code))) {
    return false;
  }

  if (hasFlag(attr, CmdCallAttr::SET_VAR)) { // set variable
    auto &argv = typeAs<ArrayObject>(state.stack.getLocal(UDC_PARAM_ARGV));
    auto cmdName = argv.takeFirst();                              // not check iterator invalidation
    state.stack.setLocal(UDC_PARAM_ARGV + 1, std::move(cmdName)); // 0
  }
  return true;
}

#define CODE(ctx) ((ctx).stack.code())
#define CONST_POOL(ctx) (cast<CompiledCode>(CODE(ctx))->getConstPool())

/* for substitution */

static bool readAsStr(ARState &state, int fd, Value &ret) {
  std::string str;
  while (true) {
    char buf[256];
    const ssize_t readSize = read(fd, buf, std::size(buf));
    if (readSize == -1 && errno == EAGAIN) {
      continue;
    }
    if (readSize <= 0) {
      if (readSize < 0) {
        raiseSystemError(state, errno, ERROR_CMD_SUB);
        return false;
      }
      break;
    }
    if (unlikely(!checkedAppend(StringRef(buf, readSize), StringObject::MAX_SIZE, str))) {
      raiseError(state, TYPE::OutOfRangeError, ERROR_STRING_LIMIT);
      return false;
    }
  }

  // remove last newlines
  for (; !str.empty() && str.back() == '\n'; str.pop_back())
    ;

  ret = Value::createStr(std::move(str));
  return true;
}

static bool readAsStrArray(ARState &state, int fd, Value &ret) {
  const auto ifsRef = state.getGlobal(BuiltinVarOffset::IFS).asStrRef();
  unsigned int skipCount = 1;
  std::string str;
  auto obj = Value::create<ArrayObject>(state.typePool.get(TYPE::StringArray));
  auto &array = typeAs<ArrayObject>(obj);

  while (true) {
    char buf[256];
    const ssize_t readSize = read(fd, buf, std::size(buf));
    if (readSize == -1 && errno == EAGAIN) {
      continue;
    }
    if (readSize <= 0) {
      if (readSize < 0) {
        raiseSystemError(state, errno, ERROR_CMD_SUB);
        return false;
      }
      break;
    }

    for (ssize_t i = 0; i < readSize; i++) {
      const char ch = buf[i];
      const bool fieldSep = matchFieldSep(ifsRef, ch);
      if (fieldSep && skipCount > 0) {
        if (isSpace(ch)) {
          continue;
        }
        if (--skipCount == 1) {
          continue;
        }
      }
      skipCount = 0;
      if (fieldSep) {
        if (unlikely(!array.append(state, Value::createStr(std::move(str))))) {
          return false;
        }
        str = "";
        skipCount = isSpace(ch) ? 2 : 1;
        continue;
      }
      if (unlikely(str.size() == StringObject::MAX_SIZE)) {
        raiseError(state, TYPE::OutOfRangeError, ERROR_STRING_LIMIT);
        return false;
      }
      str += ch;
    }
  }

  // remove last newline
  for (; !str.empty() && str.back() == '\n'; str.pop_back())
    ;

  // append remain
  if (!str.empty() || !hasSpace(ifsRef)) {
    if (unlikely(!array.append(state, Value::createStr(std::move(str))))) {
      return false;
    }
  }

  ret = std::move(obj);
  return true;
}

static ObjPtr<UnixFdObject> newFD(const ARState &st, int &fd) {
  if (fd < 0) {
    return st.emptyFDObj;
  }
  int v = fd;
  fd = -1;
  remapFDCloseOnExec(v);
  return toObjPtr<UnixFdObject>(Value::create<UnixFdObject>(v));
}

bool VM::attachAsyncJob(ARState &state, Value &&desc, unsigned int procSize, const Proc *procs,
                        ForkKind forkKind, PipeSet &pipeSet, Value &ret) {
  switch (forkKind) {
  case ForkKind::NONE:
  case ForkKind::PIPE_FAIL: {
    const auto entry = JobObject::create(procSize, procs, false, state.emptyFDObj, state.emptyFDObj,
                                         std::move(desc));
    // job termination
    const auto waitOp = state.isJobControl() ? WaitOp::BLOCK_UNTRACED : WaitOp::BLOCKING;
    const int status = entry->wait(waitOp);
    const int errNum = errno;
    state.updatePipeStatus(entry->getProcSize(), entry->getProcs(), false);
    if (entry->isRunning()) {
      const auto job = state.jobTable.attach(entry);
      if (job->getProcs()[0].is(Proc::State::STOPPED) && state.isJobControl()) {
        job->showInfo();
      }
    }
    static_cast<void>(state.tryToBeForeground());
    state.jobTable.waitForAny();
    state.setExitStatus(status);
    if (errNum != 0) {
      raiseSystemError(state, errNum, "wait failed");
      return false;
    }
    if (forkKind == ForkKind::PIPE_FAIL && state.has(RuntimeOption::ERR_RAISE)) {
      for (unsigned int index = 0; index < entry->getProcSize(); index++) {
        auto &proc = entry->getProcs()[index];
        if (const int s = proc.exitStatus(); s != 0) {
          if (index < entry->getProcSize() - 1 && proc.signaled() && proc.asSigNum() == SIGPIPE) {
            if (!state.has(RuntimeOption::FAIL_SIGPIPE)) {
              continue;
            }
          }
          std::string message = "pipeline has non-zero status: `";
          message += std::to_string(s);
          message += "' at ";
          message += std::to_string(index + 1);
          message += "th element";
          raiseError(state, TYPE::ExecError, std::move(message), s);
          return false;
        }
      }
    }
    ret = exitStatusToBool(status);
    break;
  }
  case ForkKind::IN_PIPE:
  case ForkKind::OUT_PIPE: {
    int &fd = forkKind == ForkKind::IN_PIPE ? pipeSet.in[WRITE_PIPE] : pipeSet.out[READ_PIPE];
    const auto fdObj = newFD(state, fd);
    const auto entry = JobObject::create(
        procSize, procs, false, forkKind == ForkKind::IN_PIPE ? fdObj : state.emptyFDObj,
        forkKind == ForkKind::OUT_PIPE ? fdObj : state.emptyFDObj, std::move(desc));
    state.jobTable.attach(entry, true);
    ret = fdObj;
    break;
  }
  case ForkKind::COPROC:
  case ForkKind::JOB:
  case ForkKind::DISOWN: {
    const bool disown = forkKind == ForkKind::DISOWN;
    const auto entry =
        JobObject::create(procSize, procs, false, newFD(state, pipeSet.in[WRITE_PIPE]),
                          newFD(state, pipeSet.out[READ_PIPE]), std::move(desc));
    state.jobTable.attach(entry, disown);
    ret = Value(entry.get());
    break;
  }
  default:
    break;
  }
  return true;
}

static bool needForeground(ForkKind kind) {
  switch (kind) {
  case ForkKind::NONE:
  case ForkKind::STR:
  case ForkKind::ARRAY:
  case ForkKind::PIPE_FAIL:
    return true;
  case ForkKind::IN_PIPE:
  case ForkKind::OUT_PIPE:
  case ForkKind::COPROC:
  case ForkKind::JOB:
  case ForkKind::DISOWN:
    return false;
  }
  return false; // unreachable. for suppress gcc warning
}

/**
 * for `setpgid(pid, pgid)`
 *
 * @param rootShell
 * @param kind
 * @return
 * if created child process should be process group leader (create job), return 0
 * otherwise, return its parent process group id (default)
 */
static pid_t resolvePGID(bool rootShell, ForkKind kind) {
  if (rootShell) {
    if (kind != ForkKind::STR && kind != ForkKind::ARRAY) {
      /**
       * in root shell, created child process should be process group leader
       * (except for command substitution)
       */
      return 0;
    }
  }
  return getpgid(0);
}

static Proc::Op resolveProcOp(const ARState &st, ForkKind kind) {
  Proc::Op op{};
  if (st.isJobControl()) {
    if (kind != ForkKind::STR && kind != ForkKind::ARRAY) {
      /**
       * in command substitution, always disable JOB_CONTROL
       */
      setFlag(op, Proc::Op::JOB_CONTROL);
    }

    if (needForeground(kind)) {
      setFlag(op, Proc::Op::FOREGROUND);
    }
  }
  return op;
}

bool VM::forkAndEval(ARState &state, Value &&desc) {
  const auto forkKind = static_cast<ForkKind>(read8(state.stack.ip()));
  const unsigned short offset = read16(state.stack.ip() + 1);

  // set in/out pipe
  PipeSet pipeSet(forkKind);
  const pid_t pgid = resolvePGID(state.isRootShell(), forkKind);
  const auto procOp = resolveProcOp(state, forkKind);
  const bool jobCtrl = state.isJobControl();
  auto proc = Proc::fork(state, pgid, procOp);
  if (proc.pid() > 0) { // parent process
    tryToClose(pipeSet.in[READ_PIPE]);
    tryToClose(pipeSet.out[WRITE_PIPE]);

    Value obj;

    switch (forkKind) {
    case ForkKind::STR:
    case ForkKind::ARRAY: { // always disable job control (so not change foreground process group)
      assert(!hasFlag(procOp, Proc::Op::JOB_CONTROL));
      tryToClose(pipeSet.in[WRITE_PIPE]);
      const bool ret = forkKind == ForkKind::STR
                           ? readAsStr(state, pipeSet.out[READ_PIPE], obj)
                           : readAsStrArray(state, pipeSet.out[READ_PIPE], obj);
      if (!ret || ARState::isInterrupted()) {
        /**
         * if read failed, not wait termination (always attach to job table)
         */
        tryToClose(pipeSet.out[READ_PIPE]); // close read pipe after wait, due to prevent EPIPE
        state.jobTable.attach(
            JobObject::create(proc, state.emptyFDObj, state.emptyFDObj, std::move(desc)));

        if (ret && ARState::isInterrupted()) {
          raiseSystemError(state, EINTR, ERROR_CMD_SUB);
        }
        ARState::clearPendingSignal(SIGINT); // always clear SIGINT
        return false;
      }

      const auto waitOp = jobCtrl ? WaitOp::BLOCK_UNTRACED : WaitOp::BLOCKING;
      const int status = proc.wait(waitOp); // wait exit
      const int errNum = errno;
      tryToClose(pipeSet.out[READ_PIPE]); // close read pipe after wait, due to prevent EPIPE
      if (!proc.is(Proc::State::TERMINATED)) {
        state.jobTable.attach(
            JobObject::create(proc, state.emptyFDObj, state.emptyFDObj, std::move(desc)));
      }
      if (status < 0) {
        raiseSystemError(state, errNum, "wait failed");
        return false;
      }
      if (status != 0 && state.has(RuntimeOption::ERR_RAISE)) {
        std::string message = "child process exits with non-zero status: `";
        message += std::to_string(status);
        message += "'";
        raiseError(state, TYPE::ExecError, std::move(message), status);
        return false;
      }
      state.setExitStatus(status);
      break;
    }
    default:
      if (const Proc procs[1] = {proc};
          !attachAsyncJob(state, std::move(desc), 1, procs, forkKind, pipeSet, obj)) {
        return false;
      }
    }

    // push object
    if (obj) {
      state.stack.push(std::move(obj));
    }

    state.stack.ip() += offset - 1;
  } else if (proc.pid() == 0) { // child process
    pipeSet.setupChildStdin(forkKind, jobCtrl);
    pipeSet.setupChildStdout();
    pipeSet.closeAll();

    state.stack.ip() += 3;
  } else {
    raiseSystemError(state, EAGAIN, "fork failed");
    return false;
  }
  return true;
}

/* for pipeline evaluation */
static NativeCode initCode(OpCode op) {
  NativeCode::ArrayType code;
  code[0] = static_cast<char>(op);
  code[1] = static_cast<char>(OpCode::RETURN);
  return NativeCode(code);
}

static ResolvedCmd lookupUdcFromIndex(const ARState &state, const ModId modId,
                                      const unsigned int index, const bool nullChar = false) {
  const FuncObject *udcObj = nullptr;
  if (auto &v = state.getGlobal(index)) {
    udcObj = &typeAs<FuncObject>(v);
  } else {
    return ResolvedCmd::illegalUdc();
  }

  auto &type = state.typePool.get(udcObj->getTypeID());
  if (type.isModType()) { // module object
    return ResolvedCmd::fromMod(cast<ModType>(type), modId, nullChar);
  } else { // udc object
    assert(type.is(TYPE::Command));
    return ResolvedCmd::fromUdc(*udcObj, nullChar);
  }
}

/**
 * lookup user-defined command from module.
 * if modType is null, lookup from root module
 * @param state
 * @param modType
 * @param name
 * @param nullChar
 * @param cmd
 * @return
 */
static bool lookupUdc(const ARState &state, const ModType &modType, const char *name, bool nullChar,
                      ResolvedCmd &cmd) {
  const std::string fullname = toCmdFullName(name);
  if (const auto handle = modType.lookupVisibleSymbolAtModule(state.typePool, fullname)) {
    cmd = lookupUdcFromIndex(state, handle->getModId(), handle->getIndex(), nullChar);
    return true;
  }
  return false;
}

ResolvedCmd CmdResolver::operator()(const ARState &state, const Value &name,
                                    const ModType *modType) const {
  const StringRef ref = name.asStrRef();

  // first, check user-defined command
  if (hasFlag(this->resolveOp, FROM_UDC)) {
    const auto fqn = hasFlag(this->resolveOp, FROM_FQN_UDC) ? ref.find('\0') : StringRef::npos;
    const char *cmdName = ref.data();
    const bool hasNullChar = fqn != StringRef::npos;
    if (hasNullChar) {
      const auto ret = state.typePool.getType(cmdName);
      if (!ret || !ret->isModType() || ref.find('\0', fqn + 1) != StringRef::npos) {
        return ResolvedCmd::invalid();
      }
      modType = cast<ModType>(ret);
      cmdName = ref.begin() + fqn + 1;
    } else if (!modType) {
      modType = getCurRuntimeModule(state);
    }
    if (!modType) {
      modType = state.typePool.getModTypeById(ROOT_MOD_ID);
      assert(modType);
    }
    if (ResolvedCmd cmd{}; lookupUdc(state, *modType, cmdName, hasNullChar, cmd)) {
      return cmd;
    }
    if (hasNullChar) {
      return ResolvedCmd::invalid();
    }
  }

  // second, check builtin command
  if (hasFlag(this->resolveOp, FROM_BUILTIN)) {
    if (const builtin_command_t bcmd = lookupBuiltinCommand(ref); bcmd != nullptr) {
      return ResolvedCmd::fromBuiltin(bcmd);
    }

    static std::pair<const char *, NativeCode> sb[] = {
        {"command", initCode(OpCode::BUILTIN_CMD)},
        {"call", initCode(OpCode::BUILTIN_CALL)},
        {"exec", initCode(OpCode::BUILTIN_EXEC)},
    };
    for (auto &[name, code] : sb) {
      if (ref == name) {
        return ResolvedCmd::fromBuiltin(code);
      }
    }
  }

  // resolve dynamic registered user-defined command
  if (hasFlag(this->resolveOp, FROM_DYNA_UDC)) {
    auto &map = typeAs<OrderedMapObject>(state.getGlobal(BuiltinVarOffset::DYNA_UDCS));
    if (const auto retIndex = map.lookup(name); retIndex != -1) {
      auto *obj = map[retIndex].getValue().get();
      assert(isa<FuncObject>(*obj) || isa<ClosureObject>(*obj));
      return ResolvedCmd::fromCmdObj(obj);
    }
  }

  // resolve external command path
  auto cmd = ResolvedCmd::fromExternal(nullptr);
  if (hasFlag(this->resolveOp, FROM_EXTERNAL)) {
    const char *cmdName = ref.data();
    cmd = ResolvedCmd::fromExternal(state.pathCache.searchPath(cmdName, this->searchOp));

    // if command not found or directory, lookup CMD_FALLBACK
    if (hasFlag(this->resolveOp, FROM_FALLBACK) &&
        (cmd.filePath() == nullptr || S_ISDIR(getStMode(cmd.filePath())))) {
      if (getBuiltinGlobal(state, VAR_CMD_FALLBACK).isObject()) {
        return ResolvedCmd::fallback();
      }
    }
  }
  return cmd;
}

static void raiseCmdError(ARState &state, const char *cmdName, int errNum) {
  std::string str = ERROR_EXEC;
  str += toPrintable(cmdName);
  if (errNum == ENOENT) {
    str += ": command not found";
    raiseError(state, TYPE::SystemError, std::move(str), 127);
  } else if (errNum == EACCES) {
    str += ": ";
    str += strerror(errNum);
    raiseError(state, TYPE::SystemError, std::move(str), 126);
  } else {
    raiseSystemError(state, errNum, std::move(str));
  }
}

static void raiseInvalidCmdError(ARState &state, StringRef ref) {
  std::string message = "command contains null character: `";
  for (const char ch : ref) {
    if (ch == '\0') {
      message += "\\x00";
    } else {
      message += ch;
    }
  }
  message += "'";
  raiseSystemError(state, EINVAL, std::move(message));
}

static Value toCmdDesc(char *const *argv) {
  std::string value;
  for (; *argv != nullptr; argv++) {
    if (!value.empty()) {
      value += " ";
    }
    if (!formatJobDesc(StringRef(*argv), value)) {
      break;
    }
  }
  return Value::createStr(std::move(value));
}

bool VM::forkAndExec(ARState &state, const char *filePath, char *const *argv, Value &&redirConfig) {
  // setup self pipe
  int selfPipe[2];
  if (pipe(selfPipe) < 0) {
    fatal_perror("pipe creation error");
  }
  if (!setCloseOnExec(selfPipe[WRITE_PIPE], true)) {
    fatal_perror("fcntl error");
  }

  const pid_t pgid = resolvePGID(state.isRootShell(), ForkKind::NONE);
  const auto procOp = resolveProcOp(state, ForkKind::NONE);
  auto proc = Proc::fork(state, pgid, procOp);
  if (proc.pid() == -1) {
    raiseCmdError(state, argv[0], EAGAIN);
    return false;
  } else if (proc.pid() == 0) { // child
    close(selfPipe[READ_PIPE]);
    xexecve(filePath, argv, nullptr);

    const int errNum = errno;
    const ssize_t r = write(selfPipe[WRITE_PIPE], &errNum, sizeof(int));
    (void)r; // FIXME:
    exit(-1);
  } else { // parent process
    close(selfPipe[WRITE_PIPE]);
    redirConfig = nullptr; // restore redirConfig

    ssize_t readSize;
    int errNum = 0;
    while ((readSize = read(selfPipe[READ_PIPE], &errNum, sizeof(int))) == -1) {
      if (errno != EAGAIN && errno != EINTR) {
        break;
      }
    }
    close(selfPipe[READ_PIPE]);
    if (readSize > 0 && errNum == ENOENT) { // remove cached path
      state.pathCache.removePath(argv[0]);
    }

    // wait process or job termination
    const auto waitOp = state.isJobControl() ? WaitOp::BLOCK_UNTRACED : WaitOp::BLOCKING;
    const int status = proc.wait(waitOp);
    int errNum2 = errno;
    if (!proc.is(Proc::State::TERMINATED)) {
      const auto job = state.jobTable.attach(
          JobObject::create(proc, state.emptyFDObj, state.emptyFDObj, toCmdDesc(argv)));
      if (proc.is(Proc::State::STOPPED) && state.isJobControl()) {
        job->showInfo();
      }
    }
    const int ret = state.tryToBeForeground();
    LOG(DUMP_EXEC, "tryToBeForeground: %d, %s", ret, strerror(errno));
    state.jobTable.waitForAny();
    if (errNum != 0) {
      errNum2 = errNum;
    }
    if (errNum2 != 0) {
      raiseCmdError(state, argv[0], errNum2);
      return false;
    }
    pushExitStatus(state, status);
    return true;
  }
}

bool VM::prepareSubCommand(ARState &state, const ModType &modType, ObjPtr<ArrayObject> &&argvObj,
                           Value &&redirConfig) {
  auto &array = *argvObj;
  if (array.size() == 1) {
    ERROR(state, array, "require subcommand");
    pushExitStatus(state, 2);
    return true;
  }

  const auto subCmd = array.getValues()[1].asStrRef();
  if (subCmd[0] == '_') {
    ERROR(state, array, "cannot resolve private subcommand: %s", toPrintable(subCmd).c_str());
    pushExitStatus(state, 1);
    return true;
  }

  const std::string key = toCmdFullName(subCmd);
  const auto handle = modType.lookup(state.typePool, key);
  if (!handle) {
    ERROR(state, array, "undefined subcommand: %s", toPrintable(subCmd).c_str());
    pushExitStatus(state, 2);
    return true;
  }
  auto &udc = typeAs<FuncObject>(state.getGlobal(handle->getIndex())).getCode();
  array.takeFirst(); // not check iterator invalidation
  return prepareUserDefinedCommandCall(state, udc, std::move(argvObj), std::move(redirConfig),
                                       CmdCallAttr::SET_VAR);
}

bool VM::callCommand(ARState &state, CmdResolver resolver, ObjPtr<ArrayObject> &&argvObj,
                     Value &&redirConfig, CmdCallAttr attr) {
  const auto cmd = resolver(state, argvObj->getValues()[0]);
  return callCommand(state, cmd, std::move(argvObj), std::move(redirConfig), attr);
}

static void traceCmd(const ARState &state, const ArrayObject &argv) {
  std::string value;
  for (auto &e : argv.getValues()) {
    value += " ";
    value += e.toString();
  }

  state.getCallStack().fillStackTrace([&value](StackTraceElement &&trace) {
    fprintf(stderr, "+ %s:%d>%s\n", trace.getSourceName().c_str(), trace.getLineNum(),
            value.c_str());
    value = "";
    return false; // print only once
  });

  if (!value.empty()) {
    fprintf(stderr, "+%s\n", value.c_str());
  }
}

static bool checkCmdExecError(ARState &state, CmdCallAttr attr, int64_t status) {
  if (status != 0 && hasFlag(attr, CmdCallAttr::RAISE) && state.has(RuntimeOption::ERR_RAISE)) {
    std::string message = "command exits with non-zero status: `";
    message += std::to_string(status);
    message += "'";
    raiseError(state, TYPE::ExecError, std::move(message), status);
    return false;
  }
  return true;
}

bool VM::callCommand(ARState &state, const ResolvedCmd &cmd, ObjPtr<ArrayObject> &&argvObj,
                     Value &&redirConfig, CmdCallAttr attr) {
  auto &array = *argvObj;
  if (cmd.hasNullChar()) { // adjust command name
    StringRef name = array.getValues()[0].asStrRef();
    const auto pos = name.find('\0');
    assert(pos != StringRef::npos);
    name = name.substr(pos + 1);
    array.refValues()[0] = Value::createStr(name); // not check iterator invalidation
  }
  if (state.has(RuntimeOption::XTRACE)) {
    traceCmd(state, array);
  }
  switch (cmd.kind()) {
  case ResolvedCmd::USER_DEFINED:
  case ResolvedCmd::BUILTIN_S:
    if (cmd.kind() == ResolvedCmd::USER_DEFINED) {
      setFlag(attr, CmdCallAttr::SET_VAR);
    }
    return prepareUserDefinedCommandCall(state, cmd.udc(), std::move(argvObj),
                                         std::move(redirConfig), attr);
  case ResolvedCmd::BUILTIN: {
    errno = 0;
    const int status = cmd.builtinCmd()(state, array);
    flushStdFD();
    if (state.hasError()) {
      return false;
    }
    if (!checkCmdExecError(state, attr, status)) {
      return false;
    }
    pushExitStatus(state, status);
    return true;
  }
  case ResolvedCmd::MODULE:
    return prepareSubCommand(state, cmd.modType(), std::move(argvObj), std::move(redirConfig));
  case ResolvedCmd::CMD_OBJ: {
    auto *obj = cmd.cmdObj();
    const DSCode *code = nullptr;
    if (isa<FuncObject>(obj)) {
      code = &cast<FuncObject>(obj)->getCode();
    } else {
      assert(isa<ClosureObject>(obj));
      code = &cast<ClosureObject>(obj)->getFuncObj().getCode();
    }
    setFlag(attr, CmdCallAttr::SET_VAR | CmdCallAttr::CLOSURE);
    state.stack.push(Value(obj));
    return prepareUserDefinedCommandCall(state, *code, std::move(argvObj), std::move(redirConfig),
                                         attr);
  }
  case ResolvedCmd::EXTERNAL: {
    // create argv
    const unsigned int size = array.getValues().size();
    char *argv[size + 1];
    for (unsigned int i = 0; i < size; i++) {
      argv[i] = const_cast<char *>(array.getValues()[i].asCStr());
    }
    argv[size] = nullptr;

    if (hasFlag(attr, CmdCallAttr::NEED_FORK)) {
      const bool ret = forkAndExec(state, cmd.filePath(), argv, std::move(redirConfig));
      if (ret) {
        const int status = state.getMaskedExitStatus();
        if (!checkCmdExecError(state, attr, status)) {
          return false;
        }
      }
      return ret;
    } else {
      xexecve(cmd.filePath(), argv, nullptr);
      raiseCmdError(state, argv[0], errno);
      return false;
    }
  }
  case ResolvedCmd::FALLBACK: {
    const auto *modType = getCurRuntimeModule(state);
    if (!modType) {
      modType = state.typePool.getModTypeById(ROOT_MOD_ID);
    }
    state.stack.reserve(3);
    state.stack.push(getBuiltinGlobal(state, VAR_CMD_FALLBACK));
    state.stack.push(state.getGlobal(modType->getIndex()));
    state.stack.push(argvObj);
    return prepareFuncCall(state, 2);
  }
  case ResolvedCmd::INVALID:
    raiseInvalidCmdError(state, array.getValues()[0].asStrRef());
    return false;
  case ResolvedCmd::ILLEGAL_UDC: {
    std::string value = "attempt to access uninitialized user-defined command: `";
    value += array.getValues()[0].asStrRef();
    value += "'";
    raiseError(state, TYPE::IllegalAccessError, std::move(value));
    return false;
  }
  }
  return true; // normally unreachable, but need to suppress gcc warning.
}

VM::BuiltinCmdResult VM::builtinCommand(ARState &state, ObjPtr<ArrayObject> &&argvObj,
                                        Value &&redir, CmdCallAttr attr) {
  auto &arrayObj = *argvObj;
  bool useDefaultPath = false;

  /**
   * if 0, ignore
   * if 1, show description
   * if 2, show detailed description
   */
  unsigned char showDesc = 0;

  GetOptState optState("pvVh");
  for (int opt; (opt = optState(arrayObj)) != -1;) {
    switch (opt) {
    case 'p':
      useDefaultPath = true;
      break;
    case 'v':
      showDesc = 1;
      break;
    case 'V':
      showDesc = 2;
      break;
    case 'h':
      return BuiltinCmdResult::display(showHelp(arrayObj));
    default:
      const int s = invalidOptionError(state, arrayObj, optState);
      return BuiltinCmdResult::display(s);
    }
  }

  unsigned int index = optState.index;
  const unsigned int argc = arrayObj.getValues().size();
  if (index == argc) { // do nothing
    return BuiltinCmdResult::display(0);
  }

  if (showDesc == 0) { // execute command
    if (arrayObj.getValues()[1].asStrRef().hasNullChar()) {
      const auto name = toPrintable(arrayObj.getValues()[1].asStrRef());
      ERROR(state, arrayObj, "contains null characters: %s", name.c_str());
      return BuiltinCmdResult::display(1);
    }

    auto &values = arrayObj.refValues();
    values.erase(values.begin(), values.begin() + index); // not check iterator invalidation

    const auto resolve = CmdResolver(
        CmdResolver::NO_UDC, useDefaultPath ? FilePathCache::USE_DEFAULT_PATH : FilePathCache::NON);
    const bool r = callCommand(state, resolve, std::move(argvObj), std::move(redir), attr);
    return BuiltinCmdResult::call(r);
  }

  // show command description
  unsigned int successCount = 0;
  int errNum = 0;
  for (; index < argc; index++) {
    const auto &cmdName = arrayObj.getValues()[index];
    const auto ref = cmdName.asStrRef();
    auto cmd = CmdResolver(CmdResolver::NO_FALLBACK | CmdResolver::FROM_FQN_UDC,
                           FilePathCache::DIRECT_SEARCH)(state, cmdName);
    switch (cmd.kind()) {
    case ResolvedCmd::USER_DEFINED:
    case ResolvedCmd::MODULE: {
      successCount++;
      if (printf("%s%s\n", toPrintable(ref).c_str(),
                 showDesc == 2 ? " is a user-defined command" : "") < 0) {
        errNum = errno;
        goto END;
      }
      continue;
    }
    case ResolvedCmd::BUILTIN_S:
    case ResolvedCmd::BUILTIN: {
      successCount++;
      if (printf("%s%s\n", ref.data(), showDesc == 2 ? " is a shell builtin command" : "") < 0) {
        errNum = errno;
        goto END;
      }
      continue;
    }
    case ResolvedCmd::CMD_OBJ: {
      successCount++;
      if (printf("%s%s\n", toPrintable(ref).c_str(),
                 showDesc == 2 ? " is a dynamic registered command" : "") < 0) {
        errNum = errno;
        goto END;
      }
      continue;
    }
    case ResolvedCmd::EXTERNAL: {
      const char *path = cmd.filePath();
      if (path != nullptr && isExecutable(path)) {
        successCount++;
        const char *commandName = ref.data();
        int r;
        if (showDesc == 1) {
          r = printf("%s\n", path);
        } else if (state.pathCache.isCached(commandName)) {
          r = printf("%s is hashed (%s)\n", commandName, path);
        } else {
          r = printf("%s is %s\n", commandName, path);
        }
        if (r < 0) {
          errNum = errno;
          goto END;
        }
        continue;
      }
      break;
    }
    case ResolvedCmd::FALLBACK:
    case ResolvedCmd::INVALID:
    case ResolvedCmd::ILLEGAL_UDC:
      break;
    }

    if (showDesc == 2) {
      if (cmd.kind() == ResolvedCmd::ILLEGAL_UDC) {
        ERROR(state, arrayObj, "%s: uninitialized", toPrintable(ref).c_str());
      } else {
        ERROR(state, arrayObj, "%s: not found", toPrintable(ref).c_str());
      }
    }
  }

END:
  int status;
  errno = errNum;
  if (errno != 0 || fflush(stdout) == EOF) {
    PERROR(state, arrayObj, "io error");
    status = 1;
  } else {
    status = successCount > 0 ? 0 : 1;
  }
  return BuiltinCmdResult::display(status);
}

int VM::builtinExec(ARState &state, const ArrayObject &argvObj, Value &&redir) {
  bool clearEnv = false;
  StringRef progName;
  GetOptState optState("hca:");

  if (redir) {
    typeAs<RedirObject>(redir).ignoreBackup();
  }

  for (int opt; (opt = optState(argvObj)) != -1;) {
    switch (opt) {
    case 'c':
      clearEnv = true;
      break;
    case 'a':
      progName = optState.optArg;
      break;
    case 'h':
      return showHelp(argvObj);
    default:
      return invalidOptionError(state, argvObj, optState);
    }
  }

  const unsigned int index = optState.index;
  const unsigned int argc = argvObj.getValues().size();
  if (index < argc) { // exec
    if (argvObj.getValues()[index].asStrRef().hasNullChar()) {
      const auto name = toPrintable(argvObj.getValues()[index].asStrRef());
      ERROR(state, argvObj, "contains null characters: %s", name.c_str());
      return 1;
    }

    char *argv2[argc - index + 1];
    for (unsigned int i = index; i < argc; i++) {
      argv2[i - index] = const_cast<char *>(argvObj.getValues()[i].asCStr());
    }
    argv2[argc - index] = nullptr;

    const char *filePath = state.pathCache.searchPath(argv2[0], FilePathCache::DIRECT_SEARCH);
    if (progName.data() != nullptr) {
      argv2[0] = const_cast<char *>(progName.data());
    }

    char *envp[] = {nullptr};
    xexecve(filePath, argv2, clearEnv ? envp : nullptr);
    PERROR(state, argvObj, "%s", argvObj.getValues()[index].asCStr());
    exit(1);
  }
  return 0;
}

bool VM::returnFromUserDefinedCommand(ARState &state, int64_t status) {
  const auto attr = static_cast<CmdCallAttr>(state.stack.getLocal(UDC_PARAM_ATTR).asNum());
  state.stack.unwind();
  if (!checkCmdExecError(state, attr, status)) {
    return false;
  }
  pushExitStatus(state, status);
  assert(!state.stack.checkVMReturn());
  return true;
}

bool VM::callPipeline(ARState &state, Value &&desc, bool lastPipe, ForkKind forkKind) {
  /**
   * ls | grep .
   * ==> pipeSize == 1, procSize == 2
   *
   * if lastPipe is true,
   *
   * ls | { grep . ;}
   * ==> pipeSize == 1, procSize == 1
   */
  const unsigned int procSize = read8(state.stack.ip()) - 1;
  const unsigned int pipeSize = procSize - (lastPipe ? 0 : 1);

  assert(pipeSize > 0);

  PipeSet pipeSet(forkKind);
  int pipeFds[pipeSize][2];
  initAllPipe(pipeSize, pipeFds);

  // fork
  Proc children[procSize];
  const auto procOp = resolveProcOp(state, forkKind);
  auto procOpRemain = procOp;
  unsetFlag(procOpRemain, Proc::Op::FOREGROUND); // remain process already foreground
  pid_t pgid = resolvePGID(state.isRootShell(), forkKind);
  const bool jobCtrl = state.isJobControl();
  Proc proc; // NOLINT

  uintptr_t procIndex;
  for (procIndex = 0;
       procIndex < procSize &&
       (proc = Proc::fork(state, pgid, procIndex == 0 ? procOp : procOpRemain)).pid() > 0;
       procIndex++) {
    children[procIndex] = proc;
    if (pgid == 0) {
      pgid = proc.pid();
    }
  }

  /**
   * code layout
   *
   * +----------+-------------+------------------+    +--------------------+
   * | PIPELINE | size: 1byte | br1(child): 2yte | ~  | brN(parent): 2byte |
   * +----------+-------------+------------------+    +--------------------+
   */
  if (proc.pid() == 0) {  // child
    if (procIndex == 0) { // first process
      ::dup2(pipeFds[procIndex][WRITE_PIPE], STDOUT_FILENO);
      pipeSet.setupChildStdin(forkKind, jobCtrl);
    }
    if (procIndex > 0 && procIndex < pipeSize) { // other process.
      ::dup2(pipeFds[procIndex - 1][READ_PIPE], STDIN_FILENO);
      ::dup2(pipeFds[procIndex][WRITE_PIPE], STDOUT_FILENO);
    }
    if (procIndex == pipeSize && !lastPipe) { // last process
      ::dup2(pipeFds[procIndex - 1][READ_PIPE], STDIN_FILENO);
      pipeSet.setupChildStdout();
    }
    pipeSet.closeAll(); // FIXME: check error and force exit (not propagate error due to uncaught)
    closeAllPipe(pipeSize, pipeFds);

    // set pc to next instruction
    state.stack.ip() += read16(state.stack.ip() + 1 + procIndex * 2) - 1;
  } else if (procIndex == procSize) { // parent (last pipeline)
    if (lastPipe) {
      /**
       * in last pipe, save current stdin before call dup2
       */
      auto jobEntry = JobObject::create(procSize, children, true, state.emptyFDObj,
                                        state.emptyFDObj, std::move(desc));
      state.jobTable.attach(jobEntry);
      dup2(pipeFds[procIndex - 1][READ_PIPE], STDIN_FILENO);
      closeAllPipe(pipeSize, pipeFds);
      state.stack.push(Value::create<PipelineObject>(state, std::move(jobEntry)));
    } else {
      tryToClose(pipeSet.in[READ_PIPE]);
      tryToClose(pipeSet.out[WRITE_PIPE]);
      closeAllPipe(pipeSize, pipeFds);
      Value obj;
      if (!attachAsyncJob(state, std::move(desc), procSize, children, forkKind, pipeSet, obj)) {
        return false;
      }
      if (obj) {
        state.stack.push(std::move(obj));
      }
    }

    // set pc to next instruction
    state.stack.ip() += read16(state.stack.ip() + 1 + procIndex * 2) - 1;
  } else {
    // force terminate forked process.
    for (unsigned int i = 0; i < procIndex; i++) {
      static_cast<void>(children[i].send(SIGKILL));
    }

    raiseSystemError(state, EAGAIN, "fork failed");
    return false;
  }
  return true;
}

static NativeCode initSignalTrampoline() noexcept {
  NativeCode::ArrayType code;
  code[0] = static_cast<char>(OpCode::LOAD_LOCAL);
  code[1] = 1;
  code[2] = static_cast<char>(OpCode::LOAD_LOCAL);
  code[3] = 2;
  code[4] = static_cast<char>(OpCode::CALL_FUNC);
  code[5] = 1;
  code[6] = static_cast<char>(OpCode::RETURN_SIG);
  return NativeCode(code);
}

static auto signalTrampoline = initSignalTrampoline();

bool VM::kickSignalHandler(ARState &state, int sigNum, Value &&func) {
  state.stack.reserve(3);
  state.stack.push(state.getGlobal(BuiltinVarOffset::EXIT_STATUS));
  state.stack.push(std::move(func));
  state.stack.push(Value::createSig(sigNum));

  return windStackFrame(state, 3, 3, signalTrampoline);
}

void VM::kickVMHook(ARState &state) {
  assert(state.hook);
  const auto op = static_cast<OpCode>(*state.stack.ip());
  state.hook->vmFetchHook(state, op);
}

const native_func_t *nativeFuncPtrTable();

#define vmdispatch(V) switch (V)

#if 0
#define vmcase(code)                                                                               \
  case OpCode::code: {                                                                             \
    fprintf(stderr, "pc: %u, code: %s\n", state.pc(), #code);                                      \
  }
#else
#define vmcase(code) case OpCode::code:
#endif

#define vmnext continue
#define vmerror goto EXCEPT

#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (unlikely(!(E))) {                                                                          \
      vmerror;                                                                                     \
    }                                                                                              \
  } while (false)

#define CHECK_SIGNAL()                                                                             \
  do {                                                                                             \
    if (unlikely(ARState::hasSignals())) {                                                         \
      goto SIGNAL;                                                                                 \
    }                                                                                              \
  } while (false)

bool VM::mainLoop(ARState &state) {
  bool hook = state.getVMHook() != nullptr;
  OpCode op;

  CHECK_SIGNAL();

  while (true) {
    if (unlikely(hook)) {
      kickVMHook(state);
    }

    // fetch next opcode
    op = static_cast<OpCode>(*(state.stack.ip()++));

    // dispatch instruction
    vmdispatch(op) {
      vmcase(HALT) { return true; }
      vmcase(ASSERT_ENABLED) {
        unsigned short offset = read16(state.stack.ip());
        if (state.has(RuntimeOption::ASSERT)) {
          state.stack.ip() += 2;
        } else {
          state.stack.ip() += offset - 1;
        }
        vmnext;
      }
      vmcase(ASSERT_FAIL) {
        auto msg = state.stack.pop();
        auto ref = msg.asStrRef();
        raiseError(state, TYPE::AssertFail_, ref.toString());
        vmerror;
      }
      vmcase(PRINT) {
        unsigned int v = consume24(state.stack.ip());
        auto &stackTopType = state.typePool.get(v);
        assert(!stackTopType.isVoidType());
        auto ref = state.stack.peek().asStrRef();
        std::string value = ": ";
        value += stackTopType.getNameRef();
        value += " = ";
        value += ref;
        value += "\n";
        fwrite(value.c_str(), sizeof(char), value.size(), stdout);
        fflush(stdout);
        state.stack.popNoReturn();
        vmnext;
      }
      vmcase(INSTANCE_OF) {
        unsigned int v = consume24(state.stack.ip());
        auto &targetType = state.typePool.get(v);
        auto value = state.stack.pop();
        bool ret = instanceOf(state.typePool, value, targetType);
        state.stack.push(Value::createBool(ret));
        vmnext;
      }
      vmcase(CHECK_CAST) {
        unsigned int v = consume24(state.stack.ip());
        TRY(checkCast(state, state.typePool.get(v)));
        vmnext;
      }
      vmcase(CHECK_CAST_OPT) {
        unsigned int v = consume24(state.stack.ip());
        const auto &targetType = state.typePool.get(v);
        if (!instanceOf(state.typePool, state.stack.peek(), targetType)) {
          state.stack.popNoReturn();
          state.stack.push(Value::createInvalid());
        }
        vmnext;
      }
      vmcase(PUSH_NULL) {
        state.stack.push(nullptr);
        vmnext;
      }
      vmcase(PUSH_TRUE) {
        state.stack.push(Value::createBool(true));
        vmnext;
      }
      vmcase(PUSH_FALSE) {
        state.stack.push(Value::createBool(false));
        vmnext;
      }
      vmcase(PUSH_SIG) {
        unsigned int value = consume8(state.stack.ip());
        state.stack.push(Value::createSig(static_cast<int>(value)));
        vmnext;
      }
      vmcase(PUSH_INT) {
        unsigned int value = consume8(state.stack.ip());
        state.stack.push(Value::createInt(value));
        vmnext;
      }
      vmcase(PUSH_STR0) {
        state.stack.push(Value::createStr());
        vmnext;
      }
      vmcase(PUSH_STR1) vmcase(PUSH_STR2) vmcase(PUSH_STR3) {
        char data[3];
        unsigned int size = op == OpCode::PUSH_STR1 ? 1 : op == OpCode::PUSH_STR2 ? 2 : 3;
        for (unsigned int i = 0; i < size; i++) {
          data[i] = static_cast<char>(consume8(state.stack.ip()));
        }
        state.stack.push(Value::createStr(StringRef(data, size)));
        vmnext;
      }
      vmcase(PUSH_META) {
        unsigned int meta = consume8(state.stack.ip());
        unsigned int v = consume8(state.stack.ip());
        state.stack.push(Value::createExpandMeta(static_cast<ExpandMeta>(meta), v));
        vmnext;
      }
      vmcase(PUSH_INVALID) {
        state.stack.push(Value::createInvalid());
        vmnext;
      }
      vmcase(LOAD_CONST) {
        unsigned char index = consume8(state.stack.ip());
        state.stack.push(CONST_POOL(state)[index]);
        vmnext;
      }
      vmcase(LOAD_CONST_W) {
        unsigned short index = consume16(state.stack.ip());
        state.stack.push(CONST_POOL(state)[index]);
        vmnext;
      }
      vmcase(LOAD_CONST_T) {
        unsigned int index = consume24(state.stack.ip());
        state.stack.push(CONST_POOL(state)[index]);
        vmnext;
      }
      vmcase(LOAD_GLOBAL) {
        unsigned short index = consume16(state.stack.ip());
        auto v = state.getGlobal(index);
        if (unlikely(!v)) { // normally unreachable
          raiseError(state, TYPE::IllegalAccessError,
                     "attempt to access uninitialized global variable");
          vmerror;
        }
        state.stack.push(std::move(v));
        vmnext;
      }
      vmcase(STORE_GLOBAL) {
        unsigned short index = consume16(state.stack.ip());
        state.setGlobal(index, state.stack.pop());
        vmnext;
      }
      vmcase(LOAD_LOCAL) {
        unsigned char index = consume8(state.stack.ip());
        state.stack.loadLocal(index);
        vmnext;
      }
      vmcase(STORE_LOCAL) {
        unsigned char index = consume8(state.stack.ip());
        state.stack.storeLocal(index);
        vmnext;
      }
      vmcase(LOAD_FIELD) {
        unsigned short index = consume16(state.stack.ip());
        state.stack.loadField(index);
        vmnext;
      }
      vmcase(STORE_FIELD) {
        unsigned short index = consume16(state.stack.ip());
        state.stack.storeField(index);
        vmnext;
      }
      vmcase(IMPORT_ENV) {
        unsigned char b = consume8(state.stack.ip());
        TRY(loadEnv(state, b > 0));
        vmnext;
      }
      vmcase(LOAD_ENV) {
        const char *value = loadEnv(state, false);
        TRY(value);
        state.stack.push(Value::createStr(value));
        vmnext;
      }
      vmcase(STORE_ENV) {
        TRY(storeEnv(state));
        vmnext;
      }
      vmcase(NEW_ENV_CTX) {
        state.stack.push(Value::create<EnvCtxObject>(state));
        vmnext;
      }
      vmcase(ADD2ENV_CTX) {
        auto value = state.stack.pop();
        auto name = state.stack.pop();
        typeAs<EnvCtxObject>(state.stack.peek()).setAndSaveEnv(std::move(name), std::move(value));
        vmnext;
      }
      vmcase(NEW_TIMER) {
        state.stack.push(Value::create<TimerObject>());
        vmnext;
      }
      vmcase(BOX_LOCAL) {
        unsigned char index = consume8(state.stack.ip());
        auto v = state.stack.getLocal(index);
        auto boxed = Value::create<BoxObject>(std::move(v));
        state.stack.setLocal(index, std::move(boxed));
        vmnext;
      }
      vmcase(LOAD_BOXED) {
        unsigned char index = consume8(state.stack.ip());
        auto &boxed = state.stack.getLocal(index);
        state.stack.push(typeAs<BoxObject>(boxed).getValue());
        vmnext;
      }
      vmcase(STORE_BOXED) {
        unsigned char index = consume8(state.stack.ip());
        auto &boxed = state.stack.getLocal(index);
        auto v = state.stack.pop();
        typeAs<BoxObject>(boxed).setValue(std::move(v));
        vmnext;
      }
      vmcase(NEW_CLOSURE) {
        const unsigned int paramSize = consume8(state.stack.ip());
        auto funcObj = toObjPtr<FuncObject>(state.stack.peekByOffset(paramSize));
        const Value *values = &state.stack.peekByOffset(paramSize) + 1;
        auto value = Value::create<ClosureObject>(std::move(funcObj), paramSize, values);
        for (unsigned int i = 0; i <= paramSize; i++) {
          state.stack.popNoReturn();
        }
        state.stack.push(std::move(value));
        vmnext;
      }
      vmcase(LOAD_UPVAR) vmcase(LOAD_RAW_UPVAR) {
        unsigned char index = consume8(state.stack.ip());
        auto &closure = state.stack.getCurrentClosure();
        auto slot = closure[index];
        if (op == OpCode::LOAD_UPVAR && slot.isObject() && isa<BoxObject>(slot.get())) {
          slot = typeAs<BoxObject>(slot).getValue(); // unbox
        }
        state.stack.push(std::move(slot));
        vmnext;
      }
      vmcase(STORE_UPVAR) {
        unsigned char index = consume8(state.stack.ip());
        auto &closure = state.stack.getCurrentClosure();
        auto v = state.stack.pop();
        auto &slot = closure[index];
        assert(slot.isObject() && isa<BoxObject>(slot.get()));
        typeAs<BoxObject>(slot).setValue(std::move(v)); // box
        vmnext;
      }
      vmcase(POP) {
        state.stack.popNoReturn();
        vmnext;
      }
      vmcase(DUP) {
        state.stack.dup();
        vmnext;
      }
      vmcase(DUP2) {
        state.stack.dup2();
        vmnext;
      }
      vmcase(SWAP) {
        state.stack.swap();
        vmnext;
      }
      vmcase(STORE_BY_OFFSET) {
        const unsigned int offset = consume8(state.stack.ip());
        auto v = state.stack.pop();
        state.stack.storeByOffset(offset, std::move(v));
        vmnext;
      }
      vmcase(CONCAT) vmcase(APPEND) {
        const bool selfConcat = op == OpCode::APPEND;
        auto right = state.stack.pop();
        auto left = state.stack.pop();
        TRY(concatAsStr(state, left, right, selfConcat));
        state.stack.push(std::move(left));
        vmnext;
      }
      vmcase(APPEND_ARRAY) {
        Value v = state.stack.pop();
        TRY(typeAs<ArrayObject>(state.stack.peek()).append(state, std::move(v)));
        vmnext;
      }
      vmcase(APPEND_MAP) {
        Value value = state.stack.pop();
        Value key = state.stack.pop();
        auto &map = typeAs<OrderedMapObject>(state.stack.peek());
        TRY(map.put(state, std::move(key), std::move(value)));
        vmnext;
      }
      vmcase(ITER_HAS_NEXT) {
        unsigned short offset = read16(state.stack.ip());
        if (state.stack.peek()) { // not empty
          state.stack.ip() += 2;
        } else {
          state.stack.clearOperandsUntilGuard(StackGuardType::LOOP);
          state.stack.ip() += offset - 1;
        }
        CHECK_SIGNAL();
        vmnext;
      }
      vmcase(MAP_ITER_NEXT) {
        unsigned short offset = read16(state.stack.ip());
        auto mapIter = state.stack.pop();
        if (typeAs<OrderedMapIterObject>(mapIter).hasNext()) {
          auto &e = typeAs<OrderedMapIterObject>(mapIter).nextEntry();
          state.stack.push(e.getValue());
          state.stack.push(e.getKey());
          state.stack.ip() += 2;
        } else {
          state.stack.clearOperandsUntilGuard(StackGuardType::LOOP);
          state.stack.ip() += offset - 1;
        }
        CHECK_SIGNAL();
        vmnext;
      }
      vmcase(NEW) {
        unsigned int v = consume24(state.stack.ip());
        auto &type = state.typePool.get(v);
        pushNewObject(state, type);
        vmnext;
      }
      vmcase(INIT_FIELDS) {
        unsigned int offset = consume8(state.stack.ip());
        unsigned int size = consume8(state.stack.ip());

        auto &obj = typeAs<BaseObject>(state.stack.peek());
        assert(obj.getFieldSize() == size);
        for (unsigned int i = 0; i < size; i++) {
          obj[i] = state.stack.getLocal(offset + i);
        }
        vmnext;
      }
      vmcase(CALL_FUNC) {
        unsigned int paramSize = consume8(state.stack.ip());
        TRY(prepareFuncCall(state, paramSize));
        vmnext;
      }
      vmcase(CALL_METHOD) {
        unsigned int paramSize = consume8(state.stack.ip());
        unsigned short index = consume16(state.stack.ip());
        TRY(prepareMethodCall(state, index, paramSize));
        vmnext;
      }
      vmcase(CALL_BUILTIN) {
        unsigned int paramSize = consume8(state.stack.ip());
        unsigned int index = consume8(state.stack.ip());
        auto old = state.stack.nativeWind(paramSize);
        auto ret = nativeFuncPtrTable()[index](state);
        state.stack.nativeUnwind(old);
        TRY(!state.hasError());
        state.stack.push(std::move(ret));
        CHECK_SIGNAL();
        vmnext;
      }
      vmcase(RETURN) {
        Value v = state.stack.pop();
        state.stack.unwind();
        state.stack.push(std::move(v));
        if (state.stack.checkVMReturn()) {
          return true;
        }
        CHECK_SIGNAL();
        vmnext;
      }
      vmcase(RETURN_UDC) {
        auto status = state.stack.pop().asInt();
        TRY(returnFromUserDefinedCommand(state, status));
        CHECK_SIGNAL();
        vmnext;
      }
      vmcase(RETURN_SIG) {
        auto v = state.stack.getLocal(0); // old exit status
        state.canHandleSignal = true;
        state.setGlobal(BuiltinVarOffset::EXIT_STATUS, std::move(v));
        state.stack.unwind();
        assert(!state.stack.checkVMReturn());
        CHECK_SIGNAL();
        vmnext;
      }
      vmcase(BRANCH) {
        unsigned short offset = read16(state.stack.ip());
        if (state.stack.pop().asBool()) {
          state.stack.ip() += 2;
        } else {
          state.stack.ip() += offset - 1;
        }
        CHECK_SIGNAL();
        vmnext;
      }
      vmcase(BRANCH_NOT) {
        unsigned short offset = read16(state.stack.ip());
        if (!state.stack.pop().asBool()) {
          state.stack.ip() += 2;
        } else {
          state.stack.ip() += offset - 1;
        }
        CHECK_SIGNAL();
        vmnext;
      }
      vmcase(IF_INVALID) {
        unsigned short offset = read16(state.stack.ip());
        if (state.stack.peek().isInvalid()) {
          state.stack.popNoReturn();
          state.stack.ip() += offset - 1;
        } else {
          state.stack.ip() += 2;
        }
        CHECK_SIGNAL();
        vmnext;
      }
      vmcase(IF_NOT_INVALID) {
        unsigned short offset = read16(state.stack.ip());
        if (state.stack.peek().isInvalid()) {
          state.stack.popNoReturn();
          state.stack.ip() += 2;
        } else {
          state.stack.ip() += offset - 1;
        }
        CHECK_SIGNAL();
        vmnext;
      }
      vmcase(GOTO) {
        unsigned int index = read32(state.stack.ip());
        state.stack.updateIPByOffset(index);
        CHECK_SIGNAL();
        vmnext;
      }
      vmcase(JUMP_LOOP) {
        unsigned int index = read32(state.stack.ip());
        state.stack.updateIPByOffset(index);
        state.stack.clearOperandsUntilGuard(StackGuardType::LOOP);
        CHECK_SIGNAL();
        vmnext;
      }
      vmcase(JUMP_LOOP_V) {
        unsigned int index = read32(state.stack.ip());
        state.stack.updateIPByOffset(index);
        auto v = state.stack.pop();
        state.stack.clearOperandsUntilGuard(StackGuardType::LOOP);
        state.stack.push(std::move(v));
        CHECK_SIGNAL();
        vmnext;
      }
      vmcase(LOOP_GUARD) {
        state.stack.push(Value::createStackGuard(StackGuardType::LOOP));
        vmnext;
      }
      vmcase(JUMP_TRY) {
        unsigned int index = read32(state.stack.ip());
        state.stack.updateIPByOffset(index);
        state.stack.clearOperandsUntilGuard(StackGuardType::TRY);
        CHECK_SIGNAL();
        vmnext;
      }
      vmcase(JUMP_TRY_V) {
        unsigned int index = read32(state.stack.ip());
        state.stack.updateIPByOffset(index);
        auto v = state.stack.pop();
        state.stack.clearOperandsUntilGuard(StackGuardType::TRY);
        state.stack.push(std::move(v));
        CHECK_SIGNAL();
        vmnext;
      }
      vmcase(TRY_GUARD) {
        unsigned int level = consume32(state.stack.ip());
        state.stack.push(Value::createStackGuard(StackGuardType::TRY, level));
        vmnext;
      }
      vmcase(TRY_GUARD0) {
        state.stack.push(Value::createStackGuard(StackGuardType::TRY, 1));
        vmnext;
      }
      vmcase(TRY_GUARD1) {
        unsigned int level = consume8(state.stack.ip());
        state.stack.push(Value::createStackGuard(StackGuardType::TRY, level));
        vmnext;
      }
      vmcase(THROW) {
        auto obj = state.stack.pop();
        state.throwObject(toObjPtr<ErrorObject>(obj));
        vmerror;
      }
      vmcase(ENTER_FINALLY) {
        unsigned int index = read32(state.stack.ip());
        const unsigned int savedIndex = state.stack.getFrame().getIPOffset() + 4;
        state.stack.updateIPByOffset(index);
        state.stack.enterFinally(index, savedIndex);
        vmnext;
      }
      vmcase(EXIT_FINALLY) {
        auto entry = state.stack.exitFinally();
        if (entry.hasError()) {
          state.stack.setErrorObj(entry.asError());
          vmerror;
        }
        state.stack.updateIPByOffset(entry.asRetAddr());
        vmnext;
      }
      vmcase(LOOKUP_HASH) {
        auto key = state.stack.pop();
        auto map = state.stack.pop();
        if (!key.isInvalid()) {
          auto &mapObj = typeAs<OrderedMapObject>(map);
          if (auto retIndex = mapObj.lookup(key); retIndex != -1) {
            auto &v = mapObj[retIndex].getValue();
            assert(v.kind() == ValueKind::NUMBER);
            unsigned int index = v.asNum();
            state.stack.updateIPByOffset(index);
          }
        }
        vmnext;
      }
      vmcase(REF_EQ) {
        auto v1 = state.stack.pop();
        auto v2 = state.stack.pop();
        state.stack.push(Value::createBool(v1 == v2));
        vmnext;
      }
      vmcase(REF_NE) {
        auto v1 = state.stack.pop();
        auto v2 = state.stack.pop();
        state.stack.push(Value::createBool(v1 != v2));
        vmnext;
      }
      vmcase(FORK) {
        auto desc = state.stack.pop();
        TRY(forkAndEval(state, std::move(desc)));
        vmnext;
      }
      vmcase(PIPELINE) vmcase(PIPELINE_SILENT) vmcase(PIPELINE_LP) vmcase(PIPELINE_ASYNC) {
        bool lastPipe = op == OpCode::PIPELINE_LP;
        auto kind = ForkKind::PIPE_FAIL;
        if (op == OpCode::PIPELINE_SILENT) {
          kind = ForkKind::NONE;
        } else if (op == OpCode::PIPELINE_ASYNC) {
          unsigned char v = consume8(state.stack.ip());
          kind = static_cast<ForkKind>(v);
        }
        auto desc = state.stack.pop();
        TRY(callPipeline(state, std::move(desc), lastPipe, kind));
        vmnext;
      }
      vmcase(EXPAND_TILDE) {
        const auto value = state.stack.pop();
        if (!applyTildeExpansion(state, value.asStrRef())) {
          vmerror;
        }
        vmnext;
      }
      vmcase(PARSE_CLI) {
        auto value = state.stack.pop();
        auto &obj = typeAs<BaseObject>(value);
        auto &args = typeAs<ArrayObject>(state.stack.getLocal(UDC_PARAM_ARGV));
        if (!parseCommandLine(state, args, obj)) {
          auto error = state.stack.takeThrownObject();
          showCommandLineUsage(*error);
          TRY(returnFromUserDefinedCommand(state, error->getStatus()));
          CHECK_SIGNAL();
        }
        vmnext;
      }
      vmcase(NEW_CMD) {
        auto v = state.stack.pop();
        auto obj = Value::create<ArrayObject>(state.typePool.get(TYPE::StringArray));
        auto &argv = typeAs<ArrayObject>(obj);
        argv.append(std::move(v)); // not check iterator invalidation
        state.stack.push(std::move(obj));
        vmnext;
      }
      vmcase(ADD_CMD_ARG) {
        /**
         * stack layout
         *
         * ===========> stack grow
         * +------+-------+-------+
         * | argv | redir | value |
         * +------+-------+-------+
         */
        auto arg = state.stack.pop();
        auto redir = state.stack.pop();
        CmdArgsBuilder builder(state, toObjPtr<ArrayObject>(state.stack.peek()), std::move(redir));
        TRY(builder.add(std::move(arg)));
        state.stack.push(std::move(builder).takeRedir());
        vmnext;
      }
      vmcase(ADD_EXPANDING) {
        unsigned int size = consume8(state.stack.ip());
        auto opt = static_cast<ExpandOp>(consume8(state.stack.ip()));
        TRY(addExpandingPath(state, size, opt));
        CHECK_SIGNAL();
        vmnext;
      }
      vmcase(CALL_CMD) vmcase(CALL_CMD_NOFORK) vmcase(CALL_CMD_SILENT) {
        CmdCallAttr attr = CmdCallAttr::RAISE | CmdCallAttr::NEED_FORK;
        if (op == OpCode::CALL_CMD_NOFORK) {
          unsetFlag(attr, CmdCallAttr::NEED_FORK);
        } else if (op == OpCode::CALL_CMD_SILENT) {
          unsetFlag(attr, CmdCallAttr::RAISE);
        }

        auto redir = state.stack.pop();
        auto argv = toObjPtr<ArrayObject>(state.stack.pop());

        TRY(callCommand(state, CmdResolver(CmdResolver::NO_STATIC_UDC, FilePathCache::NON),
                        std::move(argv), std::move(redir), attr));
        CHECK_SIGNAL();
        vmnext;
      }
      vmcase(CALL_UDC) vmcase(CALL_UDC_SILENT) {
        unsigned short index = consume16(state.stack.ip());
        CmdCallAttr attr = CmdCallAttr::RAISE | CmdCallAttr::NEED_FORK;
        if (op == OpCode::CALL_UDC_SILENT) {
          unsetFlag(attr, CmdCallAttr::RAISE);
        }

        auto redir = state.stack.pop();
        auto argv = toObjPtr<ArrayObject>(state.stack.pop());

        ResolvedCmd cmd = lookupUdcFromIndex(state, BUILTIN_MOD_ID, index);
        TRY(callCommand(state, cmd, std::move(argv), std::move(redir), attr));
        CHECK_SIGNAL();
        vmnext;
      }
      vmcase(CALL_CMD_COMMON) {
        auto redir = state.stack.pop();
        auto argv = toObjPtr<ArrayObject>(state.stack.pop());

        TRY(callCommand(state, CmdResolver(), std::move(argv), std::move(redir),
                        CmdCallAttr::NEED_FORK));
        CHECK_SIGNAL();
        vmnext;
      }
      vmcase(CALL_CMD_OBJ) {
        auto redir = state.stack.pop();
        auto argv = toObjPtr<ArrayObject>(state.stack.pop());
        if (argv.get()->getRefcount() > 1) {
          argv = argv->copy();
        }
        auto obj = state.stack.pop();
        auto cmd = ResolvedCmd::fromCmdObj(obj.get());
        if (argv->size() == 0) {
          // add dummy
          argv->refValues().push_back(Value::createStr()); // not check iterator invalidation
        }
        TRY(callCommand(state, cmd, std::move(argv), std::move(redir), CmdCallAttr{}));
        CHECK_SIGNAL();
        vmnext;
      }
      vmcase(BUILTIN_CMD) {
        auto v = state.stack.getLocal(UDC_PARAM_ATTR).asNum();
        auto attr = static_cast<CmdCallAttr>(v);
        Value redir = state.stack.getLocal(UDC_PARAM_REDIR);
        auto argv = toObjPtr<ArrayObject>(state.stack.getLocal(UDC_PARAM_ARGV));
        const auto ret = builtinCommand(state, std::move(argv), std::move(redir), attr);
        flushStdFD();
        if (ret.kind == BuiltinCmdResult::CALL) {
          TRY(ret.r);
        } else {
          TRY(checkCmdExecError(state, attr, ret.status));
          pushExitStatus(state, ret.status);
        }
        vmnext;
      }
      vmcase(BUILTIN_CALL) {
        auto v = state.stack.getLocal(UDC_PARAM_ATTR).asNum();
        auto attr = static_cast<CmdCallAttr>(v);
        Value redir = state.stack.getLocal(UDC_PARAM_REDIR);
        auto argv = toObjPtr<ArrayObject>(state.stack.getLocal(UDC_PARAM_ARGV));

        argv->takeFirst(); // not check iterator invalidation
        if (!argv->getValues().empty()) {
          TRY(callCommand(state,
                          CmdResolver(CmdResolver::FROM_DEFAULT_WITH_FQN, FilePathCache::NON),
                          std::move(argv), std::move(redir), attr));
        } else {
          pushExitStatus(state, 0);
        }
        vmnext;
      }
      vmcase(BUILTIN_EXEC) {
        auto v = state.stack.getLocal(UDC_PARAM_ATTR).asNum();
        const auto attr = static_cast<CmdCallAttr>(v);
        Value redir = state.stack.getLocal(UDC_PARAM_REDIR);
        auto argv = state.stack.getLocal(UDC_PARAM_ARGV);
        int status = builtinExec(state, typeAs<ArrayObject>(argv), std::move(redir));
        TRY(checkCmdExecError(state, attr, status));
        pushExitStatus(state, status);
        vmnext;
      }
      vmcase(NEW_REDIR) {
        state.stack.push(Value::create<RedirObject>());
        vmnext;
      }
      vmcase(ADD_REDIR_OP) {
        const auto redirOp = static_cast<RedirOp>(consume8(state.stack.ip()));
        const int newFd = static_cast<int>(consume8(state.stack.ip()));
        auto value = state.stack.pop();
        typeAs<RedirObject>(state.stack.peek()).addEntry(std::move(value), redirOp, newFd);
        vmnext;
      }
      vmcase(ADD_REDIR_OP0) vmcase(ADD_REDIR_OP1) vmcase(ADD_REDIR_OP2) {
        const int newFd = static_cast<int>(op) - static_cast<int>(OpCode::ADD_REDIR_OP0);
        const auto redirOp = static_cast<RedirOp>(consume8(state.stack.ip()));
        auto value = state.stack.pop();
        typeAs<RedirObject>(state.stack.peek()).addEntry(std::move(value), redirOp, newFd);
        vmnext;
      }
      vmcase(DO_REDIR) {
        TRY(typeAs<RedirObject>(state.stack.peek()).redirect(state));
        vmnext;
      }
      vmcase(LOAD_CUR_MOD) {
        ModId modId = cast<CompiledCode>(state.stack.code())->getBelongedModId();
        const auto &entry = state.modLoader[modId];
        auto &modType = cast<ModType>(state.typePool.get(entry.second.getTypeId()));
        unsigned int index = modType.getIndex();
        state.stack.push(state.getGlobal(index));
        vmnext;
      }
      vmcase(LOAD_CUR_ARG0) {
        const ControlFrame *frame = nullptr;
        state.stack.walkFrames([&frame](const ControlFrame &f) {
          if (f.code->is(CodeKind::USER_DEFINED_CMD)) {
            frame = &f;
            return false;
          }
          return true;
        });
        Value arg0;
        if (frame) {
          unsigned int localOffset = frame->localVarOffset;
          arg0 = state.stack.unsafeGetOperand(localOffset + UDC_PARAM_N);
        } else {
          arg0 = state.getGlobal(BuiltinVarOffset::POS_0);
        }
        state.stack.push(std::move(arg0));
        vmnext;
      }
      vmcase(RAND) {
        state.stack.push(Value::createInt(state.getRng().nextInt64()));
        vmnext;
      }
      vmcase(GET_SECOND) {
        auto now = getCurrentTimestamp();
        auto diff = now - state.baseTime;
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(diff);
        int64_t v = state.getGlobal(BuiltinVarOffset::SECONDS).asInt();
        v += sec.count();
        state.stack.push(Value::createInt(v));
        vmnext;
      }
      vmcase(SET_SECOND) {
        state.baseTime = getCurrentTimestamp();
        auto v = state.stack.pop();
        state.setGlobal(BuiltinVarOffset::SECONDS, std::move(v));
        vmnext;
      }
      vmcase(GET_POS_ARG) {
        auto pos = state.stack.pop().asInt();
        auto args = state.stack.pop();
        auto v = Value::createStr();
        assert(pos > 0 && static_cast<uint64_t>(pos) <= ArrayObject::MAX_SIZE);
        unsigned int index = static_cast<uint64_t>(pos) - 1;
        if (index < typeAs<ArrayObject>(args).size()) {
          v = typeAs<ArrayObject>(args).getValues()[index];
        }
        state.stack.push(std::move(v));
        vmnext;
      }
      vmcase(UNWRAP) {
        if (state.stack.peek().kind() == ValueKind::INVALID) {
          raiseError(state, TYPE::UnwrappingError, "invalid value");
          vmerror;
        }
        vmnext;
      }
      vmcase(CHECK_INVALID) {
        bool b = state.stack.pop().kind() != ValueKind::INVALID;
        state.stack.push(Value::createBool(b));
        vmnext;
      }
      vmcase(RECLAIM_LOCAL) {
        unsigned char offset = consume8(state.stack.ip());
        unsigned char size = consume8(state.stack.ip());
        state.stack.reclaimLocals(offset, size);
        vmnext;
      }
    }

  SIGNAL: {
    assert(ARState::hasSignals());
    if (ARState::pendingSigSet.has(SIGCHLD)) {
      state.jobTable.waitForAny();
    }
    if (state.canHandleSignal && ARState::hasSignals()) {
      SignalGuard guard;
      int sigNum = ARState::popPendingSignal();
      if (auto handler = state.sigVector.lookup(sigNum); handler != nullptr) {
        state.canHandleSignal = false;
        if (!kickSignalHandler(state, sigNum, handler)) {
          state.canHandleSignal = true;
          vmerror;
        }
      }
    }
    vmnext;
  }

  EXCEPT:
    assert(state.hasError());
    if (!handleException(state)) {
      return false;
    }
    vmnext;
  }
}

void VM::rethrowFromFinally(ARState &state) {
  if (const auto entry = state.stack.exitFinally();
      entry.hasError()) { // ignore current exception and rethrow
    const auto curError = state.stack.takeThrownObject();
    curError->printStackTrace(state, ErrorObject::PrintOp::IGNORED);
    state.stack.setErrorObj(entry.asError());
  }
}

bool VM::handleException(ARState &state) {
  if (unlikely(state.hook != nullptr)) {
    state.hook->vmThrowHook(state);
  }

  for (; !state.stack.checkVMReturn(); state.stack.unwind()) {
    if (!state.stack.code()->is(CodeKind::NATIVE)) {
      auto *cc = cast<CompiledCode>(state.stack.code());

      // search exception entry
      const unsigned int occurredPC = state.stack.getFrame().getIPOffset() - 1;
      for (unsigned int i = 0; cc->getExceptionEntries()[i]; i++) {
        const ExceptionEntry &entry = cc->getExceptionEntries()[i];
        auto &entryType = state.typePool.get(entry.typeId);
        if (occurredPC >= entry.begin && occurredPC < entry.end) {
          // check finally
          if (auto &entries = state.stack.getFinallyEntries();
              !entries.empty() && entries.back().getDepth() == state.stack.getFrames().size()) {
            auto &cur = entries.back();
            if (entry.begin < cur.getAddr()) {
              rethrowFromFinally(state);
            }
          }

          const DSType &occurredType =
              state.typePool.get(state.stack.getThrownObject()->getTypeID());
          if (!entryType.isSameOrBaseTypeOf(occurredType)) {
            continue;
          }

          if (entryType.is(TYPE::ProcGuard_)) {
            /**
             * when exception entry indicate exception guard of sub-shell,
             * immediately break interpreter
             * (due to prevent signal handler interrupt and to load thrown object to stack)
             */
            return false;
          }
          state.stack.updateIPByOffset(entry.dest);
          state.stack.clearOperandsUntilGuard(StackGuardType::TRY, entry.guardLevel);
          state.stack.reclaimLocals(entry.localOffset, entry.localSize);
          if (entryType.is(TYPE::Root_)) { // finally block
            state.stack.enterFinally(entry.dest);
          } else { // catch block
            state.stack.loadThrownObject();
            state.setExitStatus(0); // clear exit status when enter catch block
          }
          return true;
        }
      }
    } else if (state.stack.code() == &signalTrampoline) { // within signal trampoline
      state.canHandleSignal = true;
    }

    auto &entries = state.stack.getFinallyEntries();
    while (!entries.empty() && entries.back().getDepth() == state.stack.getFrames().size()) {
      rethrowFromFinally(state);
    }
  }
  return false;
}

EvalRet VM::startEval(ARState &state, EvalOP op, ARError *dsError, Value &value) {
  assert(state.stack.recDepth() > 0);
  if (state.stack.recDepth() == 1) {
    setLocaleSetting();
    setSignalSetting(state);
  }

  const unsigned int oldLevel = state.subshellLevel;

  // run main loop
  const auto ret = mainLoop(state);
  /**
   * if return form subshell, subshellLevel is greater than old.
   */
  const bool subshell = oldLevel != state.subshellLevel;
  if (ret) {
    value = state.stack.pop();
  } else {
    if (!subshell && hasFlag(op, EvalOP::PROPAGATE)) {
      return EvalRet::HAS_ERROR;
    }
  }

  // handle uncaught exception and termination handler
  handleUncaughtException(state, dsError);
  if (subshell) {
    callTermHook(state);
    exit(state.getMaskedExitStatus());
  }
  return ret ? EvalRet::SUCCESS : EvalRet::HANDLED_ERROR;
}

bool VM::callToplevel(ARState &state, const ObjPtr<FuncObject> &func, ARError *dsError) {
  assert(state.stack.recDepth() == 0);

  // set module to global
  state.globals.resize(state.rootModScope->getMaxGlobalVarIndex());
  {
    auto &type = state.typePool.get(func->getTypeID());
    assert(isa<ModType>(type));
    auto &modType = cast<ModType>(type);
    const unsigned int index = modType.getIndex();
    state.setGlobal(index, Value(func));
  }

  // prepare stack
  state.stack.reset();
  RecursionGuard guard(state);
  state.stack.wind(0, 0, func->getCode());

  Value ret;
  const auto s = startEval(state, EvalOP{}, dsError, ret);
  assert(s != EvalRet::HAS_ERROR);
  return s == EvalRet::SUCCESS;
}

unsigned int VM::prepareArguments(VMState &state, Value &&recv, CallArgs &&args) {
  state.clearThrownObject();

  // push arguments
  const unsigned int size = args.first;
  state.reserve(size + 1);
  state.push(std::move(recv));
  for (unsigned int i = 0; i < size; i++) {
    state.push(std::move(args.second[i]));
  }
  return size;
}

bool RecursionGuard::checkLimit() {
  if (this->state.getCallStack().recDepth() == SYS_LIMIT_NATIVE_RECURSION) {
    raiseError(this->state, TYPE::StackOverflowError, "interpreter recursion depth reaches limit");
    return false;
  }
  return true;
}

#define GUARD_RECURSION(state)                                                                     \
  RecursionGuard _guard(state);                                                                    \
  do {                                                                                             \
    if (!_guard.checkLimit()) {                                                                    \
      return nullptr;                                                                              \
    }                                                                                              \
  } while (false)

static NativeCode initCmdTrampoline() noexcept {
  NativeCode::ArrayType code;
  code[0] = static_cast<char>(OpCode::LOAD_LOCAL);
  code[1] = 0;
  code[2] = static_cast<char>(OpCode::PUSH_NULL);
  code[3] = static_cast<char>(OpCode::CALL_CMD_COMMON);
  code[4] = static_cast<char>(OpCode::RETURN);
  return NativeCode(code);
}

Value VM::execCommand(ARState &state, std::vector<Value> &&argv, bool propagate) {
  GUARD_RECURSION(state);

  static auto cmdTrampoline = initCmdTrampoline();

  const auto op = propagate ? EvalOP::PROPAGATE : EvalOP{};
  Value ret;
  auto obj = Value::create<ArrayObject>(state.typePool.get(TYPE::StringArray), std::move(argv));
  prepareArguments(state.stack, std::move(obj), {0, {}});
  if (windStackFrame(state, 1, 1, cmdTrampoline)) {
    startEval(state, op, nullptr, ret);
  }
  return ret;
}

Value VM::callFunction(ARState &state, Value &&funcObj, CallArgs &&args) {
  GUARD_RECURSION(state);

  auto &type = state.typePool.get(funcObj.getTypeID());
  const unsigned int size = prepareArguments(state.stack, std::move(funcObj), std::move(args));

  Value ret;
  if (prepareFuncCall(state, size)) {
    assert(type.isFuncType());
    startEval(state, EvalOP::PROPAGATE, nullptr, ret);
  }
  if (cast<FunctionType>(type).getReturnType().isVoidType()) {
    ret = Value(); // clear return value
  }
  return ret;
}

ARErrorKind VM::handleUncaughtException(ARState &state, ARError *dsError) {
  if (!state.hasError()) {
    return AR_ERROR_KIND_SUCCESS;
  }

  auto except = state.stack.takeThrownObject();
  auto &errorType = state.typePool.get(except->getTypeID());
  ARErrorKind kind = AR_ERROR_KIND_RUNTIME_ERROR;
  if (errorType.is(TYPE::ShellExit_)) {
    kind = AR_ERROR_KIND_EXIT;
  } else if (errorType.is(TYPE::AssertFail_)) {
    kind = AR_ERROR_KIND_ASSERTION_ERROR;
  }
  switch (kind) {
  case AR_ERROR_KIND_RUNTIME_ERROR:
  case AR_ERROR_KIND_ASSERTION_ERROR:
  case AR_ERROR_KIND_EXIT:
    state.setExitStatus(except->getStatus());
    break;
  default:
    break;
  }

  // get error line number
  unsigned int errorLineNum = 0;
  std::string sourceName;
  if (state.typePool.get(TYPE::Error).isSameOrBaseTypeOf(errorType) ||
      kind != AR_ERROR_KIND_RUNTIME_ERROR) {
    auto &trace = except->getStackTrace();
    errorLineNum = getOccurredLineNum(trace);
    sourceName = getOccurredSourceName(trace);
  }

  // print error message
  if (kind == AR_ERROR_KIND_RUNTIME_ERROR || kind == AR_ERROR_KIND_ASSERTION_ERROR ||
      state.has(RuntimeOption::TRACE_EXIT)) {
    except->printStackTrace(state, ErrorObject::PrintOp::UNCAUGHT);
  }

  if (dsError != nullptr) {
    *dsError = {.kind = kind,
                .fileName = sourceName.empty() ? nullptr : strdup(sourceName.c_str()),
                .lineNum = errorLineNum,
                .chars = 0,
                .name = strdup(kind == AR_ERROR_KIND_RUNTIME_ERROR ? errorType.getName() : "")};
  }
  state.stack.setErrorObj(std::move(except)); // restore thrown object
  return kind;
}

bool VM::callTermHook(ARState &state) {
  auto except = state.stack.takeThrownObject();
  assert(state.termHookIndex != 0);
  auto funcObj = state.getGlobal(state.termHookIndex);
  if (funcObj.kind() == ValueKind::INVALID) {
    return false;
  }

  int termKind = TERM_ON_EXIT;
  if (except) {
    auto &type = state.typePool.get(except->getTypeID());
    if (type.is(TYPE::AssertFail_)) {
      termKind = TERM_ON_ASSERT;
    } else if (!type.is(TYPE::ShellExit_)) {
      termKind = TERM_ON_ERR;
    }
  }

  auto oldExitStatus = state.getGlobal(BuiltinVarOffset::EXIT_STATUS);
  auto args =
      makeArgs(Value::createInt(termKind), termKind == TERM_ON_ERR ? except : oldExitStatus);

  state.canHandleSignal = false;
  VM::callFunction(state, std::move(funcObj), std::move(args)); // ignore exception
  state.stack.clearThrownObject();

  // restore old value
  state.setGlobal(BuiltinVarOffset::EXIT_STATUS, std::move(oldExitStatus));
  state.canHandleSignal = true;

  // clear TERM_HOOK
  state.setGlobal(state.termHookIndex, Value::createInvalid());
  return true;
}

} // namespace arsh