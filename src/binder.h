/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#ifndef ARSH_BINDER_H
#define ARSH_BINDER_H

#include <cassert>

#include <unistd.h>

#include "scope.h"
#include "signals.h"
#include "type_pool.h"

namespace arsh {

template <typename Consumer>
class Binder {
private:
  Consumer &consumer;
  TypePool &pool;
  NameScope &scope;

public:
  Binder(Consumer &consumer, TypePool &pool, NameScope &scope)
      : consumer(consumer), pool(pool), scope(scope) {}

  template <typename T>
  void bind(const char *varName, T v, HandleKind kind, HandleAttr attr) {
    auto &type = this->toType(std::forward<T>(v));
    auto handle = this->scope.defineHandle(varName, type, kind, attr);
    assert(static_cast<bool>(handle));
    this->consumer(*handle.asOk(), std::forward<T>(v));
  }

  template <typename T>
  void bind(const char *varName, T v, HandleAttr attr = HandleAttr::READ_ONLY) {
    this->bind(varName, std::move(v), HandleKind::VAR, attr);
  }

  void bind(const char *varName, const Type &type) {
    auto handle = this->scope.defineHandle(varName, type, HandleAttr::READ_ONLY);
    assert(static_cast<bool>(handle));
    this->consumer(*handle.asOk(), type);
  }

  void bindSysConst(const SysConfig &config, const char *constName) {
    this->bind(constName, *config.lookup(constName), HandleKind::SYS_CONST, HandleAttr::READ_ONLY);
  }

  void bindSmallConst(std::string &&constName, ConstEntry::Kind k, unsigned char value) {
    ConstEntry entry(k, value);
    auto handle = this->scope.defineConst(std::move(constName), entry);
    (void)handle;
    assert(static_cast<bool>(handle));
  }

private:
  const auto &toType(int64_t) const { return this->pool.get(TYPE::Int); }

  const auto &toType(const std::string &) const { return this->pool.get(TYPE::String); }

