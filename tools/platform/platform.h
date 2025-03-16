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

#ifndef ARSH_TOOLS_PLATFORM_PLATFORM_H
#define ARSH_TOOLS_PLATFORM_PLATFORM_H

namespace arsh::platform {

// for platform detection

#define EACH_PLATFORM_TYPE(OP)                                                                     \
  OP(UNKNOWN) /* unknown platform */                                                               \
  OP(LINUX)   /* linux */                                                                          \
  OP(DARWIN)  /* macOS */                                                                          \
  OP(CYGWIN)  /* Cygwin */                                                                         \
  OP(MSYS)    /* MSYS2 */                                                                          \
  OP(WSL1)    /* Windows Subsystem for Linux 1 */                                                  \
  OP(WSL2)    /* Windows Subsystem for Linux 2 */

enum class PlatformType : unsigned char {
#define GEN_ENUM(E) E,
  EACH_PLATFORM_TYPE(GEN_ENUM)
#undef GEN_ENUM
};

inline bool isLinux(const PlatformType type) {
  return type == PlatformType::LINUX || type == PlatformType::WSL2;
}

inline bool isCygwinOrMsys(const PlatformType type) {
  return type == PlatformType::CYGWIN || type == PlatformType::MSYS;
}

inline bool isFakeUnix(const PlatformType type) {
  return type == PlatformType::WSL1 || isCygwinOrMsys(type);
}

inline bool isWindows(const PlatformType type) {
  return type == PlatformType::WSL2 || isFakeUnix(type);
}

const char *toString(PlatformType c);

PlatformType platform();

/**
 * if text contains PlatformType constants, return true
 * @param text
 * @param type
 * @return
 */
bool containPlatform(const std::string &text, PlatformType type);

// for processor architecture detection

#define EACH_ARCH_TYPE(OP)                                                                         \
  OP(UNKNOWN, "unknown")                                                                           \
  OP(X86_64, "x86_64|x64|amd64|x86-64")                                                            \
  OP(X86, "x86|i386|i486|i586|i686")                                                               \
  OP(ARM, "arm|aarch32|a32")                                                                       \
  OP(AARCH64, "aarch64|arm64|a64")                                                                 \
  OP(EMSCRIPTEN, "emscripten")

enum class ArchType : unsigned char {
#define GEN_ENUM(E, S) E,
  EACH_ARCH_TYPE(GEN_ENUM)
#undef GEN_ENUM
};

const char *toString(ArchType c);

ArchType arch();

/**
 * if text contains ArchType constants, return true
 * @param text
 * @param type
 * @return
 */
bool containArch(const std::string &text, ArchType type);

/**
 * if text contains platform constant, return true.
 * @param text
 * @return
 */
inline bool contain(const std::string &text) {
  return containPlatform(text, platform()) || containArch(text, arch());
}

} // namespace arsh::platform

#endif // ARSH_TOOLS_PLATFORM_PLATFORM_H
