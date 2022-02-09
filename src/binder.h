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
  void bind(const char *varName, T v, HandleAttr attr = HandleAttr::READ_ONLY) {
    auto &type = this->toType(std::forward<T>(v));
    auto handle = this->scope.defineHandle(varName, type, attr);
    assert(static_cast<bool>(handle));
    this->consumer(*handle.asOk(), std::forward<T>(v));
  }

  void bind(const char *varName, const DSType &type) {
    auto handle = this->scope.defineHandle(varName, type, HandleAttr::READ_ONLY);
    assert(static_cast<bool>(handle));
    this->consumer(*handle.asOk(), type);
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
  binder.bind(CVAR_SCRIPT_NAME, "", HandleAttr::MOD_CONST | HandleAttr::READ_ONLY);

  /**
   * dummy object
   * must be String_Object
   */
  binder.bind(CVAR_SCRIPT_DIR, "", HandleAttr::MOD_CONST | HandleAttr::READ_ONLY);

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
  binder.bind("IFS", " \t\n", HandleAttr());

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
   * contains size of argument. ($#)
   * must be Int_Object
   */
  binder.bind("#", 0);

  /**
   * represent shell or shell script name.
   * must be String_Object
   */
  binder.bind("0", "ydsh");

  /**
   * initialize positional parameter
   */
  for (unsigned int i = 0; i < 9; i++) {
    binder.bind(std::to_string(i + 1).c_str(), "");
  }

  // set builtin variables
  /**
   * for version detection
   * must be String_Object
   */
  binder.bind(SysConfig::VERSION, *config.lookup(SysConfig::VERSION));

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
   * must be String_Object
   */
  binder.bind(SysConfig::OSTYPE, *config.lookup(SysConfig::OSTYPE));

  /**
   * must be String_Object
   */
  binder.bind(SysConfig::MACHTYPE, *config.lookup(SysConfig::MACHTYPE));

  /**
   * must be String_Object
   */
  binder.bind(SysConfig::CONFIG_HOME, *config.lookup(SysConfig::CONFIG_HOME));

  /**
   * must be String_Object
   */
  binder.bind(SysConfig::DATA_HOME, *config.lookup(SysConfig::DATA_HOME));

  /**
   * must be String_Object
   */
  binder.bind(SysConfig::DATA_DIR, *config.lookup(SysConfig::DATA_DIR));

  /**
   * must be String_Object
   */
  binder.bind(SysConfig::MODULE_HOME, *config.lookup(SysConfig::MODULE_HOME));

  /**
   * must be String_Object
   */
  binder.bind(SysConfig::MODULE_DIR, *config.lookup(SysConfig::MODULE_DIR));

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
   * must be Int_Object
   */
  binder.bind("ON_EXIT", TERM_ON_EXIT);
  binder.bind("ON_ERR", TERM_ON_ERR);
  binder.bind("ON_ASSERT", TERM_ON_ASSERT);

  /**
   * must be StringObject
   */
  binder.bind(VAR_YDSH_BIN, "");
}

} // namespace ydsh

#endif // YDSH_BINDER_H
