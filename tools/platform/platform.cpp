/*
 * Copyright (C) 2019 Nagisa Sekiguchi
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

#include <fstream>
#include <regex>

#include "platform.h"
#include <constant.h>

namespace arsh::platform {

static bool reSearch(const char *reStr, const std::string &value) {
  std::regex re(reStr, std::regex_constants::ECMAScript | std::regex_constants::icase);
  std::smatch match;
  return std::regex_search(value, match, re);
}

const char *toString(PlatformType c) {
  constexpr const char *table[] = {
#define GEN_STR(E) #E,
      EACH_PLATFORM_TYPE(GEN_STR)
#undef GEN_STR
  };
  return table[toUnderlying(c)];
}

static bool detectContainer() {
  std::ifstream stream("/proc/self/cgroup");
  if (!stream) {
    return false;
  }
  for (std::string line; std::getline(stream, line);) {
    if (reSearch("docker|lxc|containerd", line)) {
      return true;
    }
  }
  return false;
}

static PlatformType detectImpl() {
  struct utsname name {};
  if (uname(&name) == -1) {
    return PlatformType::UNKNOWN;
  }

  const StringRef buildOS = BUILD_OS;
  if (buildOS == "linux") {
    if (reSearch("microsoft", name.release)) {
      return PlatformType::WSL;
    }
    if (detectContainer()) {
      return PlatformType::CONTAINER;
    }
    return PlatformType::LINUX;
  } else if (buildOS == "darwin") {
    return PlatformType::DARWIN;
  } else if (buildOS == "cygwin") {
    return PlatformType::CYGWIN;
  } else if (buildOS == "msys") {
    return PlatformType::MSYS;
  }
  return PlatformType::UNKNOWN;
}

PlatformType platform() {
  static const auto p = detectImpl();
  return p;
}

bool containPlatform(const std::string &text, PlatformType type) {
  return reSearch(toString(type), text);
}

const char *toString(ArchType c) {
  constexpr const char *table[] = {
#define GEN_STR(E, S) #E,
      EACH_ARCH_TYPE(GEN_STR)
#undef GEN_STR
  };
  return table[toUnderlying(c)];
}

static ArchType archImpl() {
  constexpr ArchType types[] = {
#define GEN_ENUM(E, S) ArchType::E,
      EACH_ARCH_TYPE(GEN_ENUM)
#undef GEN_ENUM
  };
  for (auto &type : types) {
    if (containArch(BUILD_ARCH, type)) {
      return type;
    }
  }
  return ArchType::UNKNOWN;
}

ArchType arch() {
  static const auto a = archImpl();
  return a;
}

bool containArch(const std::string &text, ArchType type) {
  constexpr const char *table[] = {
#define GEN_STR(E, S) #E "|" S,
      EACH_ARCH_TYPE(GEN_STR)
#undef GEN_STR
  };
  return reSearch(table[toUnderlying(type)], text);
}

} // namespace arsh::platform