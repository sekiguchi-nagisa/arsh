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
#include <random>

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

AtomicSigSet ARState::pendingSigSet;

/**
 * if environmental variable SHLVL does not exist, set 0.
 */
static int64_t getShellLevel() {
  const char *shlvl = getenv(ENV_SHLVL);
  int64_t level = 0;
  if (shlvl != nullptr) {
    const auto pair = convertToNum10<int64_t>(shlvl);
    if (pair && pair.value > -1) {
      level = pair.value;
    }
  }
  return level;
}

static int64_t originalShellLevel() {
  static const auto level = getShellLevel();
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
  {
    const char *defaultDir = "/";
    const char *defaultName = "";
    if (const struct passwd *pw = getpwuid(getuid()); likely(pw != nullptr)) {
      defaultDir = pw->pw_dir;
      defaultName = pw->pw_name;
    }
    // set HOME
    setenv(ENV_HOME, defaultDir, 0 /*not overwrite */);

    // set LOGNAME
    setenv(ENV_LOGNAME, defaultName, 0 /*not overwrite */);

    // set USER
    setenv(ENV_USER, defaultName, 0 /*not overwrite */);
  }

  // set PWD/OLDPWD
  setPWDs();
}

static bool check_strftime_plus(timestamp ts) {
  const auto time = timestampToTimespec(ts);
  struct tm tm{};
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
      baseTime(this->initTime), rng(this->baseTime.time_since_epoch().count(),
                                    std::random_device()(), 42, reinterpret_cast<uintptr_t>(this)) {
  // init envs
  initEnv();
  const char *pwd = getenv(ENV_PWD);
  assert(pwd);
  if (*pwd == '/') {
    this->logicalWorkingDir = expandDots(nullptr, pwd);
  }
}

void ARState::updatePipeStatus(unsigned int size, const Proc *procs, bool mergeExitStatus) {
  if (size == 1 && !mergeExitStatus) {
    return; // do nothing for subshell
  }
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

bool VM::checkCast(ARState &state, const Type &targetType) {
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
    if (setEnv(state.pathCache, name, ref.data())) {
      env = getenv(name);
    }
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
  if (setEnv(state.pathCache, valueRef.hasNullChar() ? "" : nameRef.data(), valueRef.data())) {
    return true;
  }
  std::string str = ERROR_SET_ENV;
  str += nameRef;
  raiseError(state, TYPE::IllegalAccessError, std::move(str));
  return false;
}

void VM::pushNewObject(ARState &state, const Type &type) {
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

bool VM::prepareUserDefinedCommandCall(ARState &state, const ARCode &code,
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
      raiseStringLimit(state);
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
        raiseStringLimit(state);
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

static Value newProcSubst(const ARState &st, int &fd, Job &&job) {
  if (fd < 0) {
    return st.emptyFDObj;
  }
  int v = fd;
  fd = -1;
  remapFDCloseOnExec(v);
  return Value::create<UnixFdObject>(v, std::move(job));
}

static bool checkPipelineError(ARState &state, const unsigned int procSize, const Proc *const procs,
                               bool lastPipe) {
  if (!state.has(RuntimeOption::ERR_RAISE)) {
    return true;
  }

  const unsigned int lastIndex = procSize - (lastPipe ? 0 : 1);
  unsigned int index = 0;
  int64_t exitStatus = 0;
  for (; index < procSize; index++) {
    auto &proc = procs[index];
    if (exitStatus = proc.exitStatus(); exitStatus != 0) {
      if (index < lastIndex && proc.signaled() && proc.asSigNum() == SIGPIPE) {
        if (!state.has(RuntimeOption::FAIL_SIGPIPE)) {
          continue;
        }
      }
      goto ERROR;
    }
  }
  if (lastPipe) {
    exitStatus = state.getGlobal(BuiltinVarOffset::EXIT_STATUS).asInt();
    if (exitStatus != 0) {
      goto ERROR;
    }
  }
  return true;

ERROR:
  std::string message;
  if (procSize == 1 && !lastPipe) { // subshell
    message = "child process exits with non-zero status: `";
    message += std::to_string(exitStatus);
    message += "'";
  } else {
    message = "pipeline has non-zero status: `";
    message += std::to_string(exitStatus);
    message += "' at ";
    message += std::to_string(index + 1);
    message += "th element";
  }
  raiseError(state, TYPE::ExecError, std::move(message), exitStatus);
  return false;
}

static bool checkPipelineError(ARState &state, const JobObject &job, bool lastPipe) {
  return checkPipelineError(state, job.getProcSize(), job.getProcs(), lastPipe);
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
    if (entry->isAvailable()) {
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
    if (forkKind == ForkKind::PIPE_FAIL) {
      if (!checkPipelineError(state, *entry, false)) {
        return false;
      }
    }
    ret = exitStatusToBool(status);
    break;
  }
  case ForkKind::IN_PIPE:
  case ForkKind::OUT_PIPE: {
    /**
     * job object does not maintain <(), >() file descriptor
     */
    auto entry = JobObject::create(procSize, procs, false, state.emptyFDObj, state.emptyFDObj,
                                   std::move(desc));
    state.jobTable.attach(entry, true); // always disowned

    // create process substitution wrapper
    int &fd = forkKind == ForkKind::IN_PIPE ? pipeSet.in[WRITE_PIPE] : pipeSet.out[READ_PIPE];
    ret = newProcSubst(state, fd, std::move(entry));
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
  PipeSet pipeSet;
  if (!pipeSet.openAll(forkKind)) {
    raiseSystemError(state, errno, ERROR_PIPE);
    return false;
  }
  const pid_t pgid = resolvePGID(state.isRootShell(), forkKind);
  const auto procOp = resolveProcOp(state, forkKind);
  const bool jobCtrl = state.isJobControl();
  auto proc = Proc::fork(state, pgid, procOp);
  if (proc.pid() > 0) { // parent process
    pipeSet.in.close(READ_PIPE);
    pipeSet.out.close(WRITE_PIPE);

    Value obj;

    switch (forkKind) {
    case ForkKind::STR:
    case ForkKind::ARRAY: { // always disable job control (so not change foreground process group)
      assert(!hasFlag(procOp, Proc::Op::JOB_CONTROL));
      pipeSet.in.close(WRITE_PIPE);
      const bool ret = forkKind == ForkKind::STR
                           ? readAsStr(state, pipeSet.out[READ_PIPE], obj)
                           : readAsStrArray(state, pipeSet.out[READ_PIPE], obj);
      if (!ret || ARState::isInterrupted()) {
        /**
         * if read failed, not wait termination (always attach to job table)
         */
        pipeSet.out.close(READ_PIPE); // close read pipe after wait, due to prevent EPIPE
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
      pipeSet.out.close(READ_PIPE); // close read pipe after wait, due to prevent EPIPE
      if (!proc.is(Proc::State::TERMINATED)) {
        state.jobTable.attach(
            JobObject::create(proc, state.emptyFDObj, state.emptyFDObj, std::move(desc)));
      }
      state.setExitStatus(status);
      if (status < 0) {
        raiseSystemError(state, errNum, "wait failed");
        return false;
      }
      if (!checkPipelineError(state, 1, &proc, false)) {
        return false;
      }
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
    int errNum = 0;
    const bool s = pipeSet.setupChildStdin(forkKind, jobCtrl) && pipeSet.setupChildStdout();
    if (!s) {
      errNum = errno;
    }
    pipeSet.closeAll();

    if (errNum != 0) {
      raiseSystemError(state, errNum, ERROR_FD_SETUP);
      return false;
    }
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

static NativeCode initBuiltinEval() {
  NativeCode::ArrayType code;
  code[0] = static_cast<char>(OpCode::BUILTIN_EVAL);
  code[1] = static_cast<char>(OpCode::LOAD_STATUS);
  code[2] = static_cast<char>(OpCode::RETURN_UDC);
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

ResolvedCmd CmdResolver::operator()(const ARState &state, const StringRef ref,
                                    const ModType *modType) const {
  // first, check user-defined command
  if (hasFlag(this->resolveOp, Op::FROM_UDC)) {
    const auto fqn = hasFlag(this->resolveOp, Op::FROM_FQN_UDC) ? ref.find('\0') : StringRef::npos;
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
      modType = &getCurRuntimeModule(state);
    }
    assert(modType);
    if (ResolvedCmd cmd{}; lookupUdc(state, *modType, cmdName, hasNullChar, cmd)) {
      return cmd;
    }
    if (hasNullChar) {
      return ResolvedCmd::invalid();
    }
  }

  // second, check builtin command
  if (hasFlag(this->resolveOp, Op::FROM_BUILTIN)) {
    if (const builtin_command_t bcmd = lookupBuiltinCommand(ref); bcmd != nullptr) {
      return ResolvedCmd::fromBuiltin(bcmd);
    }

    static const std::pair<const char *, NativeCode> sb[] = {
        {"call", initCode(OpCode::BUILTIN_CALL)},
        {"command", initCode(OpCode::BUILTIN_CMD)},
        {"eval", initBuiltinEval()},
        {"exec", initCode(OpCode::BUILTIN_EXEC)},
    };
    for (auto &[name, code] : sb) {
      if (ref == name) {
        return ResolvedCmd::fromBuiltin(code);
      }
    }
  }

  // resolve dynamic registered user-defined command
  if (hasFlag(this->resolveOp, Op::FROM_DYNA_UDC)) {
    auto &map = typeAs<OrderedMapObject>(state.getGlobal(BuiltinVarOffset::DYNA_UDCS));
    if (const auto retIndex = map.lookup(OrderedMapKey(ref)); retIndex != -1) {
      auto *obj = map[retIndex].getValue().get();
      assert(isa<FuncObject>(*obj) || isa<ClosureObject>(*obj));
      return ResolvedCmd::fromCmdObj(obj);
    }
  }

  // resolve external command path
  auto cmd = ResolvedCmd::fromExternal(nullptr);
  if (hasFlag(this->resolveOp, Op::FROM_EXTERNAL)) {
    const char *cmdName = ref.data();
    cmd = ResolvedCmd::fromExternal(state.pathCache.searchPath(cmdName, this->searchOp));

    // if command not found or directory, lookup CMD_FALLBACK
    if (hasFlag(this->resolveOp, Op::FROM_FALLBACK) &&
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
  appendAsPrintable(cmdName, SYS_LIMIT_ERROR_MSG_MAX - 128, str);
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

static Value toCmdDesc(const ArrayObject &argvObj) {
  std::string value;
  for (auto &e : argvObj.getValues()) {
    if (!value.empty()) {
      value += ' ';
    }
    if (!formatJobDesc(e.asCStr(), value)) {
      break;
    }
  }
  return Value::createStr(std::move(value));
}

bool VM::forkAndExec(ARState &state, const char *filePath, const ArrayObject &argvObj,
                     Value &&redirConfig) {
  // setup self pipe
  Pipe selfPipe;
  if (!selfPipe.open()) {
    raiseSystemError(state, errno, ERROR_PIPE);
    return false;
  }

  const pid_t pgid = resolvePGID(state.isRootShell(), ForkKind::NONE);
  const auto procOp = resolveProcOp(state, ForkKind::NONE);
  auto proc = Proc::fork(state, pgid, procOp);
  if (proc.pid() == -1) {
    selfPipe.close();
    raiseCmdError(state, argvObj.getValues()[0].asCStr(), EAGAIN);
    return false;
  } else if (proc.pid() == 0) { // child
    selfPipe.close(READ_PIPE);
    xexecve(filePath, argvObj, nullptr);

    const int errNum = errno;
    const ssize_t r = write(selfPipe[WRITE_PIPE], &errNum, sizeof(int));
    (void)r; // FIXME:
    exit(-1);
  } else { // parent process
    selfPipe.close(WRITE_PIPE);
    redirConfig = nullptr; // restore redirConfig

    ssize_t readSize;
    int errNum = 0;
    while ((readSize = read(selfPipe[READ_PIPE], &errNum, sizeof(int))) == -1) {
      if (errno != EAGAIN && errno != EINTR) {
        break;
      }
    }
    selfPipe.close(READ_PIPE);
    if (readSize > 0) { // remove cached path
      state.pathCache.removePath(argvObj.getValues()[0].asCStr());
    }

    // wait process or job termination
    const auto waitOp = state.isJobControl() ? WaitOp::BLOCK_UNTRACED : WaitOp::BLOCKING;
    const int status = proc.wait(waitOp);
    int errNum2 = errno;
    if (!proc.is(Proc::State::TERMINATED)) {
      const auto job = state.jobTable.attach(
          JobObject::create(proc, state.emptyFDObj, state.emptyFDObj, toCmdDesc(argvObj)));
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
      raiseCmdError(state, argvObj.getValues()[0].asCStr(), errNum2);
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
  const auto cmd = resolver(state, argvObj->getValues()[0].asStrRef());
  return callCommand(state, cmd, std::move(argvObj), std::move(redirConfig), attr);
}

static void traceCmd(const ARState &state, const ArrayObject &argv) {
  std::string value;
  for (auto &e : argv.getValues()) {
    value += ' ';
    if (const StringRef ref = e.asStrRef(); !checkedAppend(ref, SYS_LIMIT_XTRACE_LINE_LEN, value)) {
      value += ref.substr(0, SYS_LIMIT_XTRACE_LINE_LEN);
      value.resize(SYS_LIMIT_XTRACE_LINE_LEN - 3);
      value += "...";
      break;
    }
  }

  const int fd = typeAs<UnixFdObject>(getBuiltinGlobal(state, VAR_XTRACEFD)).getRawFd();
  state.getCallStack().fillStackTrace([&value, &fd](StackTraceElement &&trace) {
    dprintf(fd, "+ %s:%d>%s\n", trace.getSourceName().c_str(), trace.getLineNum(), value.c_str());
    value = "";
    return false; // print only once
  });

  if (!value.empty()) {
    dprintf(fd, "+%s\n", value.c_str());
  }
  fsync(fd);
  errno = 0; // ignore error
}

static bool checkCmdExecError(ARState &state, StringRef cmdName, CmdCallAttr attr, int64_t status) {
  if (status != 0 && hasFlag(attr, CmdCallAttr::RAISE) && state.has(RuntimeOption::ERR_RAISE)) {
    std::string message = "`";
    appendAsPrintable(cmdName, SYS_LIMIT_ERROR_MSG_MAX, message);
    message += "' command exits with non-zero status: `";
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
    const auto cmdName = array.getValues()[0];
    const int status = cmd.builtinCmd()(state, array);
    flushStdFD();
    if (state.hasError()) {
      return false;
    }
    pushExitStatus(state, status); // set exit status before check ERR_RAISE
    if (!checkCmdExecError(state, cmdName.asStrRef(), attr, status)) {
      return false;
    }
    return true;
  }
  case ResolvedCmd::MODULE:
    return prepareSubCommand(state, cmd.modType(), std::move(argvObj), std::move(redirConfig));
  case ResolvedCmd::CMD_OBJ: {
    auto *obj = cmd.cmdObj();
    const ARCode *code = nullptr;
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
    if (hasFlag(attr, CmdCallAttr::NEED_FORK)) {
      const bool ret = forkAndExec(state, cmd.filePath(), array, std::move(redirConfig));
      if (ret) {
        const int status = state.getMaskedExitStatus();
        if (!checkCmdExecError(state, array.getValues()[0].asStrRef(), attr, status)) {
          return false;
        }
      }
      return ret;
    } else {
      xexecve(cmd.filePath(), array, nullptr);
      raiseCmdError(state, array.getValues()[0].asCStr(), errno);
      return false;
    }
  }
  case ResolvedCmd::FALLBACK: {
    const auto &modType = getCurRuntimeModule(state);
    state.stack.reserve(3);
    state.stack.push(getBuiltinGlobal(state, VAR_CMD_FALLBACK));
    state.stack.push(state.getGlobal(modType.getIndex()));
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

    const auto resolve = CmdResolver(CmdResolver::Op::NO_UDC,
                                     useDefaultPath ? FilePathCache::SearchOp::USE_DEFAULT_PATH
                                                    : FilePathCache::SearchOp::NON);
    const bool r = callCommand(state, resolve, std::move(argvObj), std::move(redir), attr);
    return BuiltinCmdResult::call(r);
  }

  // show command description
  unsigned int successCount = 0;
  int errNum = 0;
  for (; index < argc; index++) {
    const auto ref = arrayObj.getValues()[index].asStrRef();
    auto cmd = CmdResolver(CmdResolver::Op::NO_FALLBACK | CmdResolver::Op::FROM_FQN_UDC,
                           FilePathCache::SearchOp::DIRECT_SEARCH)(state, ref);
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

int VM::builtinExec(ARState &state, ArrayObject &argvObj, Value &&redir) {
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

    /**
     * preserve arg0 (searchPath result indidate orignal pointer)
     */
    const auto arg0 = argvObj.getValues()[index];
    const char *filePath =
        state.pathCache.searchPath(arg0.asCStr(), FilePathCache::SearchOp::DIRECT_SEARCH);
    if (progName.data() != nullptr) {
      if (progName.hasNullChar()) {
        const auto name = toPrintable(progName);
        ERROR(state, argvObj, "contains null characters: %s", name.c_str());
        return 1;
      }
      argvObj.refValues()[index] = Value::createStr(progName); // not check iterator invalidation
    }
    const auto begin = argvObj.getValues().begin();
    argvObj.refValues().erase(begin, begin + index); // not check iterator invalidation

    // decrement SHLVL before call command
    std::string oldSHLVL;
    bool hasSHLVL = false;
    if (!clearEnv) {
      if (const char *v = getenv(ENV_SHLVL)) {
        hasSHLVL = true;
        oldSHLVL = v;
      }
      int64_t level = getShellLevel();
      if (level > 0) {
        level--;
      } else {
        level = 0;
      }
      setenv(ENV_SHLVL, std::to_string(level).c_str(), 1);
    }

    char *envp[] = {nullptr};
    xexecve(filePath, argvObj, clearEnv ? envp : nullptr);
    raiseCmdError(state, argvObj.getValues()[0].asCStr(), errno);
    state.pathCache.removePath(argvObj.getValues()[0].asCStr()); // always remove entry

    // restore SHLVL
    if (!clearEnv) {
      if (hasSHLVL) {
        setenv(ENV_SHLVL, oldSHLVL.c_str(), 1);
      } else {
        unsetenv(ENV_SHLVL);
      }
    }
    return 1;
  }
  return 0;
}

#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (unlikely(!(E))) {                                                                          \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

bool VM::builtinEval(ARState &state, ArrayObject &argvObj) {
  state.stack.setLocal(UDC_PARAM_ARGV + 1, argvObj.takeFirst()); // not check iterator invalidation
  const unsigned int size = argvObj.size();
  if (size == 0) {
    state.setExitStatus(0);
    return true;
  }
  Value src;
  if (size == 1) {
    src = argvObj.getValues()[0];
  } else {
    StrBuilder builder(state);
    for (unsigned int i = 0; i < size; i++) {
      if (i > 0) {
        TRY(builder.add(" "));
      }
      TRY(builder.add(argvObj.getValues()[i].asStrRef()));
    }
    src = std::move(builder).take();
  }
  auto &modType = getCurRuntimeModule(state);
  if (auto func = compileAsFunc(state, src.asStrRef(), modType, false)) {
    state.stack.push(func);
    return prepareFuncCall(state, 0);
  }
  return false;
}

bool VM::returnFromUserDefinedCommand(ARState &state, int64_t status) {
  const auto attr = static_cast<CmdCallAttr>(state.stack.getLocal(UDC_PARAM_ATTR).asNum());
  const auto arg0 = state.stack.getLocal(UDC_PARAM_ARG0);
  state.stack.unwind();
  pushExitStatus(state, status); // set exit status before check ERR_RAISE
  if (!checkCmdExecError(state, arg0.asStrRef(), attr, status)) {
    return false;
  }
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

  PipeSet pipeSet;
  PipeList pipes(pipeSize);
  if (!pipeSet.openAll(forkKind) || !pipes.openAll()) {
    raiseSystemError(state, errno, ERROR_PIPE);
    return false;
  }

  // fork
  InlinedArray<Proc, 6> children(procSize);
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
  if (proc.pid() == 0) { // child
    int errNum = 0;
    if (procIndex == 0) { // first process
      if (dup2(pipes[procIndex][WRITE_PIPE], STDOUT_FILENO) < 0 ||
          !pipeSet.setupChildStdin(forkKind, jobCtrl)) {
        errNum = errno;
      }
    } else if (procIndex > 0 && procIndex < pipeSize) { // other process.
      if (dup2(pipes[procIndex - 1][READ_PIPE], STDIN_FILENO) < 0 ||
          dup2(pipes[procIndex][WRITE_PIPE], STDOUT_FILENO) < 0) {
        errNum = errno;
      }
    } else if (procIndex == pipeSize && !lastPipe) { // last process
      if (dup2(pipes[procIndex - 1][READ_PIPE], STDIN_FILENO) < 0 || !pipeSet.setupChildStdout()) {
        errNum = errno;
      }
    }
    pipeSet.closeAll();
    pipes.closeAll();

    if (errNum) {
      raiseSystemError(state, errNum, ERROR_FD_SETUP);
      return false;
    }

    // set pc to next instruction
    state.stack.ip() += read16(state.stack.ip() + 1 + (procIndex * 2)) - 1;
    return true;
  } else if (procIndex == procSize) { // parent (last pipeline)
    if (lastPipe) {
      /**
       * in last pipe, save current stdin before call dup2
       */
      auto jobEntry = JobObject::create(procSize, children.ptr(), true, state.emptyFDObj,
                                        state.emptyFDObj, std::move(desc));
      state.jobTable.attach(jobEntry);
      int errNum = 0;
      if (dup2(pipes[procIndex - 1][READ_PIPE], STDIN_FILENO) < 0) {
        errNum = errno;
      }
      pipes.closeAll();
      if (errNum) {
        raiseSystemError(state, errNum, ERROR_FD_SETUP);
        return false;
      }
      state.stack.push(Value::create<PipelineObject>(state, std::move(jobEntry)));
    } else {
      pipeSet.in.close(READ_PIPE);
      pipeSet.out.close(WRITE_PIPE);
      pipes.closeAll();
      Value obj;
      if (!attachAsyncJob(state, std::move(desc), procSize, children.ptr(), forkKind, pipeSet,
                          obj)) {
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

static const auto signalTrampoline = initSignalTrampoline();

bool VM::kickSignalHandler(ARState &state, int sigNum, Value &&func) {
  state.stack.reserve(3);
  state.stack.push(state.getGlobal(BuiltinVarOffset::EXIT_STATUS));
  state.stack.push(std::move(func));
  state.stack.push(Value::createSig(sigNum));

  return windStackFrame(state, 3, 3, signalTrampoline);
}

static NativeCode initTermHookTrampoline() {
  NativeCode::ArrayType code;
  code[0] = static_cast<char>(OpCode::LOAD_LOCAL);
  code[1] = 1;
  code[2] = static_cast<char>(OpCode::CALL_FUNC);
  code[3] = 0;
  code[4] = static_cast<char>(OpCode::RETURN_TERM);
  return NativeCode(code);
}

static const auto termHookTrampoline = initTermHookTrampoline();

bool VM::kickTermHook(ARState &state, Value &&func) {
  state.stack.reserve(2);
  state.stack.push(state.getGlobal(BuiltinVarOffset::EXIT_STATUS));
  state.stack.push(std::move(func)); // () -> Void

  return windStackFrame(state, 2, 2, termHookTrampoline);
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

#undef TRY
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
      vmcase(SUBSHELL_EXIT) {
        assert(state.subshellLevel());
        if (state.has(RuntimeOption::HUP_EXIT)) {
          state.jobTable.send(SIGHUP);
        }
        exit(state.getMaskedExitStatus());
      }
      vmcase(TERM_HOOK) {
        assert(state.subshellLevel());
        handleUncaughtException(state, nullptr);
        if (auto funcObj = state.getGlobal(state.termHookIndex);
            funcObj.kind() != ValueKind::INVALID) {
          TRY(kickTermHook(state, std::move(funcObj)));
        }
        vmnext;
      }
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
        raiseAssertFail(state, std::move(msg));
        vmerror;
      }
      vmcase(ASSERT_FAIL2) {
        const auto assertOp = static_cast<AssertOp>(consume8(state.stack.ip()));
        auto msg = state.stack.pop();
        auto right = state.stack.pop();
        auto left = state.stack.pop();
        raiseAssertFail(state, std::move(msg), assertOp, std::move(left), std::move(right));
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
        auto dummy = state.stack.pop();
        auto &targetType = state.typePool.get(dummy.getTypeID());
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
      vmcase(PUSH_TYPE) {
        unsigned int v = consume24(state.stack.ip());
        const auto &targetType = state.typePool.get(v);
        state.stack.push(Value::createDummy(targetType));
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
      vmcase(LOAD_CONST2) {
        unsigned short index = consume16(state.stack.ip());
        state.stack.push(CONST_POOL(state)[index]);
        vmnext;
      }
      vmcase(LOAD_CONST4) {
        unsigned int index = consume32(state.stack.ip());
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
      vmcase(CALL_BUILTIN2) {
        unsigned int index = consume8(state.stack.ip());
        Value ret = nativeFuncPtrTable()[index](state);
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
      vmcase(RETURN_TERM) {
        auto v = state.stack.getLocal(0); // old exit status
        state.stack.unwind();
        handleUncaughtException(state, nullptr, true);
        state.setGlobal(BuiltinVarOffset::EXIT_STATUS, std::move(v));
        state.stack.push(Value::createInvalid()); // push void
        if (state.stack.checkVMReturn()) {
          return true;
        }
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
        state.stack.enterFinally(savedIndex);
        vmnext;
      }
      vmcase(EXIT_FINALLY) {
        auto entry = state.stack.exitFinally();
        if (entry.hasError()) {
          if (state.hasError()) {
            if (auto e = entry.asError()->addSuppressed(state.stack.takeThrownObject())) {
              e->printStackTrace(state, ErrorObject::PrintOp::IGNORED);
            }
          }
          state.stack.setThrownObject(entry.asError());
        }
        if (state.hasError()) {
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
      vmcase(SYNC_PIPESTATUS) {
        const unsigned char index = consume8(state.stack.ip());
        auto &pipeline = typeAs<PipelineObject>(state.stack.getLocal(index));
        auto job = pipeline.syncStatusAndDispose();
        if (job && !checkPipelineError(state, *job, true)) {
          vmerror;
        }
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
      vmcase(EXPAND_TILDE) vmcase(APPEND_TILDE) {
        const auto value = state.stack.pop();
        const bool assign = op == OpCode::APPEND_TILDE;
        if (!applyTildeExpansion(state, value.asStrRef(), assign)) {
          vmerror;
        }
        vmnext;
      }
      vmcase(PARSE_CLI) {
        auto value = state.stack.pop();
        auto &obj = typeAs<BaseObject>(value);
        auto &args = typeAs<ArrayObject>(state.stack.getLocal(UDC_PARAM_ARGV));
        if (!parseCommandLine(state, args, obj)) {
          if (!state.typePool.get(state.stack.getThrownObject()->getTypeID()).is(TYPE::CLIError)) {
            vmerror; // propagate other error
          }
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

        TRY(callCommand(state,
                        CmdResolver(CmdResolver::Op::NO_STATIC_UDC, FilePathCache::SearchOp::NON),
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

        if (argv->size()) {
          TRY(callCommand(state, CmdResolver(), std::move(argv), std::move(redir),
                          CmdCallAttr::NEED_FORK));
        } else {
          pushExitStatus(state, 0);
        }
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
        const auto arg0 = argv->getValues()[0];
        const auto ret = builtinCommand(state, std::move(argv), std::move(redir), attr);
        flushStdFD();
        if (ret.kind == BuiltinCmdResult::CALL) {
          TRY(ret.r);
        } else {
          TRY(checkCmdExecError(state, arg0.asStrRef(), attr, ret.status));
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
        if (argv->size()) {
          TRY(callCommand(
              state,
              CmdResolver(CmdResolver::Op::FROM_DEFAULT_WITH_FQN, FilePathCache::SearchOp::NON),
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
        auto &argv = typeAs<ArrayObject>(state.stack.getLocal(UDC_PARAM_ARGV));
        const auto arg0 = argv.getValues()[0];
        int status = builtinExec(state, argv, std::move(redir));
        if (state.hasError()) {
          vmerror;
        }
        TRY(checkCmdExecError(state, arg0.asStrRef(), attr, status));
        pushExitStatus(state, status);
        vmnext;
      }
      vmcase(BUILTIN_EVAL) {
        auto argv = toObjPtr<ArrayObject>(state.stack.getLocal(UDC_PARAM_ARGV));
        TRY(builtinEval(state, *argv));
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
          arg0 = state.stack.unsafeGetOperand(localOffset + UDC_PARAM_ARG0);
        } else {
          arg0 = state.getGlobal(BuiltinVarOffset::ARG0);
        }
        state.stack.push(std::move(arg0));
        vmnext;
      }
      vmcase(LOAD_CUR_THROWN) {
        auto value = Value::createInvalid();
        if (!state.stack.getFinallyEntries().empty()) {
          if (auto &e = state.stack.getFinallyEntries().back(); e.hasError()) {
            value = e.asError();
          }
        }
        state.stack.push(std::move(value));
        vmnext;
      }
      vmcase(LOAD_STATUS) {
        auto v = state.getGlobal(BuiltinVarOffset::EXIT_STATUS);
        state.stack.push(std::move(v));
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
          raiseError(state, TYPE::UnwrapError, "invalid value");
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
    if (ARState::hasSignal(SIGCHLD)) {
      state.jobTable.waitForAny();
    }
    if (state.canHandleSignal && ARState::hasSignals()) {
      SignalGuard guard;
      int sigNum = ARState::popPendingSignal();
      if (sigNum == SIGWINCH) {
        syncWinSize(state, -1, nullptr);
      }
      if (auto handler = state.sigVector.lookup(sigNum); handler != nullptr) {
        if (!kickSignalHandler(state, sigNum, handler)) {
          vmerror;
        }
        state.canHandleSignal = false; // disable signal handling in signal trampoline
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
          const Type &occurredType = state.typePool.get(state.stack.getThrownObject()->getTypeID());
          if (!entryType.isSameOrBaseTypeOf(occurredType)) {
            continue;
          }

          state.stack.updateIPByOffset(entry.dest);
          if (entryType.is(TYPE::ProcGuard_)) {
            /**
             * when exception entry indicate exception guard of sub-shell,
             * immediately break interpreter
             * (due to prevent signal handler interrupt and to load thrown object to stack)
             */
            return true;
          }
          state.stack.clearOperandsUntilGuard(StackGuardType::TRY, entry.guardLevel);
          state.stack.reclaimLocals(entry.localOffset, entry.localSize);
          if (entryType.is(TYPE::Throwable)) { // finally block
            state.stack.enterFinally();
          } else { // catch block
            state.stack.loadThrownObject();
            state.setExitStatus(0); // clear exit status when enter catch block
          }
          return true;
        }
      }
    } else if (state.stack.code() == &signalTrampoline) { // within signal trampoline
      state.canHandleSignal = true;
    } else if (state.stack.code() == &termHookTrampoline) {
      return true; // not propagate exception from termination handler
    }
  }
  return false;
}

Value VM::startEval(ARState &state, EvalOP op, ARError *dsError) {
  assert(state.stack.recDepth() > 0);
  if (state.stack.recDepth() == 1) {
    setLocaleSetting();
    setSignalSetting(state);
  }

  // run main loop
  Value value;
  if (mainLoop(state)) {
    value = state.stack.pop();
  }
  if (!hasFlag(op, EvalOP::PROPAGATE)) {
    handleUncaughtException(state, dsError);
  }
  return value;
}

void VM::callToplevel(ARState &state, const ObjPtr<FuncObject> &func, ARError *dsError) {
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

  startEval(state, EvalOP{}, dsError);
}

static unsigned int prepareArguments(VMState &state, Value &&recv, CallArgs &&args) {
  // push arguments
  const unsigned int size = args.first;
  state.reserve(size + 1);
  if (recv) {
    state.push(std::move(recv));
  }
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

Value VM::callCommand(ARState &state, ObjPtr<ArrayObject> &&argv) {
  GUARD_RECURSION(state);

  static const auto cmdTrampoline = initCmdTrampoline();

  Value ret;
  prepareArguments(state.stack, argv, {0, {}});
  if (windStackFrame(state, 1, 1, cmdTrampoline)) {
    ret = startEval(state, EvalOP::PROPAGATE, nullptr);
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
    ret = startEval(state, EvalOP::PROPAGATE, nullptr);
  }
  if (cast<FunctionType>(type).getReturnType().isVoidType()) {
    ret = Value(); // clear return value
  }
  return ret;
}

static NativeCode initBuiltinWrapper(unsigned char index) {
  NativeCode::ArrayType code;
  code[0] = static_cast<char>(OpCode::CALL_BUILTIN2);
  code[1] = static_cast<char>(index);
  code[2] = static_cast<char>(OpCode::RETURN);
  return NativeCode(code);
}

Value VM::callMethod(ARState &state, const MethodHandle &handle, Value &&recv, CallArgs &&args) {
  assert(handle.getParamSize() == args.first);

  GUARD_RECURSION(state);

  assert(handle.isConstructor() == !recv);
  const unsigned int actualParamSize =
      prepareArguments(state.stack, std::move(recv), std::move(args)) +
      (handle.isConstructor() ? 0 : 1);

  Value ret;
  const NativeCode wrapper = initBuiltinWrapper(handle.isNative() ? handle.getIndex() : 0);
  if (handle.isNative() ? windStackFrame(state, actualParamSize, actualParamSize, wrapper)
                        : prepareMethodCall(state, handle.getIndex(), actualParamSize)) {
    ret = startEval(state, EvalOP::PROPAGATE, nullptr);
  }
  if (handle.getReturnType().isVoidType()) {
    ret = Value(); // clear return value
  }
  return ret;
}

void VM::handleUncaughtException(ARState &state, ARError *dsError, bool inTermHook) {
  if (!state.hasError()) {
    return;
  }

  auto except = state.stack.takeThrownObject();
  auto &errorType = state.typePool.get(except->getTypeID());
  ARErrorKind kind = AR_ERROR_KIND_RUNTIME_ERROR;
  if (errorType.is(TYPE::ShellExit_)) {
    kind = AR_ERROR_KIND_EXIT;
  } else if (errorType.is(TYPE::AssertFail_)) {
    kind = AR_ERROR_KIND_ASSERTION_ERROR;
  }

  // print error message
  if (kind == AR_ERROR_KIND_RUNTIME_ERROR || kind == AR_ERROR_KIND_ASSERTION_ERROR ||
      state.has(RuntimeOption::TRACE_EXIT) || inTermHook || !except->getSuppressed().empty()) {
    except->printStackTrace(state, inTermHook ? ErrorObject::PrintOp::IGNORED_TERM
                                              : ErrorObject::PrintOp::UNCAUGHT);
  }

  if (dsError != nullptr) {
    auto &trace = except->getStackTrace();
    const unsigned int errorLineNum = getOccurredLineNum(trace);
    const char *sourceName = getOccurredSourceName(trace);

    *dsError = {.kind = kind,
                .fileName = *sourceName ? strdup(sourceName) : nullptr,
                .lineNum = errorLineNum,
                .chars = 0,
                .name = strdup(kind == AR_ERROR_KIND_RUNTIME_ERROR ? errorType.getName() : "")};
  }
  state.setExitStatus(except->getStatus());
}

void VM::prepareTermination(ARState &state) {
  assert(state.stack.recDepth() == 0);

  RecursionGuard guard(state);
  if (auto funcObj = state.getGlobal(state.termHookIndex); funcObj.kind() != ValueKind::INVALID) {
    if (kickTermHook(state, std::move(funcObj))) { // always success
      startEval(state, EvalOP{}, nullptr);
    }
  }

  if (state.has(RuntimeOption::HUP_EXIT)) {
    state.jobTable.send(SIGHUP);
  }
}

} // namespace arsh