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

static bool reSearch(const std::string &reStr, const std::string &value) {
    std::regex re(reStr, std::regex_constants::ECMAScript | std::regex_constants::icase);
    std::smatch match;
    return std::regex_search(value, match, re);
}

static std::string toString(PlatformType c) {
    const char *table[] = {
#define GEN_STR(E, B) #E,
            EACH_PLATFORM_TYPE(GEN_STR)
#undef GEN_STR
    };

    unsigned int bits[] = {
#define GEN_BIT(E, B) B,
            EACH_PLATFORM_TYPE(GEN_BIT)
#undef GEN_BIT
    };

    std::string str;
    for(unsigned int i = 0; i < arraySize(bits); i++) {
        auto v = bits[i];
        if(hasFlag(static_cast<unsigned int>(c), v)) {
            if(!str.empty()) {
                str += "|";
            }
            str += table[i];
        }
    }
    return str;
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
        auto p = PlatformType::LINUX;
        if(detectContainer()) {
            p |= PlatformType::CONTAINER;
        }
        return p;
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