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

#include <sys/utsname.h>
#include <sys/wait.h>

#include <algorithm>
#include <cassert>

#include "logger.h"
#include "misc/num_util.hpp"
#include "vm.h"
#include <embed.h>

extern char **environ; // NOLINT

namespace ydsh {

// core api definition
const DSValue &getBuiltinGlobal(const DSState &st, const char *varName) {
  auto *handle = st.builtinModScope->lookup(varName);
  assert(handle != nullptr);
  return st.getGlobal(handle->getIndex());
}

void raiseError(DSState &st, TYPE type, std::string &&message, int status) {
  auto except =
      ErrorObject::newError(st, st.typePool.get(type), DSValue::createStr(std::move(message)));
  st.throwObject(std::move(except), status);
}

void raiseSystemError(DSState &st, int errorNum, std::string &&message) {
  assert(errorNum != 0);
  if (errorNum == EINTR) {
    /**
     * if EINTR, already raised SIGINT. and SIGINT handler also raises SystemError.
     * due to eliminate redundant SystemError, force clear SIGINT
     */
    SignalGuard guard;
    DSState::clearPendingSignal(SIGINT);
  }
  std::string str(std::move(message));
  str += ": ";
  str += strerror(errorNum);
  raiseError(st, TYPE::SystemError, std::move(str));
}

void installSignalHandler(DSState &st, int sigNum, const DSValue &handler) {
  SignalGuard guard;

  auto &DFL_handler = getBuiltinGlobal(st, VAR_SIG_DFL);
  auto &IGN_handler = getBuiltinGlobal(st, VAR_SIG_IGN);

  DSValue actualHandler;
  auto op = SignalVector::UnsafeSigOp::SET;
  if (sigNum == SIGBUS || sigNum == SIGSEGV || sigNum == SIGILL || sigNum == SIGFPE) {
    /**
     * not handle or ignore these signals due to prevent undefined behavior.
     * see.
     * https://wiki.sei.cmu.edu/confluence/display/c/SIG35-C.+Do+not+return+from+a+computational+exception+signal+handler
     *      http://man7.org/linux/man-pages/man2/sigaction.2.html
     */
    return;
  } else if (handler == DFL_handler) {
    if (sigNum == SIGHUP) {
      actualHandler = handler;
    } else {
      op = SignalVector::UnsafeSigOp::DFL;
    }
  } else if (handler == IGN_handler) {
    op = SignalVector::UnsafeSigOp::IGN;
  } else {
    actualHandler = handler;
  }

  st.sigVector.install(sigNum, op, actualHandler);
}

DSValue getSignalHandler(const DSState &st, int sigNum) {
  auto &DFL_handler = getBuiltinGlobal(st, VAR_SIG_DFL);
  auto &IGN_handler = getBuiltinGlobal(st, VAR_SIG_IGN);

  auto handler = st.sigVector.lookup(sigNum);

  if (handler == nullptr) {
    struct sigaction action {};
    if (sigaction(sigNum, nullptr, &action) == 0) {
      if (action.sa_handler == SIG_IGN) {
        return IGN_handler;
      }
    }
    return DFL_handler;
  }
  return handler;
}

void setJobControlSignalSetting(DSState &st, bool set) {
  SignalGuard guard;

  auto op = set ? SignalVector::UnsafeSigOp::IGN : SignalVector::UnsafeSigOp::DFL;
  DSValue handler;

  if (set) {
    st.sigVector.install(SIGINT, SignalVector::UnsafeSigOp::SET,
                         getBuiltinGlobal(st, VAR_DEF_SIGINT));
  } else {
    st.sigVector.install(SIGINT, op, handler);
  }
  st.sigVector.install(SIGQUIT, op, handler);
  st.sigVector.install(SIGTSTP, op, handler);
  st.sigVector.install(SIGTTIN, op, handler);
  st.sigVector.install(SIGTTOU, op, handler);
}

const ModType *getRuntimeModuleByLevel(const DSState &state, const unsigned int callLevel) {
  const CompiledCode *code = nullptr;
  unsigned int depth = 0;
  state.getCallStack().walkFrames([&](const ControlFrame &frame) {
    auto *c = frame.code;
    if (c->is(CodeKind::NATIVE)) {
      return true; // continue
    }
    if (depth == callLevel) {
      code = static_cast<const CompiledCode *>(c);
      return false;
    }
    depth++;
    return true;
  });
  if (code) {
    auto ret = state.typePool.getModTypeById(code->getBelongedModId());
    assert(ret);
    return cast<ModType>(ret.asOk());
  }
  return nullptr;
}

class VariableBinder {
private:
  DSState *state; // may be null
  TypePool &pool;
  NameScope &scope;

public:
  VariableBinder(DSState *state, TypePool &pool, NameScope &scope)
      : state(state), pool(pool), scope(scope) {}

