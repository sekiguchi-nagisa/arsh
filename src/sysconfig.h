/*
 * Copyright (C) 2022 Nagisa Sekiguchi
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

#ifndef ARSH_SYSCONFIG_H
#define ARSH_SYSCONFIG_H

#include "misc/string_ref.hpp"

namespace arsh {

#define EACH_SYSCONFIG_UNEXPORTED(OP)                                                              \
  OP(COMPILER, "%compiler")                                                                        \
  OP(REGEX, "%regex")                                                                              \
  OP(UNICODE, "%unicode")

#define EACH_SYSCONFIG_EXPORTED(OP)                                                                \
  OP(VERSION, "VERSION")                                                                           \
  OP(OSTYPE, "OSTYPE")                                                                             \
  OP(MACHTYPE, "MACHTYPE")                                                                         \
  OP(CONFIG_HOME, "CONFIG_HOME")                                                                   \
  OP(DATA_HOME, "DATA_HOME")                                                                       \
  OP(MODULE_HOME, "MODULE_HOME")                                                                   \
  OP(DATA_DIR, "DATA_DIR")                                                                         \
  OP(MODULE_DIR, "MODULE_DIR")

#define EACH_SYSCONFIG(OP)                                                                         \
  EACH_SYSCONFIG_UNEXPORTED(OP)                                                                    \
  EACH_SYSCONFIG_EXPORTED(OP)

/**
 * runtime system configuration constants
 */
class SysConfig {
public:
#define GEN_CONST(E, S) static constexpr const char *E = S;
  EACH_SYSCONFIG_UNEXPORTED(GEN_CONST)
  EACH_SYSCONFIG_EXPORTED(GEN_CONST)
#undef GEN_CONST

private:
  StrRefMap<std::string> values;

public:
  SysConfig();

  const StrRefMap<std::string> &getValues() const { return this->values; }

  const std::string *lookup(StringRef key) const;

  /**
   * if defined `XDG_CONFIG_HOME`, indicates $XDG_CONFIG_HOME/arsh
   * otherwise, indicates $HOME/.config/arsh
   */
  const std::string &getConfigHome() const { return *this->lookup(CONFIG_HOME); }

  /**
   * if defined `XDG_DATA_HOME`, indicates $XGD_DATA_HOME/arsh
   * otherwise, indicates $HOME/.local/share/arsh
   */
  const std::string &getDataHome() const { return *this->lookup(DATA_HOME); }

  /**
   * if defined `XDG_DATA_HOME`, indicates $XDG_DATA_HOME/arsh/modules
   * otherwise, indicates $HOME/.local/share/arsh/modules
   */
  const std::string &getModuleHome() const { return *this->lookup(MODULE_HOME); }

  const std::string &getModuleDir() const { return *this->lookup(MODULE_DIR); }
};

} // namespace arsh

#endif // ARSH_SYSCONFIG_H
