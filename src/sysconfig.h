/*
 * Copyright (C) 2020 Nagisa Sekiguchi
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

#ifndef YDSH_SYSCONFIG_H
#define YDSH_SYSCONFIG_H

#include "constant.h"

namespace ydsh {

/**
 * runtime system configuration constants
 */
class SysConfig {
public:
  static constexpr const char *COMPILER = "%compiler";
  static constexpr const char *REGEX = "%regex";

  static constexpr const char *VERSION = "YDSH_VERSION";
  static constexpr const char *OSTYPE = "OSTYPE";
  static constexpr const char *MACHTYPE = "MACHTYPE";

  static constexpr const char *CONFIG_HOME = "CONFIG_HOME";
  static constexpr const char *DATA_HOME = "DATA_HOME";
  static constexpr const char *MODULE_HOME = "MODULE_HOME";
  static constexpr const char *DATA_DIR = "DATA_DIR";
  static constexpr const char *MODULE_DIR = "MODULE_DIR";

private:
  StrRefMap<std::string> values;

public:
  SysConfig();

  const StrRefMap<std::string> &getValues() const { return this->values; }

  const std::string *lookup(StringRef key) const;

  /**
   * if defined `XDG_CONFIG_HOME`, indicates $XDG_CONFIG_HOME/ydsh
   * otherwise, indicates $HOME/.config/ydsh
   */
  const std::string &getConfigHome() const { return *this->lookup(CONFIG_HOME); }

  /**
   * if defined `XDG_DATA_HOME`, indicates $XGD_DATA_HOME/ydsh
   * otherwise, indicates $HOME/.local/share/ydsh
   */
  const std::string &getDataHome() const { return *this->lookup(DATA_HOME); }

  /**
   * if defined `XDG_DATA_HOME`, indicates $XDG_DATA_HOME/ydsh/module
   * otherwise, indicates $HOME/.local/share/ydsh/module
   */
  const std::string &getModuleHome() const { return *this->lookup(MODULE_HOME); }

  const std::string &getModuleDir() const { return *this->lookup(MODULE_DIR); }
};

} // namespace ydsh

#endif // YDSH_SYSCONFIG_H