  const auto &toType(FILE *) const { return this->pool.get(TYPE::FD); }
};

template <typename Consumer>
void bindBuiltins(Consumer &consumer, const SysConfig &config, TypePool &pool, NameScope &scope) {
  Binder<Consumer> binder(consumer, pool, scope);

  /**
   * dummy object.
   * must be String_Object
   */
  binder.bind(CVAR_SCRIPT_NAME, "", HandleKind::MOD_CONST, HandleAttr::READ_ONLY);

  /**
   * dummy object
   * must be String_Object
   */
  binder.bind(CVAR_SCRIPT_DIR, "", HandleKind::MOD_CONST, HandleAttr::READ_ONLY);

  /**
   * default variable for read command.
   * must be String_Object
   */
  binder.bind("REPLY", "", HandleAttr());

  /**
   * holding read variable.
   * must be Map_Object
   */
  binder.bind("reply", *pool.createMapType(pool.get(TYPE::String), pool.get(TYPE::String)).take());

  binder.bind("DYNA_UDCS",
              *pool.createMapType(pool.get(TYPE::String), pool.get(TYPE::Command)).take());

  /**
   * dummy object for module
   */
  binder.bind("MODULE", pool.get(TYPE::Module));

  /**
   * dummy object for random number
   * must be Int_Object
   */
  binder.bind("RANDOM", 0);

  /**
   * must be Long_Object.
   */
  binder.bind("SECONDS", 0, HandleAttr());

  /**
   * dummy object for currently thrown object
   */
  binder.bind("THROWN", *pool.createOptionType(pool.get(TYPE::Throwable)).take());

  /**
   * for internal field splitting.
   * must be String_Object.
   */
  binder.bind("IFS", VAL_DEFAULT_IFS, HandleAttr());

  /**
   * for current east asian ambiguous character width setting
   * only updated in line editor
   */
  binder.bind("EAW", 1);

  /**
   * maintain completion result.
   * must be Array_Object
   */
  binder.bind("COMPREPLY", pool.get(TYPE::Candidates));

  /**
   * maintain directory stack
   */
  binder.bind("DIRSTACK", pool.get(TYPE::StringArray));

  /**
   * contains latest executed pipeline status.
   * must be Array_Object
   */
  binder.bind("PIPESTATUS", *pool.createArrayType(pool.get(TYPE::Int)).take());

  binder.bind("SUBSHELL", 0);

  binder.bind("LINES", 0);

  binder.bind("COLUMNS", 0);

  /**
   * contains exit status of most recent executed process. ($?)
   * must be Int_Object
   */
  binder.bind("?", 0, HandleAttr());

  /**
   * contains script argument(exclude script name). ($@)
   * must be Array_Object
   */
  binder.bind(VAR_ARGS, pool.get(TYPE::StringArray));

  /**
   * represent shell or shell script name.
   * must be String_Object
   */
  binder.bind(CVAR_ARG0, "arsh");

  /**
   * process id of current process.
   * must be Int
   */
  binder.bind("PID", getpid());

  /**
   * parent process id of current process.
   * must be Int
   */
  binder.bind("PPID", getppid());

  /**
   * process id of root shell. ($$)
   * must be Int
   */
  binder.bind("$", getpid());

  // set builtin variables

  /**
   * uid of shell
   * must be Int
   */
  binder.bind("UID", getuid());

  /**
   * euid of shell
   * must be Int
   */
  binder.bind("EUID", geteuid());

  /**
   * must be UnixFD_Object
   */
  binder.bind("STDIN", stdin);

  /**
   * must be UnixFD_Object
   */
  binder.bind("STDOUT", stdout);

  /**
   * must be UnixFD_Object
   */
  binder.bind("STDERR", stderr);

  /**
   * dummy object for signal handler setting
   * must be Object
   */
  binder.bind("SIG", pool.get(TYPE::Signals));

  binder.bind("JOB", pool.get(TYPE::Jobs));

  /**
   * must be StringObject
   */
  binder.bind(VAR_BIN_NAME, "");

  // builtin system constant
#define GEN_BIND(E, S) binder.bindSysConst(config, SysConfig::E);
  EACH_SYSCONFIG_EXPORTED(GEN_BIND)
#undef GEN_BIND

  // define small constants

  /**
   * must be bool
   */
  binder.bindSmallConst("TRUE", ConstEntry::Kind::BOOL, 1);
  binder.bindSmallConst("True", ConstEntry::Kind::BOOL, 1);
  binder.bindSmallConst("true", ConstEntry::Kind::BOOL, 1);
  binder.bindSmallConst("FALSE", ConstEntry::Kind::BOOL, 0);
  binder.bindSmallConst("False", ConstEntry::Kind::BOOL, 0);
  binder.bindSmallConst("false", ConstEntry::Kind::BOOL, 0);

  /**
   * must be Nothing?
   */
  binder.bindSmallConst("NONE", ConstEntry::Kind::NONE, 0);
  binder.bindSmallConst("None", ConstEntry::Kind::NONE, 0);
  binder.bindSmallConst("none", ConstEntry::Kind::NONE, 0);

  // signal constants (POSIX.1-1990 standard)
  {
    const auto range = getStandardSignalEntries();
    for (auto &e : range) {
      if (e.kind == SignalEntry::Kind::POSIX_1_1990 || e.sigNum == SIGWINCH) {
        binder.bindSmallConst(e.toFullName(), ConstEntry::Kind::SIG, e.sigNum);
      }
    }
  }

  // add alias
  {
    auto ret = scope.lookup(VAR_ARGS);
    assert(ret);
    scope.defineAlias("@", ret.asOk());
  }
  {
    auto ret = scope.lookup("?"); // for Int object
    assert(ret);
    scope.defineAlias("#", ret.asOk()); // dummy for completion (must Int)
  }
  {
    auto ret = scope.lookup(CVAR_ARG0);
    assert(ret);
    scope.defineAlias("0", ret.asOk());
  }
}

} // namespace arsh

#endif // ARSH_BINDER_H
