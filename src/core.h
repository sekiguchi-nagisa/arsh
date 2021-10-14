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

#ifndef YDSH_CORE_H
#define YDSH_CORE_H

#include <array>
#include <string>
#include <vector>

#include "complete.h"
#include "misc/resource.hpp"
#include "object.h"
#include "opcode.h"

struct DSState;

namespace ydsh {

struct VMHook {
  /**
   * hook for vm fetch event
   * @param st
   * @param op
   * fetched opcode
   */
  virtual void vmFetchHook(DSState &st, OpCode op) = 0;

  /**
   * hook for exception handle event
   * @param st
   */
  virtual void vmThrowHook(DSState &st) = 0;
};

const DSValue &getBuiltinGlobal(const DSState &st, const char *varName);

/**
 * raise Error Object and update exit status
 * @param st
 * @param type
 * @param message
 * @param status
 */
void raiseError(DSState &st, TYPE type, std::string &&message, int status = 1);

void raiseSystemError(DSState &st, int errorNum, std::string &&message);

/**
 *
 * @param st
 * @param sigNum
 * @param handler
 * must be FuncObject
 */
void installSignalHandler(DSState &st, int sigNum, const DSValue &handler);

/**
 *
 * @param st
 * @param sigNum
 * @return
 * if sigNum is invalid (ex. pseudo-signal), return SIG_DFL object
 */
DSValue getSignalHandler(const DSState &st, int sigNum);

/**
 * if set is true, ignore some signals.
 * if set is false, reset some signal setting.
 * @param set
 */
void setJobControlSignalSetting(DSState &st, bool set);

const ModType *getRuntimeModuleByLevel(const DSState &state, unsigned int callLevel);

inline const ModType *getCurRuntimeModule(const DSState &state) {
  return getRuntimeModuleByLevel(state, 0);
}

/**
 * perform completion in specified unserlying module
 * @param st
 * @param underlyingModType
 * may be null
 * @param ref
 * @param option
 * @return
 * return size of completion result. (equivalent to size of $COMPREPLY)
 */
unsigned int doCodeCompletion(DSState &st, const ModType *underlyingModType, StringRef ref,
                              CodeCompOp option = {});

class SignalVector {
private:
  /**
   * pair.second must be FuncObject
   */
  std::vector<std::pair<int, DSValue>> data;

public:
  SignalVector() = default;
  ~SignalVector() = default;

  /**
   * if func is null, delete handler.
   * @param sigNum
   * @param value
   * must be FuncObject
   * may be null
   */
  void insertOrUpdate(int sigNum, const DSValue &value);

  /**
   *
   * @param sigNum
   * @return
   * if not found, return null obj.
   */
  DSValue lookup(int sigNum) const;

  const std::vector<std::pair<int, DSValue>> &getData() const { return this->data; };

  enum class UnsafeSigOp {
    DFL,
    IGN,
    SET,
  };

  /**
   * unsafe op.
   * @param sigNum
   * @param op
   * @param handler
   * may be nullptr
   */
  void install(int sigNum, UnsafeSigOp op, const DSValue &handler);

  /**
   * clear all handler and set to SIG_DFL.
   */
  void clear();
};

template <typename... T>
inline std::pair<unsigned int, std::array<DSValue, 3>> makeArgs(T &&...arg) {
  static_assert(sizeof...(arg) <= 3, "too long");
  return std::make_pair(sizeof...(arg), std::array<DSValue, 3>{{std::forward<T>(arg)...}});
}

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

/**
 * wrap '_exit'
 * when coverage build, call '__gcov_flush()' before _exit
 * @param exitStatus
 */
[[noreturn]] void terminate(int exitStatus);

} // namespace ydsh

#endif // YDSH_CORE_H
