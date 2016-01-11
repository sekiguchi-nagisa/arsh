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
namespace log {
namespace __detail_log {

inline unsigned int mask(unsigned int index) {
    return 1 << (index + 1);
}

template <typename P, typename E>
class Logger {
private:
    static_assert(std::is_enum<E>::value, "must be enum type");

    static constexpr unsigned int CLOSE_APPENDER = 1 << 0;

    std::ostream *appender;

    /**
     * if least significant bit is set, close appender
     */
    unsigned int whiteList;

    Logger();

public:
    ~Logger() {
        if(ydsh::misc::hasFlag(this->whiteList, CLOSE_APPENDER)) {
            delete this->appender;
            this->appender = nullptr;
        }
    }

    static Logger<P, E> &instance() {
        static Logger<P, E> logger;
        return logger;
    }

    static bool checkPolicy(E e) {
        return ydsh::misc::hasFlag(instance().whiteList, mask(e));
    }

    static std::ostream &format2digit(std::ostream &stream, int num) {
        return stream << (num < 10 ? "0" : "") << num;
    }

    static std::ostream &header(const char *funcName);
};

template <typename P, typename E>
Logger<P, E>::Logger() : appender(&std::cerr), whiteList(P::init()) {
    const char *path = getenv(P::appenderPath());
    if(path != nullptr) {
        std::ostream *os = new std::ofstream(path);
        if(!(*os)) {
            delete os;
            os = nullptr;
        }
        if(os != nullptr) {
            this->appender = os;
            ydsh::misc::setFlag(this->whiteList, CLOSE_APPENDER);
        }
    }
}

template <typename P, typename E>
std::ostream &Logger<P, E>::header(const char *funcName) {
    std::ostream &stream = *instance().appender;

    time_t timer = time(nullptr);
    struct tm *local = localtime(&timer);

    stream << (local->tm_year + 1900) << "-";
    format2digit(stream, local->tm_mon + 1) << "-";
    format2digit(stream, local->tm_mday) << " ";
    format2digit(stream, local->tm_hour) << ":";
    format2digit(stream, local->tm_min) << ":";
    format2digit(stream, local->tm_sec);

    return stream << " ["  << getpid() << "] " << funcName << "(): ";
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
                ydsh::misc::setFlag(policySet, mask(indexCount));
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
            ydsh::misc::setFlag(policySet, mask(indexCount));
        }
    }
    return policySet;
}

} // namespace __detail_log

template <typename T>
std::ostream &invoke(std::ostream &stream, T t) {
    t(stream);
    return stream;
}


#ifdef USE_LOGGING
constexpr bool enableLogging = true;
#else
constexpr bool enableLogging = false;
#endif

} // namespace log
} // namespace ydsh


#define LOG(E, B) \
    do {\
        using namespace ydsh::log;\
        if(enableLogging && Logger::checkPolicy(__detail_log::LoggingPolicy::E)) {\
            Logger::header(__func__) << B << std::endl;\
        }\
    } while(false)

#define LOG_L(E, B) \
    do {\
        using namespace ydsh::log;\
        if(enableLogging && Logger::checkPolicy(__detail_log::LoggingPolicy::E)) {\
            invoke(Logger::header(__func__), B) << std::endl;\
        }\
    } while(false)


#define DEFINE_LOGGING_POLICY(PREFIX, APPENDER, ...) \
namespace ydsh { namespace log { namespace __detail_log {\
struct LoggingPolicy {\
    enum Policy { __VA_ARGS__ };\
    static unsigned int init() { \
        return initPolicy<sizeof((Policy[]){ __VA_ARGS__ }) / sizeof(Policy)>(PREFIX, #__VA_ARGS__);\
    }\
    static const char *appenderPath() { return PREFIX APPENDER; }\
};\
}\
typedef __detail_log::Logger<__detail_log::LoggingPolicy, __detail_log::LoggingPolicy::Policy> Logger;\
}}

#endif //YDSH_MISC_LOGGER_BASE_HPP
