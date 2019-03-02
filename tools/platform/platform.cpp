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

#include <regex>
#include <fstream>

#include <misc/util.hpp>
#include <misc/flag_util.hpp>
#include "platform.h"

namespace ydsh {
namespace platform {

static bool reSearch(const char *reStr, const std::string &value) {
    std::regex re(reStr, std::regex_constants::ECMAScript | std::regex_constants::icase);
    std::smatch match;
    return std::regex_search(value, match, re);
}

const char *toString(PlatformType c) {
    const char *table[] = {
#define GEN_STR(E) #E,
            EACH_PLATFORM_TYPE(GEN_STR)
#undef GEN_STR
    };
    return table[static_cast<unsigned int>(c)];
}

static bool detectContainer() {
    std::ifstream stream("/proc/1/cgroup");
    if(!stream) {
        return false;
    }
    for(std::string line; std::getline(stream, line); ) {
        if(reSearch("docker|lxc", line)) {
            return true;
        }
    }
    return false;
}

static PlatformType detectImpl() {
    struct utsname name{};
    if(uname(&name) == -1) {
        return PlatformType::UNKNOWN;
    }

    std::string sysName = name.sysname;
    if(reSearch("linux", sysName)) {
        if(reSearch("microsoft", name.release)) {
            return PlatformType::WSL;
        }
        if(detectContainer()) {
            return PlatformType::CONTAINER;
        }
        return PlatformType::LINUX;
    }
    if(reSearch("darwin", sysName)) {
        return PlatformType::DARWIN;
    }
    if(reSearch("cygwin", sysName)) {
        return PlatformType::CYGWIN;
    }
    return PlatformType::UNKNOWN;
}

PlatformType detect() {
    static auto p = detectImpl();
    return p;
}

bool contain(const std::string &text) {
    return reSearch(toString(detect()), text);
}

} // namespace platform
} // namespace ydsh