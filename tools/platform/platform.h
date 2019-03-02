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

#define EACH_PLATFORM_TYPE(OP) \
    OP(UNKNOWN, 0) \
    OP(LINUX,   1 << 0) \
    OP(DARWIN,  1 << 1) \
    OP(CYGWIN,  1 << 2) \
    OP(WSL,     1 << 3)


enum class PlatformType : unsigned int {
#define GEN_ENUM(E, B) E,
    EACH_PLATFORM_TYPE(GEN_ENUM)
#undef GEN_ENUM
};

PlatformType detect();

/**
 * if text contains platform constant, return true.
 * @param text
 * @return
 */
bool contain(const std::string &text);

} // namespace platform
} // namespace ydsh

#endif //YDSH_TOOLS_PLATFORM_H
