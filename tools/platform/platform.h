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

#ifndef YDSH_TOOLS_PLATFORM_H
#define YDSH_TOOLS_PLATFORM_H

namespace ydsh {
namespace platform {

// for platform detection

#define EACH_PLATFORM_TYPE(OP) \
    OP(UNKNOWN) /* unknown platform */\
    OP(LINUX) /* linux (not container) */\
    OP(CONTAINER) /* linux container (docker/LXC) */ \
    OP(DARWIN) /* MacOSX */\
    OP(CYGWIN) /* Cygwin */\
    OP(WSL) /* Windows Subsystem for Linux */


enum class PlatformType : unsigned int {
#define GEN_ENUM(E) E,
    EACH_PLATFORM_TYPE(GEN_ENUM)
#undef GEN_ENUM
};

inline bool isLinux(PlatformType type) {
    return type == PlatformType::LINUX || type == PlatformType::CONTAINER;
}

inline bool isWindows(PlatformType type) {
    return type == PlatformType::WSL || type == PlatformType::CYGWIN;
}

const char *toString(PlatformType c);

PlatformType platform();

bool containPlatform(const std::string &text, PlatformType type);

// for processor architecture detection

#define EACH_ARCH_TYPE(OP) \
    OP(UNKNOWN, "unknown") \
    OP(X86_64, "x64 | amd64 | x86-64") \
    OP(AARCH64, "arm64 | a64")

enum class ArchType : unsigned int {
#define GEN_ENUM(E, S) E,
    EACH_ARCH_TYPE(GEN_ENUM)
#undef GEN_ENUM
};

const char *toString(ArchType c);

ArchType arch();

bool containArch(const std::string &text, ArchType type);

/**
 * if text contains platform constant, return true.
 * @param text
 * @return
 */
inline bool contain(const std::string &text) {
    return containPlatform(text, platform()) || containArch(text, arch());
}

} // namespace platform
} // namespace ydsh

#endif //YDSH_TOOLS_PLATFORM_H