  void bind(const char *varName, DSValue &&value, FieldAttribute attr = FieldAttribute::READ_ONLY) {
    auto id = value.getTypeID();
    this->bind(varName, this->pool.get(id), std::move(value), attr);
  }

private:
  void bind(const char *varName, const DSType &type, DSValue &&value, FieldAttribute attr) {
    auto handle = this->scope.defineHandle(varName, type, attr);
    assert(static_cast<bool>(handle));
    if (this->state) {
      this->state->setGlobal(handle.asOk()->getIndex(), std::move(value));
    }
  }
};

void bindBuiltinVariables(DSState *state, TypePool &pool, NameScope &scope) {
  VariableBinder binder(state, pool, scope);

  /**
   * dummy object.
   * must be String_Object
   */
  binder.bind(CVAR_SCRIPT_NAME, DSValue::createStr(),
              FieldAttribute::MOD_CONST | FieldAttribute::READ_ONLY);

  /**
   * dummy object
   * must be String_Object
   */
  binder.bind(CVAR_SCRIPT_DIR, DSValue::createStr(),
              FieldAttribute::MOD_CONST | FieldAttribute::READ_ONLY);

  /**
   * default variable for read command.
   * must be String_Object
   */
  binder.bind("REPLY", DSValue::createStr(), FieldAttribute());

  /**
   * holding read variable.
   * must be Map_Object
   */
  binder.bind("reply",
              DSValue::create<MapObject>(
                  *pool.createMapType(pool.get(TYPE::String), pool.get(TYPE::String)).take()));

  /**
   * process id of current process.
   * must be Int_Object
   */
  binder.bind("PID", DSValue::createInt(getpid()));

  /**
   * parent process id of current process.
   * must be Int_Object
   */
  binder.bind("PPID", DSValue::createInt(getppid()));

  /**
   * must be Long_Object.
   */
  binder.bind("SECONDS", DSValue::createInt(0), FieldAttribute::SECONDS);

  /**
   * for internal field splitting.
   * must be String_Object.
   */
  binder.bind("IFS", DSValue::createStr(" \t\n"), FieldAttribute());

  /**
   * maintain completion result.
   * must be Array_Object
   */
  binder.bind("COMPREPLY", DSValue::create<ArrayObject>(pool.get(TYPE::StringArray)));

  /**
   * contains latest executed pipeline status.
   * must be Array_Object
   */
  binder.bind("PIPESTATUS",
              DSValue::create<ArrayObject>(*pool.createArrayType(pool.get(TYPE::Int)).take()));

  /**
   * contains exit status of most recent executed process. ($?)
   * must be Int_Object
   */
  binder.bind("?", DSValue::createInt(0), FieldAttribute());

  /**
   * process id of root shell. ($$)
   * must be Int_Object
   */
  binder.bind("$", DSValue::createInt(getpid()));

  /**
   * contains script argument(exclude script name). ($@)
   * must be Array_Object
   */
  binder.bind("@", DSValue::create<ArrayObject>(pool.get(TYPE::StringArray)));

  /**
   * contains size of argument. ($#)
   * must be Int_Object
   */
  binder.bind("#", DSValue::createInt(0));

  /**
   * represent shell or shell script name.
   * must be String_Object
   */
  binder.bind("0", DSValue::createStr("ydsh"));

  /**
   * initialize positional parameter
   */
  for (unsigned int i = 0; i < 9; i++) {
    binder.bind(std::to_string(i + 1).c_str(), DSValue::createStr());
  }

  // set builtin variables
  /**
   * for version detection
   * must be String_Object
   */
  binder.bind(CVAR_VERSION, DSValue::createStr(X_INFO_VERSION_CORE));

  /**
   * uid of shell
   * must be Int_Object
   */
  binder.bind("UID", DSValue::createInt(getuid()));

  /**
   * euid of shell
   * must be Int_Object
   */
  binder.bind("EUID", DSValue::createInt(geteuid()));

  struct utsname name {};
  if (uname(&name) == -1) {
    fatal_perror("cannot get utsname");
  }

  /**
   * must be String_Object
   */
  binder.bind(CVAR_OSTYPE, DSValue::createStr(name.sysname));

  /**
   * must be String_Object
   */
  binder.bind(CVAR_MACHTYPE, DSValue::createStr(BUILD_ARCH));

  /**
   * must be String_Object
   */
  binder.bind(CVAR_DATA_DIR, DSValue::createStr(SYSTEM_DATA_DIR));

  /**
   * must be String_Object
   */
  binder.bind(CVAR_MODULE_DIR, DSValue::createStr(SYSTEM_MOD_DIR));

  /**
   * dummy object for random number
   * must be Int_Object
   */
  binder.bind("RANDOM", DSValue::createInt(0), FieldAttribute::READ_ONLY | FieldAttribute ::RANDOM);

  /**
   * dummy object for signal handler setting
   * must be DSObject
   */
  binder.bind("SIG", DSValue::createDummy(pool.get(TYPE::Signals)));

  /**
   * must be UnixFD_Object
   */
  binder.bind(VAR_STDIN, DSValue::create<UnixFdObject>(STDIN_FILENO));

  /**
   * must be UnixFD_Object
   */
  binder.bind(VAR_STDOUT, DSValue::create<UnixFdObject>(STDOUT_FILENO));

  /**
   * must be UnixFD_Object
   */
  binder.bind(VAR_STDERR, DSValue::create<UnixFdObject>(STDERR_FILENO));

  /**
   * must be Int_Object
   */
  binder.bind("ON_EXIT", DSValue::createInt(TERM_ON_EXIT));
  binder.bind("ON_ERR", DSValue::createInt(TERM_ON_ERR));
  binder.bind("ON_ASSERT", DSValue::createInt(TERM_ON_ASSERT));

  /**
   * must be StringObject
   */
  binder.bind(VAR_YDSH_BIN, DSValue::createStr());
}

const char *getEmbeddedScript() { return embed_script; };

// ##########################
// ##     SignalVector     ##
// ##########################

struct SigEntryComp {
  using Entry = std::pair<int, DSValue>;

