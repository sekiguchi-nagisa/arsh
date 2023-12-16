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

#include <pwd.h>

#include "misc/files.hpp"
#include "misc/unicode_version.in"
#include "regex_wrapper.h"
#include "sysconfig.h"

namespace arsh {

SysConfig::SysConfig() {
  this->values = {
      {VERSION, X_INFO_VERSION_CORE}, {COMPILER, X_INFO_CPP " " X_INFO_CPP_V},
      {UNICODE, UNICODE_VERSION_STR}, {DATA_DIR, X_DATA_DIR},
      {MODULE_DIR, X_MODULE_DIR},     {OSTYPE, BUILD_OS},
      {MACHTYPE, BUILD_ARCH},
  };

  const char *home;
  if (struct passwd *pw = getpwuid(getuid()); likely(pw != nullptr)) {
    home = pw->pw_dir;
  } else {
#ifndef __EMSCRIPTEN__
    fatal_perror("getpwuid failed");
#else
    home = getenv(ENV_HOME);
#endif
  }

  {
    std::string configHome;
    if (auto ptr = getRealpath(getenv("XDG_CONFIG_HOME"))) {
      configHome = ptr.get();
      configHome += "/arsh";
    } else {
      configHome = home;
      configHome += "/.config/arsh";
    }
    this->values.emplace(CONFIG_HOME, std::move(configHome));
  }

  {
    std::string dataHome;
    if (auto ptr = getRealpath(getenv("XDG_DATA_HOME"))) {
      dataHome = ptr.get();
      dataHome += "/arsh";
    } else {
      dataHome = home;
      dataHome += "/.local/share/arsh";
    }

    std::string moduleHome = dataHome;
    moduleHome += "/modules";

    this->values.emplace(DATA_HOME, std::move(dataHome));
    this->values.emplace(MODULE_HOME, std::move(moduleHome));
  }

  {
    std::string value;
    if (auto version = PCRE::version()) {
      value += "PCRE2 ";
      value += std::to_string(version.major);
      value += ".";
      value += std::to_string(version.minor);
    } else {
      value = "null";
    }
    this->values.emplace(REGEX, std::move(value));
  }
}

const std::string *SysConfig::lookup(StringRef key) const {
  auto iter = this->values.find(key);
  if (iter != this->values.end()) {
    return &iter->second;
  }
  return nullptr;
}

} // namespace arsh