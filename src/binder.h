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

#ifndef YDSH_BINDER_H
#define YDSH_BINDER_H

#include <cassert>

#include <unistd.h>

#include "scope.h"
#include "signals.h"
#include "type_pool.h"

namespace ydsh {

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

  void bind(const char *varName, const DSType &type) {
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

  const auto &toType(FILE *) const { return this->pool.get(TYPE::UnixFD); }
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

  /**
   * process id of current process.
   * must be Int_Object
   */
  binder.bind("PID", getpid());

  /**
   * parent process id of current process.
   * must be Int_Object
   */
  binder.bind("PPID", getppid());

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
   * for internal field splitting.
   * must be String_Object.
   */
  binder.bind("IFS", VAL_DEFAULT_IFS, HandleAttr());

  /**
   * maintain completion result.
   * must be Array_Object
   */
  binder.bind("COMPREPLY", pool.get(TYPE::StringArray));

  /**
   * contains latest executed pipeline status.
   * must be Array_Object
   */
  binder.bind("PIPESTATUS", *pool.createArrayType(pool.get(TYPE::Int)).take());

  /**
   * contains exit status of most recent executed process. ($?)
   * must be Int_Object
   */
  binder.bind("?", 0, HandleAttr());

  /**
   * process id of root shell. ($$)
   * must be Int_Object
   */
  binder.bind("$", getpid());

  /**
   * contains script argument(exclude script name). ($@)
   * must be Array_Object
   */
  binder.bind("@", pool.get(TYPE::StringArray));

  /**
   * dummy object for completion. actually not used
   */
  binder.bind("#", 0);

  /**
   * represent shell or shell script name.
   * must be String_Object
   */
  binder.bind("0", "ydsh");

  // set builtin variables

  /**
   * uid of shell
   * must be Int_Object
   */
  binder.bind("UID", getuid());

  /**
   * euid of shell
   * must be Int_Object
   */
  binder.bind("EUID", geteuid());

  /**
   * for version detection
   * must be String_Object
   */
  binder.bindSysConst(config, SysConfig::VERSION);

  /**
   * must be String_Object
   */
  binder.bindSysConst(config, SysConfig::OSTYPE);

  /**
   * must be String_Object
   */
  binder.bindSysConst(config, SysConfig::MACHTYPE);

  /**
   * must be String_Object
   */
  binder.bindSysConst(config, SysConfig::CONFIG_HOME);

  /**
   * must be String_Object
   */
  binder.bindSysConst(config, SysConfig::DATA_HOME);

  /**
   * must be String_Object
   */
  binder.bindSysConst(config, SysConfig::DATA_DIR);

  /**
   * must be String_Object
   */
  binder.bindSysConst(config, SysConfig::MODULE_HOME);

  /**
   * must be String_Object
   */
  binder.bindSysConst(config, SysConfig::MODULE_DIR);

  // define small constants
  /**
   * must be int
   */
  binder.bindSmallConst("ON_EXIT", ConstEntry::Kind::INT, TERM_ON_EXIT);
  binder.bindSmallConst("ON_ERR", ConstEntry::Kind::INT, TERM_ON_ERR);
  binder.bindSmallConst("ON_ASSERT", ConstEntry::Kind::INT, TERM_ON_ASSERT);

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
   * dummy object for signal handler setting
   * must be DSObject
   */
  binder.bind("SIG", pool.get(TYPE::Signals));

  /**
   * must be UnixFD_Object
   */
  binder.bind(VAR_STDIN, stdin);

  /**
   * must be UnixFD_Object
   */
  binder.bind(VAR_STDOUT, stdout);

  /**
   * must be UnixFD_Object
   */
  binder.bind(VAR_STDERR, stderr);

  /**
   * must be StringObject
   */
  binder.bind(VAR_YDSH_BIN, "");

  // signal constants (POSIX.1-1990 standard)
  auto *signalPairs = getSignalList();
  for (unsigned int i = 0; signalPairs[i].name; i++) {
    auto &pair = signalPairs[i];
    std::string name = "SIG";
    name += pair.name;
    binder.bindSmallConst(std::move(name), ConstEntry::Kind::SIG, pair.sigNum);
    if (pair.sigNum == SIGTTOU) {
      break;
    }
  }
}

} // namespace ydsh

#endif // YDSH_BINDER_H