  bool operator()(const Entry &x, int y) const { return x.first < y; }

  bool operator()(int x, const Entry &y) const { return x < y.first; }
};

void SignalVector::insertOrUpdate(int sigNum, const DSValue &func) {
  auto iter = std::lower_bound(this->data.begin(), this->data.end(), sigNum, SigEntryComp());
  if (iter != this->data.end() && iter->first == sigNum) {
    if (func) {
      iter->second = func; // update
    } else {
      this->data.erase(iter); // remove
    }
  } else if (func) {
    this->data.insert(iter, std::make_pair(sigNum, func)); // insert
  }
}

DSValue SignalVector::lookup(int sigNum) const {
  auto iter = std::lower_bound(this->data.begin(), this->data.end(), sigNum, SigEntryComp());
  if (iter != this->data.end() && iter->first == sigNum) {
    return iter->second;
  }
  return nullptr;
}

// when called this handler, all signals are blocked due to signal mask
static void signalHandler(int sigNum) {
  DSState::pendingSigSet.add(sigNum);
  setFlag(DSState::eventDesc, VMEvent::SIGNAL);
}

void SignalVector::install(int sigNum, UnsafeSigOp op, const DSValue &handler) {
  if (sigNum == SIGCHLD) {
    op = UnsafeSigOp::SET;
  }

  // set posix signal handler
  struct sigaction action {};
  if (sigNum != SIGINT) { // always restart system call except for SIGINT
    action.sa_flags = SA_RESTART;
  }
  sigfillset(&action.sa_mask);

  switch (op) {
  case UnsafeSigOp::DFL:
    action.sa_handler = SIG_DFL;
    break;
  case UnsafeSigOp::IGN:
    action.sa_handler = SIG_IGN;
    break;
  case UnsafeSigOp::SET:
    action.sa_handler = signalHandler;
    break;
  }
  sigaction(sigNum, &action, nullptr);

  // register handler
  if (sigNum != SIGCHLD) {
    this->insertOrUpdate(sigNum, handler);
  }
}

void SignalVector::clear() {
  for (auto &e : this->data) {
    struct sigaction action {};
    action.sa_handler = SIG_DFL;
    sigaction(e.first, &action, nullptr);
  }
  this->data.clear();
}

int xexecve(const char *filePath, char *const *argv, char *const *envp) {
  if (filePath == nullptr) {
    errno = ENOENT;
    return -1;
  }

  // set env
  setenv("_", filePath, 1);
  if (envp == nullptr) {
    envp = environ;
  }

  LOG_EXPR(DUMP_EXEC, [&] {
    std::string str = filePath;
    str += ", [";
    for (unsigned int i = 0; argv[i] != nullptr; i++) {
      if (i > 0) {
        str += ", ";
      }
      str += argv[i];
    }
    str += "]";
    return str;
  });

  // execute external command
  int ret = execve(filePath, argv, envp);
  if (errno == ENOEXEC) { // fallback to /bin/sh
    unsigned int size = 0;
    for (; argv[size]; size++)
      ;
    size++;
    char *newArgv[size + 1];
    newArgv[0] = const_cast<char *>("/bin/sh");
    memcpy(newArgv + 1, argv, sizeof(char *) * size);
    return execve(newArgv[0], newArgv, envp);
  }
  return ret;
}

} // namespace ydsh

#ifdef CODE_COVERAGE
extern "C" void __gcov_flush(); // for coverage reporting
#endif

namespace ydsh {

void terminate(int exitStatus) {
#ifdef CODE_COVERAGE
  /*
   * after call _exit(), not write coverage information due to skip atexit handler.
   * in order to write coverage information, manually call __gcove_flush()
   */
  __gcov_flush();
#endif
  _exit(exitStatus);
}

} // namespace ydsh