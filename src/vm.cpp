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

#include "brace.h"
#include "logger.h"
#include "misc/files.h"
#include "misc/glob.hpp"
#include "misc/num_util.hpp"
#include "opcode.h"
#include "redir.h"
#include "vm.h"

// #####################
// ##     DSState     ##
// #####################

VMEvent DSState::eventDesc{};

SigSet DSState::pendingSigSet;

/**
 * if environmental variable SHLVL dose not exist, set 0.
 */
static int64_t getShellLevel() {
  char *shlvl = getenv(ENV_SHLVL);
  int64_t level = 0;
  if (shlvl != nullptr) {
    auto pair = convertToDecimal<int64_t>(shlvl);
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
  auto v = getCWD();
  const char *cwd = v ? v.get() : ".";

  const char *pwd = getenv(ENV_PWD);
  if (strcmp(cwd, ".") == 0 || !pwd || *pwd != '/' || !isSameFile(pwd, cwd)) {
    setenv(ENV_PWD, cwd, 1);
    pwd = cwd;
  }

  const char *oldpwd = getenv(ENV_OLDPWD);
  if (!oldpwd || *oldpwd != '/' || !S_ISDIR(getStMode(oldpwd))) {
    setenv(ENV_OLDPWD, pwd, 1);
  }
}

static void initEnv() {
  // set locale
  setLocaleSetting();

  // set environmental variables

  // update shell level
  if (auto shlvl = originalShellLevel(); shlvl < INT64_MAX) {
    setenv(ENV_SHLVL, std::to_string(shlvl + 1).c_str(), 1);
  } else {
    setenv(ENV_SHLVL, "1", 1);
  }

  if (struct passwd *pw = getpwuid(getuid()); likely(pw != nullptr)) {
    // set HOME
    setenv(ENV_HOME, pw->pw_dir, 1);

    // set LOGNAME
    setenv(ENV_LOGNAME, pw->pw_name, 1);

    // set USER
    setenv(ENV_USER, pw->pw_name, 1);
  } else {
#ifndef __EMSCRIPTEN__
    fatal_perror("getpwuid failed");
#endif
  }

  // set PWD/OLDPWD
  setPWDs();
}

static bool check_strftime_plus(timestamp time) {
  time_t t = timestampToTime(time);
  struct tm tm {};
  if (gmtime_r(&t, &tm)) {
    char buf[64];
    auto s = strftime_l(buf, std::size(buf), "%+", &tm, POSIX_LOCALE_C.get());
    auto ret = StringRef(buf, s);
    if (!ret.empty() && ret != "%+") {
      return true;
    }
  }
  return false;
}

DSState::DSState()
    : modLoader(this->sysConfig),
      emptyFDObj(toObjPtr<UnixFdObject>(DSValue::create<UnixFdObject>(-1))),
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

void DSState::updatePipeStatus(unsigned int size, const Proc *procs, bool mergeExitStatus) const {
  auto &obj = typeAs<ArrayObject>(this->getGlobal(BuiltinVarOffset::PIPESTATUS));
  obj.refValues().clear();
  obj.refValues().reserve(size + (mergeExitStatus ? 1 : 0));

  for (unsigned int i = 0; i < size; i++) {
    obj.append(DSValue::createInt(procs[i].exitStatus()));
  }
  if (mergeExitStatus) {
    obj.append(this->getGlobal(BuiltinVarOffset::EXIT_STATUS));
  }
  ASSERT_ARRAY_SIZE(obj);
}

namespace ydsh {

bool VM::checkCast(DSState &state, const DSType &targetType) {
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

const char *VM::loadEnv(DSState &state, bool hasDefault) {
  DSValue dValue;
  if (hasDefault) {
    dValue = state.stack.pop();
  }
  auto nameObj = state.stack.pop();
  auto nameRef = nameObj.asStrRef();
  assert(!nameRef.hasNullChar());
  const char *name = nameRef.data();
  const char *env = getenv(name);
  if (env == nullptr && hasDefault) {
    auto ref = dValue.asStrRef();
    if (ref.hasNullChar()) {
      std::string message = SET_ENV_ERROR;
      message += name;
      raiseError(state, TYPE::IllegalAccessError, std::move(message));
      return nullptr;
    }
    setenv(name, ref.data(), 1);
    env = getenv(name);
  }

  if (env == nullptr) {
    std::string message = UNDEF_ENV_ERROR;
    message += name;
    raiseError(state, TYPE::IllegalAccessError, std::move(message));
  }
  return env;
}

bool VM::storeEnv(DSState &state) {
  auto value = state.stack.pop();
  auto name = state.stack.pop();
  auto nameRef = name.asStrRef();
  auto valueRef = value.asStrRef();
  assert(!nameRef.hasNullChar());
  if (setenv(valueRef.hasNullChar() ? "" : nameRef.data(), valueRef.data(), 1) == 0) {
    return true;
  }
  std::string str = SET_ENV_ERROR;
  str += nameRef;
  raiseError(state, TYPE::IllegalAccessError, std::move(str));
  return false;
}

void VM::pushNewObject(DSState &state, const DSType &type) {
  DSValue value;
  switch (type.typeKind()) {
  case TypeKind::Array:
    value = DSValue::create<ArrayObject>(type);
    break;
  case TypeKind::Map:
    value = DSValue::create<MapObject>(type);
    break;
  case TypeKind::Tuple:
    value = DSValue::create<BaseObject>(cast<TupleType>(type));
    break;
  case TypeKind::Record:
    value = DSValue::create<BaseObject>(cast<RecordType>(type));
    break;
  default:
    value = DSValue::createDummy(type);
    break;
  }
  state.stack.push(std::move(value));
}

bool VM::prepareUserDefinedCommandCall(DSState &state, const DSCode &code, DSValue &&argvObj,
                                       DSValue &&redirConfig, const CmdCallAttr attr) {
  if (hasFlag(attr, CmdCallAttr::SET_VAR)) {
    // reset exit status
    state.setExitStatus(0);
  }

  // set parameter
  state.stack.reserve(UDC_PARAM_N);
  state.stack.push(DSValue::createNum(static_cast<unsigned int>(attr)));
  state.stack.push(std::move(redirConfig));
  state.stack.push(std::move(argvObj));

  const unsigned int stackTopOffset = UDC_PARAM_N + (hasFlag(attr, CmdCallAttr::CLOSURE) ? 1 : 0);
  if (unlikely(!windStackFrame(state, stackTopOffset, UDC_PARAM_N, code))) {
    return false;
  }

  if (hasFlag(attr, CmdCallAttr::SET_VAR)) { // set variable
    auto &argv = typeAs<ArrayObject>(state.stack.getLocal(UDC_PARAM_ARGV));
    auto cmdName = argv.takeFirst();
    state.stack.setLocal(UDC_PARAM_ARGV + 1, std::move(cmdName)); // 0
  }
  return true;
}

#define CODE(ctx) ((ctx).stack.code())
#define GET_CODE(ctx) (CODE(ctx)->getCode())
#define CONST_POOL(ctx) (cast<CompiledCode>(CODE(ctx))->getConstPool())

/* for substitution */

static bool readAsStr(DSState &state, int fd, DSValue &ret) {
  std::string str;
  while (true) {
    char buf[256];
    ssize_t readSize = read(fd, buf, std::size(buf));
    if (readSize == -1 && errno == EAGAIN) {
      continue;
    }
    if (readSize <= 0) {
      if (readSize < 0) {
        raiseSystemError(state, errno, CMD_SUB_ERROR);
        return false;
      }
      break;
    }
    if (unlikely(str.size() > StringObject::MAX_SIZE - readSize)) {
      raiseError(state, TYPE::OutOfRangeError, STRING_LIMIT_ERROR);
      return false;
    }
    str.append(buf, readSize);
  }

  // remove last newlines
  for (; !str.empty() && str.back() == '\n'; str.pop_back())
    ;

  ret = DSValue::createStr(std::move(str));
  return true;
}

static bool readAsStrArray(DSState &state, int fd, DSValue &ret) {
  auto ifsRef = state.getGlobal(BuiltinVarOffset::IFS).asStrRef();
  unsigned int skipCount = 1;
  std::string str;
  auto obj = DSValue::create<ArrayObject>(state.typePool.get(TYPE::StringArray));
  auto &array = typeAs<ArrayObject>(obj);

  while (true) {
    char buf[256];
    ssize_t readSize = read(fd, buf, std::size(buf));
    if (readSize == -1 && errno == EAGAIN) {
      continue;
    }
    if (readSize <= 0) {
      if (readSize < 0) {
        raiseSystemError(state, errno, CMD_SUB_ERROR);
        return false;
      }
      break;
    }

    for (ssize_t i = 0; i < readSize; i++) {
      char ch = buf[i];
      bool fieldSep = matchFieldSep(ifsRef, ch);
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
        if (unlikely(!array.append(state, DSValue::createStr(std::move(str))))) {
          return false;
        }
        str = "";
        skipCount = isSpace(ch) ? 2 : 1;
        continue;
      }
      if (unlikely(str.size() == StringObject::MAX_SIZE)) {
        raiseError(state, TYPE::OutOfRangeError, STRING_LIMIT_ERROR);
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
    if (unlikely(!array.append(state, DSValue::createStr(std::move(str))))) {
      return false;
    }
  }

  ret = std::move(obj);
  return true;
}

static ObjPtr<UnixFdObject> newFD(const DSState &st, int &fd) {
  if (fd < 0) {
    return st.emptyFDObj;
  }
  int v = fd;
  fd = -1;
  setCloseOnExec(v, true);
  auto value = DSValue::create<UnixFdObject>(v);
  return toObjPtr<UnixFdObject>(value);
}

bool VM::attachAsyncJob(DSState &state, DSValue &&desc, unsigned int procSize, const Proc *procs,
                        ForkKind forkKind, PipeSet &pipeSet, DSValue &ret) {
  switch (forkKind) {
  case ForkKind::NONE:
  case ForkKind::PIPE_FAIL: {
    auto entry = JobObject::create(procSize, procs, false, state.emptyFDObj, state.emptyFDObj,
                                   std::move(desc));
    // job termination
    auto waitOp = state.isJobControl() ? WaitOp::BLOCK_UNTRACED : WaitOp::BLOCKING;
    int status = entry->wait(waitOp);
    int errNum = errno;
    state.updatePipeStatus(entry->getProcSize(), entry->getProcs(), false);
    if (entry->isRunning()) {
      auto job = state.jobTable.attach(entry);
      if (job->getProcs()[0].is(Proc::State::STOPPED) && state.isJobControl()) {
        job->showInfo();
      }
    }
    state.tryToBeForeground();
    state.jobTable.waitForAny();
    state.setExitStatus(status);
    if (errNum != 0) {
      raiseSystemError(state, errNum, "wait failed");
      return false;
    }
    if (forkKind == ForkKind::PIPE_FAIL && hasFlag(state.runtimeOption, RuntimeOption::ERR_RAISE)) {
      for (unsigned int index = 0; index < entry->getProcSize(); index++) {
        auto &proc = entry->getProcs()[index];
        int s = proc.exitStatus();
        if (s != 0) {
          if (index < entry->getProcSize() - 1 && proc.signaled() && proc.asSigNum() == SIGPIPE) {
            if (!hasFlag(state.runtimeOption, RuntimeOption::FAIL_SIGPIPE)) {
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
    auto fdObj = newFD(state, fd);
    auto entry = JobObject::create(
        procSize, procs, false, forkKind == ForkKind::IN_PIPE ? fdObj : state.emptyFDObj,
        forkKind == ForkKind::OUT_PIPE ? fdObj : state.emptyFDObj, std::move(desc));
    state.jobTable.attach(entry, true);
    ret = fdObj;
    break;
  }
  case ForkKind::COPROC:
  case ForkKind::JOB:
  case ForkKind::DISOWN: {
    bool disown = forkKind == ForkKind::DISOWN;
    auto entry = JobObject::create(procSize, procs, false, newFD(state, pipeSet.in[WRITE_PIPE]),
                                   newFD(state, pipeSet.out[READ_PIPE]), std::move(desc));
    state.jobTable.attach(entry, disown);
    ret = DSValue(entry.get());
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

static Proc::Op resolveProcOp(const DSState &st, ForkKind kind) {
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

bool VM::forkAndEval(DSState &state, DSValue &&desc) {
  const auto forkKind = static_cast<ForkKind>(read8(GET_CODE(state), state.stack.pc()));
  const unsigned short offset = read16(GET_CODE(state), state.stack.pc() + 1);

  // set in/out pipe
  PipeSet pipeset(forkKind);
  const pid_t pgid = resolvePGID(state.isRootShell(), forkKind);
  const auto procOp = resolveProcOp(state, forkKind);
  auto proc = Proc::fork(state, pgid, procOp);
  if (proc.pid() > 0) { // parent process
    tryToClose(pipeset.in[READ_PIPE]);
    tryToClose(pipeset.out[WRITE_PIPE]);

    DSValue obj;

    switch (forkKind) {
    case ForkKind::STR:
    case ForkKind::ARRAY: { // always disable job control (so not change foreground process group)
      assert(!hasFlag(procOp, Proc::Op::JOB_CONTROL));
      tryToClose(pipeset.in[WRITE_PIPE]);
      bool ret = forkKind == ForkKind::STR ? readAsStr(state, pipeset.out[READ_PIPE], obj)
                                           : readAsStrArray(state, pipeset.out[READ_PIPE], obj);
      if (!ret || DSState::isInterrupted()) {
        /**
         * if read failed, not wait termination (always attach to job table)
         */
        tryToClose(pipeset.out[READ_PIPE]); // close read pipe after wait, due to prevent EPIPE
        state.jobTable.attach(
            JobObject::create(proc, state.emptyFDObj, state.emptyFDObj, std::move(desc)));

        if (ret && DSState::isInterrupted()) {
          raiseSystemError(state, EINTR, CMD_SUB_ERROR);
        }
        DSState::clearPendingSignal(SIGINT); // always clear SIGINT
        return false;
      }

      auto waitOp = state.isJobControl() ? WaitOp::BLOCK_UNTRACED : WaitOp::BLOCKING;
      int status = proc.wait(waitOp); // wait exit
      int errNum = errno;
      tryToClose(pipeset.out[READ_PIPE]); // close read pipe after wait, due to prevent EPIPE
      if (!proc.is(Proc::State::TERMINATED)) {
        state.jobTable.attach(
            JobObject::create(proc, state.emptyFDObj, state.emptyFDObj, std::move(desc)));
      }
      if (status < 0) {
        raiseSystemError(state, errNum, "wait failed");
        return false;
      }
      if (status != 0 && hasFlag(state.runtimeOption, RuntimeOption::ERR_RAISE)) {
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
      Proc procs[1] = {proc};
      if (!attachAsyncJob(state, std::move(desc), 1, procs, forkKind, pipeset, obj)) {
        return false;
      }
    }

    // push object
    if (obj) {
      state.stack.push(std::move(obj));
    }

    state.stack.pc() += offset - 1;
  } else if (proc.pid() == 0) { // child process
    pipeset.setupChildStdin(forkKind, state.isJobControl());
    pipeset.setupChildStdout();
    pipeset.closeAll();

    state.stack.pc() += 3;
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

static ResolvedCmd lookupUdcFromIndex(const DSState &state, unsigned short modId,
                                      unsigned int index) {
  const FuncObject *udcObj = nullptr;
  auto &v = state.getGlobal(index);
  if (v) {
    udcObj = &typeAs<FuncObject>(v);
  } else {
    return ResolvedCmd::illegalUdc();
  }

  auto &type = state.typePool.get(udcObj->getTypeID());
  if (type.isModType()) { // module object
    return ResolvedCmd::fromMod(cast<ModType>(type), modId);
  } else { // udc object
    assert(type.isVoidType());
    return ResolvedCmd::fromUdc(*udcObj);
  }
}

/**
 * lookup user-defined command from module.
 * if modType is null, lookup from root module
 * @param state
 * @param name
 * @param cmd
 * @param modType
 * if called from native code, may be null
 * @return
 */
static bool lookupUdc(const DSState &state, const ModType &modType, const char *name,
                      ResolvedCmd &cmd) {
  std::string fullname = toCmdFullName(name);
  auto handle = modType.lookupVisibleSymbolAtModule(state.typePool, fullname);
  if (handle) {
    cmd = lookupUdcFromIndex(state, handle->getModId(), handle->getIndex());
    return true;
  }
  return false;
}

ResolvedCmd CmdResolver::operator()(const DSState &state, const DSValue &name,
                                    const ModType *modType) const {
  const StringRef ref = name.asStrRef();

  // first, check user-defined command
  if (hasFlag(this->resolveOp, FROM_UDC)) {
    auto fqn = hasFlag(this->resolveOp, FROM_FQN_UDC) ? ref.find('\0') : StringRef::npos;
    const char *cmdName = ref.data();
    if (fqn != StringRef::npos) {
      auto ret = state.typePool.getType(cmdName);
      if (!ret || !ret->isModType() || ref.find('\0', fqn + 1) != StringRef::npos) {
        return ResolvedCmd::invalid();
      }
      modType = cast<ModType>(ret);
      cmdName = ref.begin() + fqn + 1;
    } else if (!modType) {
      modType = getCurRuntimeModule(state);
    }
    if (!modType) {
      modType = state.typePool.getModTypeById(1);
      assert(modType);
    }
    ResolvedCmd cmd{};
    if (lookupUdc(state, *modType, cmdName, cmd)) {
      return cmd;
    } else if (fqn != StringRef::npos) {
      return ResolvedCmd::invalid();
    }
  }

  // second, check builtin command
  if (hasFlag(this->resolveOp, FROM_BUILTIN)) {
    if (builtin_command_t bcmd = lookupBuiltinCommand(ref); bcmd != nullptr) {
      return ResolvedCmd::fromBuiltin(bcmd);
    }

    static std::pair<const char *, NativeCode> sb[] = {
        {"command", initCode(OpCode::BUILTIN_CMD)},
        {"call", initCode(OpCode::BUILTIN_CALL)},
        {"exec", initCode(OpCode::BUILTIN_EXEC)},
    };
    for (auto &e : sb) {
      if (ref == e.first) {
        return ResolvedCmd::fromBuiltin(e.second);
      }
    }
  }

  // resolve dynamic registered user-defined command
  if (hasFlag(this->resolveOp, FROM_DYNA_UDC)) {
    auto &map = typeAs<MapObject>(state.getGlobal(BuiltinVarOffset::DYNA_UDCS)).getValueMap();
    if (auto iter = map.find(name); iter != map.end()) {
      auto *obj = (*iter).second.get();
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

static void raiseCmdError(DSState &state, const char *cmdName, int errnum) {
  std::string str = EXEC_ERROR;
  str += toPrintable(cmdName);
  if (errnum == ENOENT) {
    str += ": command not found";
    raiseError(state, TYPE::SystemError, std::move(str), 127);
  } else if (errnum == EACCES) {
    str += ": ";
    str += strerror(errnum);
    raiseError(state, TYPE::SystemError, std::move(str), 126);
  } else {
    raiseSystemError(state, errnum, std::move(str));
  }
}

static void raiseInvalidCmdError(DSState &state, StringRef ref) {
  std::string message = "command contains null character: `";
  for (char ch : ref) {
    if (ch == '\0') {
      message += "\\x00";
    } else {
      message += ch;
    }
  }
  message += "'";
  raiseSystemError(state, EINVAL, std::move(message));
}

static void raiseCmdExecError(DSState &state, int64_t status) {
  std::string message = "command exits with non-zero status: `";
  message += std::to_string(status);
  message += "'";
  raiseError(state, TYPE::ExecError, std::move(message), status);
}

static DSValue toCmdDesc(char *const *argv) {
  std::string value;
  for (; *argv != nullptr; argv++) {
    if (!value.empty()) {
      value += " ";
    }
    if (!formatJobDesc(StringRef(*argv), value)) {
      break;
    }
  }
  return DSValue::createStr(std::move(value));
}

bool VM::forkAndExec(DSState &state, const char *filePath, char *const *argv,
                     DSValue &&redirConfig) {
  // setup self pipe
  int selfpipe[2];
  if (pipe(selfpipe) < 0) {
    fatal_perror("pipe creation error");
  }
  if (!setCloseOnExec(selfpipe[WRITE_PIPE], true)) {
    fatal_perror("fcntl error");
  }

  const pid_t pgid = resolvePGID(state.isRootShell(), ForkKind::NONE);
  const auto procOp = resolveProcOp(state, ForkKind::NONE);
  auto proc = Proc::fork(state, pgid, procOp);
  if (proc.pid() == -1) {
    raiseCmdError(state, argv[0], EAGAIN);
    return false;
  } else if (proc.pid() == 0) { // child
    close(selfpipe[READ_PIPE]);
    xexecve(filePath, argv, nullptr);

    int errnum = errno;
    ssize_t r = write(selfpipe[WRITE_PIPE], &errnum, sizeof(int));
    (void)r; // FIXME:
    exit(-1);
  } else { // parent process
    close(selfpipe[WRITE_PIPE]);
    redirConfig = nullptr; // restore redirconfig

    ssize_t readSize;
    int errnum = 0;
    while ((readSize = read(selfpipe[READ_PIPE], &errnum, sizeof(int))) == -1) {
      if (errno != EAGAIN && errno != EINTR) {
        break;
      }
    }
    close(selfpipe[READ_PIPE]);
    if (readSize > 0 && errnum == ENOENT) { // remove cached path
      state.pathCache.removePath(argv[0]);
    }

    // wait process or job termination
    int status;
    auto waitOp = state.isJobControl() ? WaitOp::BLOCK_UNTRACED : WaitOp::BLOCKING;
    status = proc.wait(waitOp);
    int errNum2 = errno;
    if (!proc.is(Proc::State::TERMINATED)) {
      auto job = state.jobTable.attach(
          JobObject::create(proc, state.emptyFDObj, state.emptyFDObj, toCmdDesc(argv)));
      if (proc.is(Proc::State::STOPPED) && state.isJobControl()) {
        job->showInfo();
      }
    }
    int ret = state.tryToBeForeground();
    LOG(DUMP_EXEC, "tryToBeForeground: %d, %s", ret, strerror(errno));
    state.jobTable.waitForAny();
    if (errnum != 0) {
      errNum2 = errnum;
    }
    if (errNum2 != 0) {
      raiseCmdError(state, argv[0], errNum2);
      return false;
    }
    pushExitStatus(state, status);
    return true;
  }
}

bool VM::prepareSubCommand(DSState &state, const ModType &modType, DSValue &&argvObj,
                           DSValue &&redirConfig) {
  auto &array = typeAs<ArrayObject>(argvObj);
  if (array.size() == 1) {
    ERROR(array, "require subcommand");
    pushExitStatus(state, 2);
    return true;
  }

  auto subcmd = array.getValues()[1].asStrRef();
  if (subcmd[0] == '_') {
    ERROR(array, "cannot resolve private subcommand: %s", toPrintable(subcmd).c_str());
    pushExitStatus(state, 1);
    return true;
  }

  std::string key = toCmdFullName(subcmd);
  auto handle = modType.lookup(state.typePool, key);
  if (!handle) {
    ERROR(array, "undefined subcommand: %s", toPrintable(subcmd).c_str());
    pushExitStatus(state, 2);
    return true;
  }
  auto &udc = typeAs<FuncObject>(state.getGlobal(handle->getIndex())).getCode();
  array.takeFirst();
  return prepareUserDefinedCommandCall(state, udc, std::move(argvObj), std::move(redirConfig),
                                       CmdCallAttr::SET_VAR);
}

bool VM::callCommand(DSState &state, CmdResolver resolver, DSValue &&argvObj, DSValue &&redirConfig,
                     CmdCallAttr attr) {
  auto &array = typeAs<ArrayObject>(argvObj);
  auto cmd = resolver(state, array.getValues()[0]);
  return callCommand(state, cmd, std::move(argvObj), std::move(redirConfig), attr);
}

static void traceCmd(const DSState &state, const ArrayObject &argv) {
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

bool VM::callCommand(DSState &state, const ResolvedCmd &cmd, DSValue &&argvObj,
                     DSValue &&redirConfig, CmdCallAttr attr) {
  auto &array = typeAs<ArrayObject>(argvObj);
  if (hasFlag(state.runtimeOption, RuntimeOption::XTRACE)) {
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
    int status = cmd.builtinCmd()(state, array);
    flushStdFD();
    if (state.hasError()) {
      return false;
    }
    if (status != 0 && hasFlag(attr, CmdCallAttr::RAISE) &&
        hasFlag(state.runtimeOption, RuntimeOption::ERR_RAISE)) {
      raiseCmdExecError(state, status);
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
    state.stack.push(DSValue(obj));
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
      bool ret = forkAndExec(state, cmd.filePath(), argv, std::move(redirConfig));
      if (ret) {
        int status = state.getMaskedExitStatus();
        if (status != 0 && hasFlag(attr, CmdCallAttr::RAISE) &&
            hasFlag(state.runtimeOption, RuntimeOption::ERR_RAISE)) {
          raiseCmdExecError(state, status);
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
      modType = state.typePool.getModTypeById(1);
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

bool VM::builtinCommand(DSState &state, DSValue &&argvObj, DSValue &&redir, CmdCallAttr attr) {
  auto &arrayObj = typeAs<ArrayObject>(argvObj);

  bool useDefaultPath = false;

  /**
   * if 0, ignore
   * if 1, show description
   * if 2, show detailed description
   */
  unsigned char showDesc = 0;

  GetOptState optState;
  for (int opt; (opt = optState(arrayObj, "pvVh")) != -1;) {
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
      pushExitStatus(state, showHelp(arrayObj));
      return true;
    default:
      int s = invalidOptionError(arrayObj, optState);
      pushExitStatus(state, s);
      return true;
    }
  }

  unsigned int index = optState.index;
  const unsigned int argc = arrayObj.getValues().size();
  if (index < argc) {
    if (showDesc == 0) { // execute command
      if (arrayObj.getValues()[1].asStrRef().hasNullChar()) {
        auto name = toPrintable(arrayObj.getValues()[1].asStrRef());
        ERROR(arrayObj, "contains null characters: %s", name.c_str());
        pushExitStatus(state, 1);
        return true;
      }

      auto &values = arrayObj.refValues();
      values.erase(values.begin(), values.begin() + index);

      auto resolve =
          CmdResolver(CmdResolver::NO_UDC,
                      useDefaultPath ? FilePathCache::USE_DEFAULT_PATH : FilePathCache::NON);
      return callCommand(state, resolve, std::move(argvObj), std::move(redir), attr);
    }

    // show command description
    unsigned int successCount = 0;
    for (; index < argc; index++) {
      const auto &cmdName = arrayObj.getValues()[index];
      const auto ref = cmdName.asStrRef();
      auto cmd = CmdResolver(CmdResolver::NO_FALLBACK | CmdResolver::FROM_FQN_UDC,
                             FilePathCache::DIRECT_SEARCH)(state, cmdName);
      switch (cmd.kind()) {
      case ResolvedCmd::USER_DEFINED:
      case ResolvedCmd::MODULE: {
        successCount++;
        fputs(toPrintable(ref).c_str(), stdout);
        if (showDesc == 2) {
          fputs(" is an user-defined command", stdout);
        }
        fputc('\n', stdout);
        continue;
      }
      case ResolvedCmd::BUILTIN_S:
      case ResolvedCmd::BUILTIN: {
        successCount++;
        fputs(ref.data(), stdout);
        if (showDesc == 2) {
          fputs(" is a shell builtin command", stdout);
        }
        fputc('\n', stdout);
        continue;
      }
      case ResolvedCmd::CMD_OBJ: {
        successCount++;
        fputs(toPrintable(ref).c_str(), stdout);
        if (showDesc == 2) {
          fputs(" is a dynamic registered command", stdout);
        }
        fputc('\n', stdout);
        continue;
      }
      case ResolvedCmd::EXTERNAL: {
        const char *path = cmd.filePath();
        if (path != nullptr && isExecutable(path)) {
          successCount++;
          const char *commandName = ref.data();
          if (showDesc == 1) {
            printf("%s\n", path);
          } else if (state.pathCache.isCached(commandName)) {
            printf("%s is hashed (%s)\n", commandName, path);
          } else {
            printf("%s is %s\n", commandName, path);
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
          ERROR(arrayObj, "%s: uninitialized", toPrintable(ref).c_str());
        } else {
          ERROR(arrayObj, "%s: not found", toPrintable(ref).c_str());
        }
      }
    }
    pushExitStatus(state, successCount > 0 ? 0 : 1);
    return true;
  }
  pushExitStatus(state, 0);
  return true;
}

void VM::builtinExec(DSState &state, DSValue &&array, DSValue &&redir) {
  auto &argvObj = typeAs<ArrayObject>(array);
  bool clearEnv = false;
  StringRef progName;
  GetOptState optState;

  if (redir) {
    typeAs<RedirObject>(redir).ignoreBackup();
  }

  for (int opt; (opt = optState(argvObj, "hca:")) != -1;) {
    switch (opt) {
    case 'c':
      clearEnv = true;
      break;
    case 'a':
      progName = optState.optArg;
      break;
    case 'h':
      pushExitStatus(state, showHelp(argvObj));
      return;
    default:
      int s = invalidOptionError(argvObj, optState);
      pushExitStatus(state, s);
      return;
    }
  }

  unsigned int index = optState.index;
  const unsigned int argc = argvObj.getValues().size();
  if (index < argc) { // exec
    if (argvObj.getValues()[index].asStrRef().hasNullChar()) {
      auto name = toPrintable(argvObj.getValues()[index].asStrRef());
      ERROR(argvObj, "contains null characters: %s", name.c_str());
      pushExitStatus(state, 1);
      return;
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
    PERROR(argvObj, "%s", argvObj.getValues()[index].asCStr());
    exit(1);
  }
  pushExitStatus(state, 0);
}

bool VM::callPipeline(DSState &state, DSValue &&desc, bool lastPipe, ForkKind forkKind) {
  /**
   * ls | grep .
   * ==> pipeSize == 1, procSize == 2
   *
   * if lastPipe is true,
   *
   * ls | { grep . ;}
   * ==> pipeSize == 1, procSize == 1
   */
  const unsigned int procSize = read8(GET_CODE(state), state.stack.pc()) - 1;
  const unsigned int pipeSize = procSize - (lastPipe ? 0 : 1);

  assert(pipeSize > 0);

  PipeSet pipeset(forkKind);
  int pipefds[pipeSize][2];
  initAllPipe(pipeSize, pipefds);

  // fork
  Proc childs[procSize];
  const auto procOp = resolveProcOp(state, forkKind);
  auto procOpRemain = procOp;
  unsetFlag(procOpRemain, Proc::Op::FOREGROUND); // remain process already foreground
  pid_t pgid = resolvePGID(state.isRootShell(), forkKind);
  Proc proc; // NOLINT

  unsigned int procIndex;
  for (procIndex = 0;
       procIndex < procSize &&
       (proc = Proc::fork(state, pgid, procIndex == 0 ? procOp : procOpRemain)).pid() > 0;
       procIndex++) {
    childs[procIndex] = proc;
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
      ::dup2(pipefds[procIndex][WRITE_PIPE], STDOUT_FILENO);
      pipeset.setupChildStdin(forkKind, state.isJobControl());
    }
    if (procIndex > 0 && procIndex < pipeSize) { // other process.
      ::dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
      ::dup2(pipefds[procIndex][WRITE_PIPE], STDOUT_FILENO);
    }
    if (procIndex == pipeSize && !lastPipe) { // last process
      ::dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
      pipeset.setupChildStdout();
    }
    pipeset.closeAll(); // FIXME: check error and force exit (not propagate error due to uncaught)
    closeAllPipe(pipeSize, pipefds);

    // set pc to next instruction
    state.stack.pc() += read16(GET_CODE(state), state.stack.pc() + 1 + procIndex * 2) - 1;
  } else if (procIndex == procSize) { // parent (last pipeline)
    if (lastPipe) {
      /**
       * in last pipe, save current stdin before call dup2
       */
      auto jobEntry = JobObject::create(procSize, childs, true, state.emptyFDObj, state.emptyFDObj,
                                        std::move(desc));
      state.jobTable.attach(jobEntry);
      ::dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
      closeAllPipe(pipeSize, pipefds);
      state.stack.push(DSValue::create<PipelineObject>(state, std::move(jobEntry)));
    } else {
      tryToClose(pipeset.in[READ_PIPE]);
      tryToClose(pipeset.out[WRITE_PIPE]);
      closeAllPipe(pipeSize, pipefds);
      DSValue obj;
      if (!attachAsyncJob(state, std::move(desc), procSize, childs, forkKind, pipeset, obj)) {
        return false;
      }
      if (obj) {
        state.stack.push(std::move(obj));
      }
    }

    // set pc to next instruction
    state.stack.pc() += read16(GET_CODE(state), state.stack.pc() + 1 + procIndex * 2) - 1;
  } else {
    // force terminate forked process.
    for (unsigned int i = 0; i < procIndex; i++) {
      childs[i].send(SIGKILL);
    }

    raiseSystemError(state, EAGAIN, "fork failed");
    return false;
  }
  return true;
}

class GlobIter {
private:
  const DSValue *cur;
  const char *ptr{nullptr};

public:
  explicit GlobIter(const DSValue *value) : cur(value) {
    if (this->cur->hasStrRef()) {
      this->ptr = this->cur->asStrRef().begin();
    }
  }

  char operator*() const { return this->ptr == nullptr ? '\0' : *this->ptr; }

  bool operator==(const GlobIter &other) const {
    return this->cur == other.cur && this->ptr == other.ptr;
  }

  bool operator!=(const GlobIter &other) const { return !(*this == other); }

  GlobIter &operator++() {
    if (this->ptr) {
      this->ptr++;
      if (*this->ptr == '\0') { // if StringRef iterator reaches null, increment DSValue iterator
        this->ptr = nullptr;
      }
    }
    if (!this->ptr) {
      this->cur++;
      if (this->cur->hasStrRef()) {
        this->ptr = this->cur->asStrRef().begin();
      }
    }
    return *this;
  }

  const DSValue *getIter() const { return this->cur; }
};

static const char *toMessagePrefix(TildeExpandStatus status) {
  switch (status) {
  case TildeExpandStatus::OK:
  case TildeExpandStatus::NO_TILDE:
    break;
  case TildeExpandStatus::NO_USER:
    return "no such user: ";
  case TildeExpandStatus::NO_DIR_STACK:
    break;
  case TildeExpandStatus::UNDEF_OR_EMPTY:
    return "undefined or empty: ";
  case TildeExpandStatus::INVALID_NUM:
    return "invalid number format: ";
  case TildeExpandStatus::OUT_OF_RANGE:
    return "directory stack index out of range, up to directory stack limit ";
  case TildeExpandStatus::HAS_NULL:
    return "expanded path contains null characters: ";
  }
  assert(false);
  return "";
}

static void raiseTildeError(DSState &state, const DirStackProvider &provider,
                            const std::string &path, TildeExpandStatus status) {
  assert(status != TildeExpandStatus::OK);
  StringRef ref = path;
  if (auto pos = ref.find("/"); pos != StringRef::npos) {
    ref = ref.substr(0, pos);
  }
  std::string str = toMessagePrefix(status);
  if (status == TildeExpandStatus::OUT_OF_RANGE) {
    str += "(";
    str += std::to_string(provider.size());
    str += "): ";
  }
  str += ref;
  raiseError(state, TYPE::TildeError, std::move(str));
}

class DefaultDirStackProvider : public DirStackProvider {
private:
  const DSState &state;
  CStrPtr cwd;

  const ArrayObject &dirStack() const {
    return typeAs<ArrayObject>(this->state.getGlobal(BuiltinVarOffset::DIRSTACK));
  }

public:
  explicit DefaultDirStackProvider(const DSState &state) : state(state) {}

  size_t size() const override {
    return std::min(this->dirStack().size(), SYS_LIMIT_DIRSTACK_SIZE);
  }

  StringRef get(size_t index) override {
    size_t size = this->size();
    if (index < size) {
      return this->dirStack().getValues()[index].asStrRef();
    } else if (index == size) {
      this->cwd = this->state.getWorkingDir();
      return this->cwd.get();
    }
    return "";
  }
};

struct DSValueGlobMeta {
  static bool isAny(GlobIter iter) {
    auto &v = *iter.getIter();
    return v.kind() == DSValueKind::EXPAND_META && v.asExpandMeta().first == ExpandMeta::ANY;
  }

  static bool isZeroOrMore(GlobIter iter) {
    auto &v = *iter.getIter();
    return v.kind() == DSValueKind::EXPAND_META &&
           v.asExpandMeta().first == ExpandMeta::ZERO_OR_MORE;
  }
};

static void raiseGlobbingError(DSState &state, const DSValue *begin, const DSValue *end,
                               const char *message) {
  std::string value = message;
  value += " `";
  for (; begin != end; ++begin) {
    auto &v = *begin;
    if (v.hasStrRef()) {
      for (auto &e : v.asStrRef()) {
        if (e != '\0') {
          value += e;
        } else {
          value += "\\x00";
        }
      }
    } else {
      value += v.toString();
    }
  }
  value += "'";
  raiseError(state, TYPE::GlobbingError, std::move(value));
}

bool VM::addGlobbingPath(DSState &state, ArrayObject &argv, const DSValue *begin,
                         const DSValue *end, bool tilde) {
  // check if glob path fragments have null character
  for (auto *iter = begin; iter != end; ++iter) {
    auto &v = *iter;
    if (v.hasStrRef()) {
      auto ref = v.asStrRef();
      if (unlikely(ref.hasNullChar())) {
        raiseGlobbingError(state, begin, end, "glob pattern has null characters");
        return false;
      }
    }
  }

  const unsigned int oldSize = argv.size();
  auto appender = [&](std::string &&path) {
    return argv.append(state, DSValue::createStr(std::move(path)));
  };
  GlobMatchOption option{};
  if (tilde) {
    setFlag(option, GlobMatchOption::TILDE);
  }
  if (hasFlag(state.runtimeOption, RuntimeOption::DOTGLOB)) {
    setFlag(option, GlobMatchOption::DOTGLOB);
  }
  if (hasFlag(state.runtimeOption, RuntimeOption::FASTGLOB)) {
    setFlag(option, GlobMatchOption::FASTGLOB);
  }
  auto matcher = createGlobMatcher<DSValueGlobMeta>(
      nullptr, GlobIter(begin), GlobIter(end), [] { return DSState::isInterrupted(); }, option);
  DefaultDirStackProvider provider(state);
  TildeExpandStatus tildeExpandStatus{};
  auto expander = [&provider, &tildeExpandStatus](std::string &path) {
    tildeExpandStatus = expandTilde(path, true, &provider);
    return tildeExpandStatus == TildeExpandStatus::OK;
  };
  auto ret = matcher(std::move(expander), appender);
  switch (ret) {
  case GlobMatchResult::MATCH:
  case GlobMatchResult::NOMATCH:
    if (ret == GlobMatchResult::MATCH || hasFlag(state.runtimeOption, RuntimeOption::NULLGLOB)) {
      argv.sortAsStrArray(oldSize);
      return true;
    } else {
      raiseGlobbingError(state, begin, end, "no matches for glob pattern");
      return false;
    }
  case GlobMatchResult::CANCELED:
    raiseSystemError(state, EINTR, "glob expansion is canceled");
    return false;
  case GlobMatchResult::TILDE_FAIL:
    assert(tildeExpandStatus != TildeExpandStatus::OK);
    raiseTildeError(state, provider, matcher.getBase(), tildeExpandStatus);
    return false;
  default:
    assert(ret == GlobMatchResult::LIMIT && state.hasError());
    return false;
  }
}

static DSValue concatPath(DSState &state, const DSValue *begin, const DSValue *end) {
  auto ret = DSValue::createStr();
  for (; begin != end; ++begin) {
    if (!ret.appendAsStr(state, begin->asStrRef())) {
      return {};
    }
  }
  return ret;
}

struct ExpandState {
  unsigned char index;
  unsigned char usedSize;
  unsigned char closeIndex;
  unsigned char braceId;

  struct Compare {
    bool operator()(const ExpandState &x, unsigned int y) const { return x.braceId < y; }

    bool operator()(unsigned int x, const ExpandState &y) const { return x < y.braceId; }
  };
};

static bool needGlob(const DSValue *begin, const DSValue *end) {
  for (; begin != end; ++begin) {
    auto &v = *begin;
    if (v.kind() == DSValueKind::EXPAND_META) {
      return true;
    }
  }
  return false;
}

/**
 *
 * @param cur
 * @param rangeState
 * (begin, end, step, (digits, kind))
 * @return
 */
static bool tryUpdate(int64_t &cur, const BaseObject &obj) {
  auto &nums = obj[3].asNumList();
  BraceRange range = {
      .begin = obj[0].asInt(),
      .end = obj[1].asInt(),
      .step = obj[2].asInt(),
      .digits = nums[0],
      .kind = static_cast<BraceRange::Kind>(nums[1]),
  };
  return tryUpdateSeqValue(cur, range);
}

bool VM::applyBraceExpansion(DSState &state, ArrayObject &argv, const DSValue *begin,
                             const DSValue *end, ExpandOp expandOp) {
  const unsigned int size = end - begin;
  assert(size <= UINT8_MAX);
  FlexBuffer<ExpandState> stack;
  auto values = std::make_unique<DSValue[]>(size + 1); // reserve sentinel
  FlexBuffer<int64_t> seqStack;
  unsigned int usedSize = 0;

  for (unsigned int i = 0; i < size; i++) {
    auto &v = begin[i];
    if (v.kind() == DSValueKind::EXPAND_META) {
      auto meta = v.asExpandMeta();
      switch (meta.first) {
      case ExpandMeta::BRACE_OPEN: {
        // find close index
        unsigned int closeIndex = i + 1;
        for (int level = 1; closeIndex < size; closeIndex++) {
          if (begin[closeIndex].kind() == DSValueKind::EXPAND_META) {
            auto next = begin[closeIndex].asExpandMeta();
            if (next.first == ExpandMeta::BRACE_CLOSE) {
              if (--level == 0) {
                break;
              }
            } else if (next.first == ExpandMeta::BRACE_OPEN) {
              level++;
            }
          }
        }
        stack.push_back(ExpandState{
            .index = static_cast<unsigned char>(i),
            .usedSize = static_cast<unsigned char>(usedSize),
            .closeIndex = static_cast<unsigned char>(closeIndex),
            .braceId = static_cast<unsigned char>(meta.second),
        });
        goto CONTINUE;
      }
      case ExpandMeta::BRACE_SEP:
      case ExpandMeta::BRACE_CLOSE: {
        auto iter =
            std::lower_bound(stack.begin(), stack.end(), meta.second, ExpandState::Compare());
        assert(iter != stack.end());
        (*iter).index = i;
        i = (*iter).closeIndex;
        goto CONTINUE;
      }
      case ExpandMeta::BRACE_TILDE:
        if (usedSize) {
          goto CONTINUE;
        }
        break;
      case ExpandMeta::BRACE_SEQ_OPEN: {
        i++;
        stack.push_back(ExpandState{
            .index = static_cast<unsigned char>(i + 1),
            .usedSize = static_cast<unsigned char>(usedSize),
            .closeIndex = static_cast<unsigned char>(i + 1),
            .braceId = static_cast<unsigned char>(meta.second),
        });

        auto &seq = typeAs<BaseObject>(begin[i]); // (begin, end, step, (digits, kind))
        assert(seq.getFieldSize() == 4);
        seqStack.push_back(seq[0].asInt());
        goto CONTINUE;
      }
      case ExpandMeta::BRACE_SEQ_CLOSE: {
        auto &nums = typeAs<BaseObject>(begin[i - 1])[3].asNumList();
        unsigned int digits = nums[0];
        auto kind = static_cast<BraceRange::Kind>(nums[1]);
        values[usedSize++] = DSValue::createStr(
            formatSeqValue(seqStack.back(), digits, kind == BraceRange::Kind::CHAR));
        goto CONTINUE;
      }
      default:
        break;
      }
    }
    values[usedSize++] = v;

  CONTINUE:
    if (i == size - 1) {
      if (unlikely(DSState::isInterrupted())) {
        raiseSystemError(state, EINTR, "brace expansion is canceled");
        return false;
      }

      values[usedSize] = DSValue(); // sentinel

      auto *vbegin = values.get();
      auto *vend = vbegin + usedSize;
      bool tilde = hasFlag(expandOp, ExpandOp::TILDE);
      if (!tilde && usedSize > 0 && vbegin->kind() == DSValueKind::EXPAND_META &&
          vbegin->asExpandMeta().first == ExpandMeta::BRACE_TILDE) {
        tilde = true;
        ++vbegin; // skip meta
      }

      if (needGlob(vbegin, vend)) {
        if (!addGlobbingPath(state, argv, vbegin, vend, tilde)) {
          return false;
        }
      } else {
        DSValue path = concatPath(state, vbegin, vend);
        if (tilde) {
          DefaultDirStackProvider dirStackProvider(state);
          std::string str = path.asStrRef().toString();
          if (auto s = expandTilde(str, true, &dirStackProvider); s != TildeExpandStatus::OK) {
            raiseTildeError(state, dirStackProvider, str, s);
            return false;
          }
          path = DSValue::createStr(std::move(str));
        }
        if (!path.asStrRef().empty()) { // skip empty string
          if (!argv.append(state, std::move(path))) {
            return false;
          }
        }
      }

      while (!stack.empty()) {
        unsigned int oldIndex = stack.back().index;
        auto &old = begin[oldIndex];
        assert(old.kind() == DSValueKind::EXPAND_META);
        auto meta = old.asExpandMeta().first;
        if (meta == ExpandMeta::BRACE_CLOSE) {
          stack.pop_back();
        } else if (meta == ExpandMeta::BRACE_SEQ_CLOSE) {
          if (tryUpdate(seqStack.back(), typeAs<BaseObject>(begin[oldIndex - 1]))) {
            i = oldIndex - 1;
            usedSize = stack.back().usedSize;
            break;
          } else {
            stack.pop_back();
            seqStack.pop_back();
          }
        } else {
          i = oldIndex;
          usedSize = stack.back().usedSize;
          break;
        }
      }
    }
  }
  return true;
}

bool VM::addExpandingPath(DSState &state, const unsigned int size, ExpandOp expandOp) {
  /**
   * stack layout
   *
   * ===========> stack grow
   * +------+-------+--------+     +--------+----------+
   * | argv | redir | param1 | ~~~ | paramN | paramN+1 |
   * +------+-------+--------+     +--------+----------+
   *
   * after evaluation
   * +------+-------+
   * | argv | redir |
   * +------+-------+
   */
  auto &argv = typeAs<ArrayObject>(state.stack.peekByOffset(size + 2));
  const auto *begin = &state.stack.peekByOffset(size);
  const auto *end = &state.stack.peek();
  bool ret;
  if (hasFlag(expandOp, ExpandOp::BRACE)) {
    ret = applyBraceExpansion(state, argv, begin, end, expandOp);
  } else {
    ret = addGlobbingPath(state, argv, begin, end, hasFlag(expandOp, ExpandOp::TILDE));
  }

  if (ret) { // pop operands
    for (unsigned int i = 0; i <= size; i++) {
      state.stack.popNoReturn();
    }
  }
  return ret;
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

bool VM::kickSignalHandler(DSState &state, int sigNum, DSValue &&func) {
  state.stack.reserve(3);
  state.stack.push(state.getGlobal(BuiltinVarOffset::EXIT_STATUS));
  state.stack.push(std::move(func));
  state.stack.push(DSValue::createSig(sigNum));

  return windStackFrame(state, 3, 3, signalTrampoline);
}

bool VM::checkVMEvent(DSState &state) {
  if (hasFlag(DSState::eventDesc, VMEvent::SIGNAL) && !hasFlag(DSState::eventDesc, VMEvent::MASK)) {
    SignalGuard guard;
    int sigNum = DSState::popPendingSignal();
    if (sigNum == SIGCHLD) {
      state.jobTable.waitForAny();
    } else if (auto handler = state.sigVector.lookup(sigNum); handler != nullptr) {
      setFlag(DSState::eventDesc, VMEvent::MASK);
      if (!kickSignalHandler(state, sigNum, handler)) {
        unsetFlag(DSState::eventDesc, VMEvent::MASK);
        return false;
      }
    }
  }

  if (state.hook != nullptr) {
    assert(hasFlag(DSState::eventDesc, VMEvent::HOOK));
    auto op = static_cast<OpCode>(GET_CODE(state)[state.stack.pc()]);
    state.hook->vmFetchHook(state, op);
  }
  return true;
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

bool VM::mainLoop(DSState &state) {
  OpCode op;
  while (true) {
    if (unlikely(!empty(DSState::eventDesc))) {
      TRY(checkVMEvent(state));
    }

    // fetch next opcode
    op = static_cast<OpCode>(GET_CODE(state)[state.stack.pc()++]);

    // dispatch instruction
    vmdispatch(op) {
      vmcase(HALT) { return true; }
      vmcase(ASSERT_ENABLED) {
        unsigned short offset = read16(GET_CODE(state), state.stack.pc());
        if (hasFlag(state.runtimeOption, RuntimeOption::ASSERT)) {
          state.stack.pc() += 2;
        } else {
          state.stack.pc() += offset - 1;
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
        unsigned int v = read24(GET_CODE(state), state.stack.pc());
        state.stack.pc() += 3;

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
        unsigned int v = read24(GET_CODE(state), state.stack.pc());
        state.stack.pc() += 3;

        auto &targetType = state.typePool.get(v);
        auto value = state.stack.pop();
        bool ret = instanceOf(state.typePool, value, targetType);
        state.stack.push(DSValue::createBool(ret));
        vmnext;
      }
      vmcase(CHECK_CAST) {
        unsigned int v = read24(GET_CODE(state), state.stack.pc());
        state.stack.pc() += 3;
        TRY(checkCast(state, state.typePool.get(v)));
        vmnext;
      }
      vmcase(CHECK_CAST_OPT) {
        unsigned int v = read24(GET_CODE(state), state.stack.pc());
        state.stack.pc() += 3;
        const auto &targetType = state.typePool.get(v);
        if (!instanceOf(state.typePool, state.stack.peek(), targetType)) {
          state.stack.popNoReturn();
          state.stack.push(DSValue::createInvalid());
        }
        vmnext;
      }
      vmcase(PUSH_NULL) {
        state.stack.push(nullptr);
        vmnext;
      }
      vmcase(PUSH_TRUE) {
        state.stack.push(DSValue::createBool(true));
        vmnext;
      }
      vmcase(PUSH_FALSE) {
        state.stack.push(DSValue::createBool(false));
        vmnext;
      }
      vmcase(PUSH_SIG) {
        unsigned int value = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
        state.stack.push(DSValue::createSig(static_cast<int>(value)));
        vmnext;
      }
      vmcase(PUSH_INT) {
        unsigned int value = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
        state.stack.push(DSValue::createInt(value));
        vmnext;
      }
      vmcase(PUSH_STR0) {
        state.stack.push(DSValue::createStr());
        vmnext;
      }
      vmcase(PUSH_STR1) vmcase(PUSH_STR2) vmcase(PUSH_STR3) {
        char data[3];
        unsigned int size = op == OpCode::PUSH_STR1 ? 1 : op == OpCode::PUSH_STR2 ? 2 : 3;
        for (unsigned int i = 0; i < size; i++) {
          data[i] = static_cast<char>(read8(GET_CODE(state), state.stack.pc()));
          state.stack.pc()++;
        }
        state.stack.push(DSValue::createStr(StringRef(data, size)));
        vmnext;
      }
      vmcase(PUSH_META) {
        unsigned int meta = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
        unsigned int v = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
        state.stack.push(DSValue::createExpandMeta(static_cast<ExpandMeta>(meta), v));
        vmnext;
      }
      vmcase(PUSH_INVALID) {
        state.stack.push(DSValue::createInvalid());
        vmnext;
      }
      vmcase(LOAD_CONST) {
        unsigned char index = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
        state.stack.push(CONST_POOL(state)[index]);
        vmnext;
      }
      vmcase(LOAD_CONST_W) {
        unsigned short index = read16(GET_CODE(state), state.stack.pc());
        state.stack.pc() += 2;
        state.stack.push(CONST_POOL(state)[index]);
        vmnext;
      }
      vmcase(LOAD_CONST_T) {
        unsigned int index = read24(GET_CODE(state), state.stack.pc());
        state.stack.pc() += 3;
        state.stack.push(CONST_POOL(state)[index]);
        vmnext;
      }
      vmcase(LOAD_GLOBAL) {
        unsigned short index = read16(GET_CODE(state), state.stack.pc());
        state.stack.pc() += 2;
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
        unsigned short index = read16(GET_CODE(state), state.stack.pc());
        state.stack.pc() += 2;
        state.setGlobal(index, state.stack.pop());
        vmnext;
      }
      vmcase(LOAD_LOCAL) {
        unsigned char index = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
        state.stack.loadLocal(index);
        vmnext;
      }
      vmcase(STORE_LOCAL) {
        unsigned char index = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
        state.stack.storeLocal(index);
        vmnext;
      }
      vmcase(LOAD_FIELD) {
        unsigned short index = read16(GET_CODE(state), state.stack.pc());
        state.stack.pc() += 2;
        state.stack.loadField(index);
        vmnext;
      }
      vmcase(STORE_FIELD) {
        unsigned short index = read16(GET_CODE(state), state.stack.pc());
        state.stack.pc() += 2;
        state.stack.storeField(index);
        vmnext;
      }
      vmcase(IMPORT_ENV) {
        unsigned char b = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
        TRY(loadEnv(state, b > 0));
        vmnext;
      }
      vmcase(LOAD_ENV) {
        const char *value = loadEnv(state, false);
        TRY(value);
        state.stack.push(DSValue::createStr(value));
        vmnext;
      }
      vmcase(STORE_ENV) {
        TRY(storeEnv(state));
        vmnext;
      }
      vmcase(NEW_ENV_CTX) {
        state.stack.push(DSValue::create<EnvCtxObject>(state));
        vmnext;
      }
      vmcase(ADD2ENV_CTX) {
        auto value = state.stack.pop();
        auto name = state.stack.pop();
        typeAs<EnvCtxObject>(state.stack.peek()).setAndSaveEnv(std::move(name), std::move(value));
        vmnext;
      }
      vmcase(NEW_TIMER) {
        state.stack.push(DSValue::create<TimerObject>());
        vmnext;
      }
      vmcase(BOX_LOCAL) {
        unsigned char index = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
        auto v = state.stack.getLocal(index);
        auto boxed = DSValue::create<BoxObject>(std::move(v));
        state.stack.setLocal(index, std::move(boxed));
        vmnext;
      }
      vmcase(LOAD_BOXED) {
        unsigned char index = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
        auto &boxed = state.stack.getLocal(index);
        state.stack.push(typeAs<BoxObject>(boxed).getValue());
        vmnext;
      }
      vmcase(STORE_BOXED) {
        unsigned char index = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
        auto &boxed = state.stack.getLocal(index);
        auto v = state.stack.pop();
        typeAs<BoxObject>(boxed).setValue(std::move(v));
        vmnext;
      }
      vmcase(NEW_CLOSURE) {
        const unsigned int paramSize = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
        auto funcObj = toObjPtr<FuncObject>(state.stack.peekByOffset(paramSize));
        const DSValue *values = &state.stack.peekByOffset(paramSize) + 1;
        auto value = DSValue::create<ClosureObject>(std::move(funcObj), paramSize, values);
        for (unsigned int i = 0; i <= paramSize; i++) {
          state.stack.popNoReturn();
        }
        state.stack.push(std::move(value));
        vmnext;
      }
      vmcase(LOAD_UPVAR) vmcase(LOAD_RAW_UPVAR) {
        unsigned char index = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
        auto &closure = state.stack.getCurrentClosure();
        auto slot = closure[index];
        if (op == OpCode::LOAD_UPVAR && slot.isObject() && isa<BoxObject>(slot.get())) {
          slot = typeAs<BoxObject>(slot).getValue(); // unbox
        }
        state.stack.push(std::move(slot));
        vmnext;
      }
      vmcase(STORE_UPVAR) {
        unsigned char index = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
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
      vmcase(CONCAT) vmcase(APPEND) {
        const bool selfConcat = op == OpCode::APPEND;
        auto right = state.stack.pop();
        auto left = state.stack.pop();
        TRY(concatAsStr(state, left, right, selfConcat));
        state.stack.push(std::move(left));
        vmnext;
      }
      vmcase(APPEND_ARRAY) {
        DSValue v = state.stack.pop();
        TRY(typeAs<ArrayObject>(state.stack.peek()).append(state, std::move(v)));
        vmnext;
      }
      vmcase(APPEND_MAP) {
        DSValue value = state.stack.pop();
        DSValue key = state.stack.pop();
        typeAs<MapObject>(state.stack.peek()).set(std::move(key), std::move(value));
        vmnext;
      }
      vmcase(ITER_HAS_NEXT) {
        unsigned short offset = read16(GET_CODE(state), state.stack.pc());
        if (state.stack.peek()) { // not empty
          state.stack.pc() += 2;
        } else {
          state.stack.clearOperandsUntilGuard(StackGuardType::LOOP);
          state.stack.pc() += offset - 1;
        }
        vmnext;
      }
      vmcase(MAP_ITER_NEXT) {
        unsigned short offset = read16(GET_CODE(state), state.stack.pc());
        auto mapIter = state.stack.pop();
        if (typeAs<MapIterObject>(mapIter).hasNext()) {
          auto [k, v] = typeAs<MapIterObject>(mapIter).nextUnpack();
          state.stack.push(std::move(v));
          state.stack.push(std::move(k));
          state.stack.pc() += 2;
        } else {
          state.stack.clearOperandsUntilGuard(StackGuardType::LOOP);
          state.stack.pc() += offset - 1;
        }
        vmnext;
      }
      vmcase(NEW) {
        unsigned int v = read24(GET_CODE(state), state.stack.pc());
        state.stack.pc() += 3;
        auto &type = state.typePool.get(v);
        pushNewObject(state, type);
        vmnext;
      }
      vmcase(INIT_FIELDS) {
        unsigned int offset = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
        unsigned int size = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;

        auto &obj = typeAs<BaseObject>(state.stack.peek());
        assert(obj.getFieldSize() == size);
        for (unsigned int i = 0; i < size; i++) {
          obj[i] = state.stack.getLocal(offset + i);
        }
        vmnext;
      }
      vmcase(CALL_FUNC) {
        unsigned int paramSize = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
        TRY(prepareFuncCall(state, paramSize));
        vmnext;
      }
      vmcase(CALL_METHOD) {
        unsigned int paramSize = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
        unsigned short index = read16(GET_CODE(state), state.stack.pc());
        state.stack.pc() += 2;
        TRY(prepareMethodCall(state, index, paramSize));
        vmnext;
      }
      vmcase(CALL_BUILTIN) {
        unsigned int paramSize = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
        unsigned int index = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
        auto old = state.stack.nativeWind(paramSize);
        auto ret = nativeFuncPtrTable()[index](state);
        state.stack.nativeUnwind(old);
        TRY(!state.hasError());
        state.stack.push(std::move(ret));
        vmnext;
      }
      vmcase(RETURN) {
        DSValue v = state.stack.pop();
        state.stack.unwind();
        state.stack.push(std::move(v));
        if (state.stack.checkVMReturn()) {
          return true;
        }
        vmnext;
      }
      vmcase(RETURN_UDC) {
        auto attr = static_cast<CmdCallAttr>(state.stack.getLocal(UDC_PARAM_ATTR).asNum());
        auto status = state.stack.pop().asInt();
        state.stack.unwind();
        if (status != 0 && hasFlag(attr, CmdCallAttr::RAISE) &&
            hasFlag(state.runtimeOption, RuntimeOption::ERR_RAISE)) {
          raiseCmdExecError(state, status);
          vmerror;
        }
        pushExitStatus(state, status);
        assert(!state.stack.checkVMReturn());
        vmnext;
      }
      vmcase(RETURN_SIG) {
        auto v = state.stack.getLocal(0); // old exit status
        unsetFlag(DSState::eventDesc, VMEvent::MASK);
        state.setGlobal(BuiltinVarOffset::EXIT_STATUS, std::move(v));
        state.stack.unwind();
        assert(!state.stack.checkVMReturn());
        vmnext;
      }
      vmcase(BRANCH) {
        unsigned short offset = read16(GET_CODE(state), state.stack.pc());
        if (state.stack.pop().asBool()) {
          state.stack.pc() += 2;
        } else {
          state.stack.pc() += offset - 1;
        }
        vmnext;
      }
      vmcase(BRANCH_NOT) {
        unsigned short offset = read16(GET_CODE(state), state.stack.pc());
        if (!state.stack.pop().asBool()) {
          state.stack.pc() += 2;
        } else {
          state.stack.pc() += offset - 1;
        }
        vmnext;
      }
      vmcase(IF_INVALID) {
        unsigned short offset = read16(GET_CODE(state), state.stack.pc());
        if (state.stack.peek().isInvalid()) {
          state.stack.popNoReturn();
          state.stack.pc() += offset - 1;
        } else {
          state.stack.pc() += 2;
        }
        vmnext;
      }
      vmcase(IF_NOT_INVALID) {
        unsigned short offset = read16(GET_CODE(state), state.stack.pc());
        if (state.stack.peek().isInvalid()) {
          state.stack.popNoReturn();
          state.stack.pc() += 2;
        } else {
          state.stack.pc() += offset - 1;
        }
        vmnext;
      }
      vmcase(GOTO) {
        unsigned int index = read32(GET_CODE(state), state.stack.pc());
        state.stack.pc() = index;
        vmnext;
      }
      vmcase(JUMP_LOOP) {
        unsigned int index = read32(GET_CODE(state), state.stack.pc());
        state.stack.pc() = index;
        state.stack.clearOperandsUntilGuard(StackGuardType::LOOP);
        vmnext;
      }
      vmcase(JUMP_LOOP_V) {
        unsigned int index = read32(GET_CODE(state), state.stack.pc());
        state.stack.pc() = index;
        auto v = state.stack.pop();
        state.stack.clearOperandsUntilGuard(StackGuardType::LOOP);
        state.stack.push(std::move(v));
        vmnext;
      }
      vmcase(LOOP_GUARD) {
        state.stack.push(DSValue::createStackGuard(StackGuardType::LOOP));
        vmnext;
      }
      vmcase(JUMP_TRY) {
        unsigned int index = read32(GET_CODE(state), state.stack.pc());
        state.stack.pc() = index;
        state.stack.clearOperandsUntilGuard(StackGuardType::TRY);
        vmnext;
      }
      vmcase(JUMP_TRY_V) {
        unsigned int index = read32(GET_CODE(state), state.stack.pc());
        state.stack.pc() = index;
        auto v = state.stack.pop();
        state.stack.clearOperandsUntilGuard(StackGuardType::TRY);
        state.stack.push(std::move(v));
        vmnext;
      }
      vmcase(TRY_GUARD) {
        unsigned int level = read32(GET_CODE(state), state.stack.pc());
        state.stack.pc() += 4;
        state.stack.push(DSValue::createStackGuard(StackGuardType::TRY, level));
        vmnext;
      }
      vmcase(TRY_GUARD0) {
        state.stack.push(DSValue::createStackGuard(StackGuardType::TRY, 1));
        vmnext;
      }
      vmcase(TRY_GUARD1) {
        unsigned int level = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc() += 1;
        state.stack.push(DSValue::createStackGuard(StackGuardType::TRY, level));
        vmnext;
      }
      vmcase(THROW) {
        auto obj = state.stack.pop();
        state.throwObject(toObjPtr<ErrorObject>(obj));
        vmerror;
      }
      vmcase(ENTER_FINALLY) {
        unsigned int index = read32(GET_CODE(state), state.stack.pc());
        const unsigned int savedIndex = state.stack.pc() + 4;
        state.stack.pc() = index;
        state.stack.push(state.getGlobal(BuiltinVarOffset::EXIT_STATUS));
        state.stack.enterFinally(index, savedIndex);
        vmnext;
      }
      vmcase(EXIT_FINALLY) {
        auto v = state.stack.pop();
        assert(v.kind() == DSValueKind::INT);
        state.setGlobal(BuiltinVarOffset::EXIT_STATUS, std::move(v));
        auto entry = state.stack.exitFinally();
        if (entry.hasError()) {
          state.stack.setErrorObj(entry.asError());
          vmerror;
        } else {
          state.stack.pc() = entry.asRetAddr();
          vmnext;
        }
      }
      vmcase(LOOKUP_HASH) {
        auto key = state.stack.pop();
        auto map = state.stack.pop();
        if (!key.isInvalid()) {
          auto &valueMap = typeAs<MapObject>(map).getValueMap();
          auto iter = valueMap.find(key);
          if (iter != valueMap.end()) {
            assert(iter->second.kind() == DSValueKind::NUMBER);
            unsigned int index = iter->second.asNum();
            state.stack.pc() = index;
          }
        }
        vmnext;
      }
      vmcase(REF_EQ) {
        auto v1 = state.stack.pop();
        auto v2 = state.stack.pop();
        state.stack.push(DSValue::createBool(v1 == v2));
        vmnext;
      }
      vmcase(REF_NE) {
        auto v1 = state.stack.pop();
        auto v2 = state.stack.pop();
        state.stack.push(DSValue::createBool(v1 != v2));
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
          unsigned char v = read8(GET_CODE(state), state.stack.pc());
          state.stack.pc()++;
          kind = static_cast<ForkKind>(v);
        }
        auto desc = state.stack.pop();
        TRY(callPipeline(state, std::move(desc), lastPipe, kind));
        vmnext;
      }
      vmcase(EXPAND_TILDE) {
        std::string str = state.stack.pop().asStrRef().toString();
        DefaultDirStackProvider dirStackProvider(state);
        if (auto s = expandTilde(str, true, &dirStackProvider); s != TildeExpandStatus::OK) {
          raiseTildeError(state, dirStackProvider, str, s);
          vmerror;
        }
        state.stack.push(DSValue::createStr(std::move(str)));
        vmnext;
      }
      vmcase(NEW_CMD) {
        auto v = state.stack.pop();
        auto obj = DSValue::create<ArrayObject>(state.typePool.get(TYPE::StringArray));
        auto &argv = typeAs<ArrayObject>(obj);
        argv.append(std::move(v));
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
        unsigned int size = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
        auto opt = static_cast<ExpandOp>(read8(GET_CODE(state), state.stack.pc()));
        state.stack.pc()++;
        TRY(addExpandingPath(state, size, opt));
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
        auto argv = state.stack.pop();

        TRY(callCommand(state, CmdResolver(CmdResolver::NO_STATIC_UDC, FilePathCache::NON),
                        std::move(argv), std::move(redir), attr));
        vmnext;
      }
      vmcase(CALL_UDC) vmcase(CALL_UDC_SILENT) {
        unsigned short index = read16(GET_CODE(state), state.stack.pc());
        state.stack.pc() += 2;

        CmdCallAttr attr = CmdCallAttr::RAISE | CmdCallAttr::NEED_FORK;
        if (op == OpCode::CALL_UDC_SILENT) {
          unsetFlag(attr, CmdCallAttr::RAISE);
        }

        auto redir = state.stack.pop();
        auto argv = state.stack.pop();

        ResolvedCmd cmd = lookupUdcFromIndex(state, 0, index);
        TRY(callCommand(state, cmd, std::move(argv), std::move(redir), attr));
        vmnext;
      }
      vmcase(CALL_CMD_COMMON) {
        auto redir = state.stack.pop();
        auto argv = state.stack.pop();

        TRY(callCommand(state, CmdResolver(), std::move(argv), std::move(redir),
                        CmdCallAttr::NEED_FORK));
        vmnext;
      }
      vmcase(CALL_CMD_OBJ) {
        auto redir = state.stack.pop();
        auto argv = state.stack.pop();
        auto obj = state.stack.pop();
        auto cmd = ResolvedCmd::fromCmdObj(obj.get());
        if (auto &argvObj = typeAs<ArrayObject>(argv); argvObj.size() == 0) {
          argvObj.refValues().push_back(DSValue::createStr()); // add dummy
        }
        TRY(callCommand(state, cmd, std::move(argv), std::move(redir), CmdCallAttr{}));
        vmnext;
      }
      vmcase(BUILTIN_CMD) {
        auto v = state.stack.getLocal(UDC_PARAM_ATTR).asNum();
        auto attr = static_cast<CmdCallAttr>(v);
        DSValue redir = state.stack.getLocal(UDC_PARAM_REDIR);
        DSValue argv = state.stack.getLocal(UDC_PARAM_ARGV);
        bool ret = builtinCommand(state, std::move(argv), std::move(redir), attr);
        flushStdFD();
        TRY(ret);
        vmnext;
      }
      vmcase(BUILTIN_CALL) {
        auto v = state.stack.getLocal(UDC_PARAM_ATTR).asNum();
        auto attr = static_cast<CmdCallAttr>(v);
        DSValue redir = state.stack.getLocal(UDC_PARAM_REDIR);
        DSValue argv = state.stack.getLocal(UDC_PARAM_ARGV);

        typeAs<ArrayObject>(argv).takeFirst();
        auto &array = typeAs<ArrayObject>(argv);
        if (!array.getValues().empty()) {
          TRY(callCommand(state,
                          CmdResolver(CmdResolver::FROM_DEFAULT_WITH_FQN, FilePathCache::NON),
                          std::move(argv), std::move(redir), attr));
        } else {
          pushExitStatus(state, 0);
        }
        vmnext;
      }
      vmcase(BUILTIN_EXEC) {
        DSValue redir = state.stack.getLocal(UDC_PARAM_REDIR);
        DSValue argv = state.stack.getLocal(UDC_PARAM_ARGV);
        builtinExec(state, std::move(argv), std::move(redir));
        vmnext;
      }
      vmcase(NEW_REDIR) {
        state.stack.push(DSValue::create<RedirObject>());
        vmnext;
      }
      vmcase(ADD_REDIR_OP0) vmcase(ADD_REDIR_OP1) vmcase(ADD_REDIR_OP2) {
        const int newFd = static_cast<int>(op) - static_cast<int>(OpCode::ADD_REDIR_OP0);
        const auto redirOp = static_cast<RedirOp>(read8(GET_CODE(state), state.stack.pc()));
        state.stack.pc()++;
        auto value = state.stack.pop();
        typeAs<RedirObject>(state.stack.peek()).addEntry(std::move(value), redirOp, newFd);
        vmnext;
      }
      vmcase(DO_REDIR) {
        TRY(typeAs<RedirObject>(state.stack.peek()).redirect(state));
        vmnext;
      }
      vmcase(LOAD_CUR_MOD) {
        unsigned short modId = cast<CompiledCode>(CODE(state))->getBelongedModId();
        auto &entry = state.modLoader[modId];
        auto &modType = cast<ModType>(state.typePool.get(entry.second.getTypeId()));
        unsigned int index = modType.getIndex();
        state.stack.push(state.getGlobal(index));
        vmnext;
      }
      vmcase(RAND) {
        state.stack.push(DSValue::createInt(state.getRng().nextInt64()));
        vmnext;
      }
      vmcase(GET_SECOND) {
        auto now = getCurrentTimestamp();
        auto diff = now - state.baseTime;
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(diff);
        int64_t v = state.getGlobal(BuiltinVarOffset::SECONDS).asInt();
        v += sec.count();
        state.stack.push(DSValue::createInt(v));
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
        auto v = DSValue::createStr();
        assert(pos > 0 && static_cast<uint64_t>(pos) <= ArrayObject::MAX_SIZE);
        unsigned int index = static_cast<uint64_t>(pos) - 1;
        if (index < typeAs<ArrayObject>(args).size()) {
          v = typeAs<ArrayObject>(args).getValues()[index];
        }
        state.stack.push(std::move(v));
        vmnext;
      }
      vmcase(UNWRAP) {
        if (state.stack.peek().kind() == DSValueKind::INVALID) {
          raiseError(state, TYPE::UnwrappingError, "invalid value");
          vmerror;
        }
        vmnext;
      }
      vmcase(CHECK_INVALID) {
        bool b = state.stack.pop().kind() != DSValueKind::INVALID;
        state.stack.push(DSValue::createBool(b));
        vmnext;
      }
      vmcase(RECLAIM_LOCAL) {
        unsigned char offset = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;
        unsigned char size = read8(GET_CODE(state), state.stack.pc());
        state.stack.pc()++;

        state.stack.reclaimLocals(offset, size);
        vmnext;
      }
    }

  EXCEPT:
    assert(state.hasError());
    if (!handleException(state)) {
      return false;
    }
  }
}

void VM::rethrowFromFinally(DSState &state) {
  auto entry = state.stack.exitFinally();
  if (entry.hasError()) { // ignore current exception and rethrow
    auto curError = state.stack.takeThrownObject();
    curError->printStackTrace(state, ErrorObject::PrintOp::IGNORED);
    state.stack.setErrorObj(entry.asError());
  }
}

bool VM::handleException(DSState &state) {
  if (unlikely(state.hook != nullptr)) {
    state.hook->vmThrowHook(state);
  }

  for (; !state.stack.checkVMReturn(); state.stack.unwind()) {
    if (!CODE(state)->is(CodeKind::NATIVE)) {
      auto *cc = cast<CompiledCode>(CODE(state));

      // search exception entry
      const unsigned int occurredPC = state.stack.pc() - 1;
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
          state.stack.pc() = entry.dest;
          state.stack.clearOperandsUntilGuard(StackGuardType::TRY, entry.guardLevel);
          state.stack.reclaimLocals(entry.localOffset, entry.localSize);
          if (entryType.is(TYPE::Root_)) { // finally block
            state.stack.push(state.getGlobal(BuiltinVarOffset::EXIT_STATUS));
            state.stack.enterFinally(entry.dest);
          } else { // catch block
            state.stack.loadThrownObject();
            state.setExitStatus(0); // clear exit status when enter catch block
          }
          return true;
        }
      }
    } else if (CODE(state) == &signalTrampoline) { // within signal trampoline
      unsetFlag(DSState::eventDesc, VMEvent::MASK);
    }

    auto &entries = state.stack.getFinallyEntries();
    while (!entries.empty() && entries.back().getDepth() == state.stack.getFrames().size()) {
      rethrowFromFinally(state);
    }
  }
  return false;
}

EvalRet VM::startEval(DSState &state, EvalOP op, DSError *dsError, DSValue &value) {
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

bool VM::callToplevel(DSState &state, const ObjPtr<FuncObject> &func, DSError *dsError) {
  assert(state.stack.recDepth() == 0);

  EvalOP op{};

  // set module to global
  state.globals.resize(state.rootModScope->getMaxGlobalVarIndex());
  {
    auto &type = state.typePool.get(func->getTypeID());
    assert(isa<ModType>(type));
    auto &modType = cast<ModType>(type);
    unsigned int index = modType.getIndex();
    state.setGlobal(index, DSValue(func));
  }

  // prepare stack
  state.stack.reset();
  RecursionGuard guard(state);
  state.stack.wind(0, 0, func->getCode());

  DSValue ret;
  auto s = startEval(state, op, dsError, ret);
  assert(s != EvalRet::HAS_ERROR);
  return s == EvalRet::SUCCESS;
}

unsigned int VM::prepareArguments(VMState &state, DSValue &&recv, CallArgs &&args) {
  state.clearThrownObject();

  // push arguments
  unsigned int size = args.first;
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

DSValue VM::execCommand(DSState &state, std::vector<DSValue> &&argv, bool propagate) {
  GUARD_RECURSION(state);

  static auto cmdTrampoline = initCmdTrampoline();

  const auto op = propagate ? EvalOP::PROPAGATE : EvalOP{};
  DSValue ret;
  auto obj = DSValue::create<ArrayObject>(state.typePool.get(TYPE::StringArray), std::move(argv));
  prepareArguments(state.stack, std::move(obj), {0, {}});
  if (windStackFrame(state, 1, 1, cmdTrampoline)) {
    startEval(state, op, nullptr, ret);
  }
  return ret;
}

DSValue VM::callFunction(DSState &state, DSValue &&funcObj, CallArgs &&args) {
  GUARD_RECURSION(state);

  auto &type = state.typePool.get(funcObj.getTypeID());
  unsigned int size = prepareArguments(state.stack, std::move(funcObj), std::move(args));

  DSValue ret;
  if (prepareFuncCall(state, size)) {
    assert(type.isFuncType());
    startEval(state, EvalOP::PROPAGATE, nullptr, ret);
  }
  if (cast<FunctionType>(type).getReturnType().isVoidType()) {
    ret = DSValue(); // clear return value
  }
  return ret;
}

DSErrorKind VM::handleUncaughtException(DSState &state, DSError *dsError) {
  if (!state.hasError()) {
    return DS_ERROR_KIND_SUCCESS;
  }

  auto except = state.stack.takeThrownObject();
  auto &errorType = state.typePool.get(except->getTypeID());
  DSErrorKind kind = DS_ERROR_KIND_RUNTIME_ERROR;
  if (errorType.is(TYPE::ShellExit_)) {
    kind = DS_ERROR_KIND_EXIT;
  } else if (errorType.is(TYPE::AssertFail_)) {
    kind = DS_ERROR_KIND_ASSERTION_ERROR;
  }
  switch (kind) {
  case DS_ERROR_KIND_RUNTIME_ERROR:
  case DS_ERROR_KIND_ASSERTION_ERROR:
  case DS_ERROR_KIND_EXIT:
    state.setExitStatus(typeAs<ErrorObject>(except).getStatus());
    break;
  default:
    break;
  }

  // get error line number
  unsigned int errorLineNum = 0;
  std::string sourceName;
  if (state.typePool.get(TYPE::Error).isSameOrBaseTypeOf(errorType) ||
      kind != DS_ERROR_KIND_RUNTIME_ERROR) {
    auto &obj = typeAs<ErrorObject>(except);
    errorLineNum = getOccurredLineNum(obj.getStackTrace());
    const char *ptr = getOccurredSourceName(obj.getStackTrace());
    sourceName = ptr;
  }

  // print error message
  if (kind == DS_ERROR_KIND_RUNTIME_ERROR || kind == DS_ERROR_KIND_ASSERTION_ERROR ||
      hasFlag(state.runtimeOption, RuntimeOption::TRACE_EXIT)) {
    typeAs<ErrorObject>(except).printStackTrace(state, ErrorObject::PrintOp::UNCAUGHT);
  }

  if (dsError != nullptr) {
    *dsError = {.kind = kind,
                .fileName = sourceName.empty() ? nullptr : strdup(sourceName.c_str()),
                .lineNum = errorLineNum,
                .chars = 0,
                .name = strdup(kind == DS_ERROR_KIND_RUNTIME_ERROR ? errorType.getName() : "")};
  }
  state.stack.setErrorObj(std::move(except)); // restore thrown object
  return kind;
}

bool VM::callTermHook(DSState &state) {
  auto except = state.stack.takeThrownObject();
  assert(state.termHookIndex != 0);
  auto funcObj = state.getGlobal(state.termHookIndex);
  if (funcObj.kind() == DSValueKind::INVALID) {
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
      makeArgs(DSValue::createInt(termKind), termKind == TERM_ON_ERR ? except : oldExitStatus);

  setFlag(DSState::eventDesc, VMEvent::MASK);
  VM::callFunction(state, std::move(funcObj), std::move(args)); // ignore exception
  state.stack.clearThrownObject();

  // restore old value
  state.setGlobal(BuiltinVarOffset::EXIT_STATUS, std::move(oldExitStatus));
  unsetFlag(DSState::eventDesc, VMEvent::MASK);

  // clear TERM_HOOK
  state.setGlobal(state.termHookIndex, DSValue::createInvalid());
  return true;
}

} // namespace ydsh