/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#ifndef YDSH_MISC_LOGGER_BASE_HPP
#define YDSH_MISC_LOGGER_BASE_HPP

#include <unistd.h>

#include <iostream>
#include <fstream>
#include <type_traits>
#include <string>
#include <cstring>
#include <ctime>

#include "flag_util.hpp"

namespace ydsh {
namespace __detail_log {

inline unsigned int mask(unsigned int index) {
    return 1 << (index + 1);
}

template <typename P, typename E>
class LoggerBase {
private:
    static_assert(std::is_enum<E>::value, "must be enum type");

    static constexpr unsigned int CLOSE_APPENDER = 1 << 0;

    std::ostream *appender;

    /**
     * if least significant bit is set, close appender
     */
    unsigned int whiteList;

    LoggerBase();

public:
    ~LoggerBase() {
        if(hasFlag(this->whiteList, CLOSE_APPENDER)) {
            delete this->appender;
            this->appender = nullptr;
        }
    }

    static LoggerBase<P, E> &instance() {
        static LoggerBase<P, E> logger;
        return logger;
    }

    static bool checkPolicy(E e) {
        return hasFlag(instance().whiteList, mask(e));
    }

    static std::ostream &header(const char *funcName);
};

template <typename P, typename E>
LoggerBase<P, E>::LoggerBase() : appender(&std::cerr), whiteList(P::init()) {
    const char *path = getenv(P::appenderPath());
    if(path != nullptr) {
        std::ostream *os = new std::ofstream(path);
        if(!(*os)) {
            delete os;
            os = nullptr;
        }
        if(os != nullptr) {
            this->appender = os;
            setFlag(this->whiteList, CLOSE_APPENDER);
        }
    }
}

template <typename P, typename E>
std::ostream &LoggerBase<P, E>::header(const char *funcName) {
    std::ostream &stream = *instance().appender;

    time_t timer = time(nullptr);
    struct tm *local = localtime(&timer);

    char buf[32];
    strftime(buf, 32, "%F %T", local);

    return stream << buf << " ["  << getpid() << "] " << funcName << "(): ";
}


template <unsigned int N>
unsigned int initPolicy(const char *prefix, const char *arg) {
    static_assert(N < 32, "not allow more than 31 policy");

    unsigned int policySet = 0;

    // parse arg
    const unsigned int prefixSize = strlen(prefix);
    unsigned int indexCount = 0;
    std::string buf(prefix);
    for(unsigned int i = 0; arg[i] != '\0'; i++) {
        char ch = arg[i];
        switch(ch) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
            continue;
        case ',': {
            if(getenv(buf.c_str()) != nullptr) {
                ydsh::setFlag(policySet, mask(indexCount));
            }
            indexCount++;
            buf = prefix;
            break;
        }
        default:
            buf += ch;
            break;
        }
    }
    if(buf.size() > prefixSize) {
        if(getenv(buf.c_str()) != nullptr) {
            setFlag(policySet, mask(indexCount));
        }
    }
    return policySet;
}

template <typename T>
inline void invoke(std::ostream &stream, T t) {
    t(stream);
    stream << std::endl;
}

} // namespace __detail_log
} // namespace ydsh

#ifdef USE_LOGGING

#define LOG_L(E, B) \
    do {\
        using namespace ydsh;\
        if(__detail_log::Logger::checkPolicy(__detail_log::LoggingPolicy::E)) {\
            __detail_log::invoke(__detail_log::Logger::header(__func__), B);\
        }\
    } while(false)

#define LOG(E, V) LOG_L(E, [&](std::ostream &__stream) { __stream << V; })


#define DEFINE_LOGGING_POLICY(PREFIX, APPENDER, ...) \
namespace ydsh { namespace __detail_log {\
struct LoggingPolicy {\
    enum Policy { __VA_ARGS__ };\
    static unsigned int init() { \
        return initPolicy<sizeof((Policy[]){ __VA_ARGS__ }) / sizeof(Policy)>(PREFIX, #__VA_ARGS__);\
    }\
    static const char *appenderPath() { return PREFIX APPENDER; }\
};\
using Logger = LoggerBase<LoggingPolicy, LoggingPolicy::Policy>;\
}}


#else

#define LOG(E, B)
#define LOG_L(E, B)
#define DEFINE_LOGGING_POLICY(PREFIX, APPENDER, ...)

#endif

#endif //YDSH_MISC_LOGGER_BASE_HPP
