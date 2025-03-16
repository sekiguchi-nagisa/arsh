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

static PlatformType detectPlatform() {
  struct utsname name{};
  if (uname(&name) == -1) {
    return PlatformType::UNKNOWN;
  }

  constexpr StringRef buildOS = BUILD_OS;
  if (buildOS == "linux") {
    if (const StringRef release = name.release; release.contains("Microsoft")) {
      return PlatformType::WSL1;
    } else if (release.contains("microsoft-standard")) {
      return PlatformType::WSL2;
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
  static const auto p = detectPlatform();
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

static constexpr ArchType detectArch() {
  constexpr std::pair<ArchType, std::string_view> table[] = {
#define GEN_ENUM(E, S) {ArchType::E, S},
      EACH_ARCH_TYPE(GEN_ENUM)
#undef GEN_ENUM
  };
  for (const auto &e : table) {
    if (e.second.find(BUILD_ARCH) != std::string_view::npos) {
      return e.first;
    }
  }
  return ArchType::UNKNOWN;
}

ArchType arch() {
  static constexpr auto a = detectArch();
  static_assert(a != ArchType::UNKNOWN);
  return a;
}

bool containArch(const std::string &text, ArchType type) {
  constexpr const char *table[] = {
#define GEN_STR(E, S) S,
      EACH_ARCH_TYPE(GEN_STR)
#undef GEN_STR
  };
  return reSearch(table[toUnderlying(type)], text);
}

} // namespace arsh::platform