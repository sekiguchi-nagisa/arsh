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

#ifndef ARSH_CORE_H
#define ARSH_CORE_H

#include <string>
#include <vector>

#include "complete.h"
#include "misc/resource.hpp"
#include "misc/result.hpp"
#include "object.h"
#include "opcode.h"
#include "signals.h"

struct ARState;

namespace arsh {

struct VMHook {
  /**
   * hook for vm fetch event
   * @param st
   * @param op
   * fetched opcode
   */
  virtual void vmFetchHook(ARState &st, OpCode op) = 0;

  /**
   * hook for exception handle event
   * @param st
   */
  virtual void vmThrowHook(ARState &st) = 0;
};

const Value &getBuiltinGlobal(const ARState &st, const char *varName);

void reassignReplyVar(ARState &st);

/**
 * raise Error Object and update exit status
 * @param st
 * @param type
 * @param message
 * @param status
 */
void raiseError(ARState &st, TYPE type, std::string &&message, int64_t status = 1);

void raiseSystemError(ARState &st, int errorNum, std::string &&message);

/**
 * actual implementation of exit command
 * @param st
 * @param status
 */
void raiseShellExit(ARState &st, int64_t status);

/**
 * print error message with current location (source:lineno)
 * emit newline
 * @param state
 * @param cmdName
 * @param errNum
 * may be 0
 * @param fmt
 * @param ...
 * @return
 */
bool printErrorAt(const ARState &state, StringRef cmdName, int errNum, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/**
 * get and set signal handler.
 * if handler is null, not set handler.
 * @param st
 * @param sigNum
 * @param handler
 * may be null
 * @return
 * old signal handler
 */
ObjPtr<Object> installSignalHandler(ARState &st, int sigNum, ObjPtr<Object> handler);

void installSignalHandler(ARState &st, SigSet sigSet, const ObjPtr<Object> &handler);

/**
 * if set is true, ignore some signals.
 * if set is false, reset some signal setting.
 * @param set
 */
void setJobControlSignalSetting(ARState &st, bool set);

/**
 * synchronize actual signal handler setting with SignalVector
 * also set SIGCHLD handler.
 * @param st
 */
void setSignalSetting(ARState &st);

/**
 * clear installed signal handlers and set to SIG_DFL (except for SIGCHLD).
 * not block signal
 * @param state
 */
void resetSignalSettingUnblock(ARState &state);

void setLocaleSetting();

const ModType *getRuntimeModuleByLevel(const ARState &state, unsigned int callLevel);

inline const ModType *getCurRuntimeModule(const ARState &state) {
  return getRuntimeModuleByLevel(state, 0);
}

class RuntimeCancelToken : public CancelToken {
private:
  const bool clearSignal;

public:
  RuntimeCancelToken() : RuntimeCancelToken(false) {}

  explicit RuntimeCancelToken(bool clear) : clearSignal(clear) {}

  bool operator()() override;
};

struct DoCodeCompletionOption {
  CodeCompOp op;
  bool putDesc;
};

/**
 * perform completion in specified underlying module context.
 * during execution preserve exit status and IFS
 * @param st
 * @param modDesc
 * module descriptor for specifying completion context
 * @param option
 * @param source
 * @return
 * if specify invalid module descriptor, return -1 and set EINVAL
 * if interrupted, return -1 and set EINTR
 * return size of completion result. (equivalent to size of $COMPREPLY)
 */
int doCodeCompletion(ARState &st, StringRef modDesc, DoCodeCompletionOption option,
                     StringRef source);

class SignalVector {
private:
  /**
   * pair.second must be FuncObject
   */
  std::vector<std::pair<int, ObjPtr<Object>>> data;

public:
  SignalVector() = default;
  ~SignalVector() = default;

  /**
   * if value is null, delete handler.
   * @param sigNum
   * @param value
   * may be null
   */
  void insertOrUpdate(int sigNum, ObjPtr<Object> value);

  /**
   *
   * @param sigNum
   * @return
   * if not found, return null obj.
   */
  ObjPtr<Object> lookup(int sigNum) const;

  const auto &getData() const { return this->data; };

  void clear() { this->data.clear(); }
};

/**
 * compile string as function
 * @param state
 * @param expr
 * @param modType
 * globally imported to fresh module-context
 * @return
 * compiled FuncObject.
 * if compilation failed, return ErrorObject
 */
Result<ObjPtr<FuncObject>, ObjPtr<ErrorObject>> loadExprAsFunc(ARState &state, StringRef expr,
                                                               const ModType &modType);

/**
 * resolve fully qualified command name
 * @param state
 * @param name
 * must be String
 * @param modType
 * @return
 * if not resolved, return empty string
 */
std::string resolveFullCommandName(const ARState &state, const Value &name, const ModType &modType,
                                   bool udcOnly = false);

/**
 *
 * @param state
 * @param arrayObj
 * @param compFunc
 * must be FuncObject
 * @return
 * if has error, return false
 */
bool mergeSort(ARState &state, ArrayObject &arrayObj, const Value &compFunc);

/**
 *
 * @param filePath
 * if null, not execute and set ENOENT.
 * @param argv
 * not null
 * @param envp
 * may be null
 * @return
 * if success, not return.
 */
int xexecve(const char *filePath, char *const *argv, char *const *envp);

class FakeModuleLoader : public ModuleLoaderBase {
private:
  CStrPtr path;

public:
  explicit FakeModuleLoader(const SysConfig &config) : ModuleLoaderBase(config) {}

private:
  ModResult addNewModEntry(CStrPtr &&ptr) override;
};

} // namespace arsh

#endif // ARSH_CORE_H
