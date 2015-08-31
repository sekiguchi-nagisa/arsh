/*
 * Copyright (C) 2015 Nagisa Sekiguchi
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

#ifndef YDSH_LOGGER_BASE_HPP
#define YDSH_LOGGER_BASE_HPP

#include <unistd.h>

#include <iostream>
#include <type_traits>
#include <string>
#include <cstring>
#include <ctime>
#include <functional>

namespace ydsh {
namespace log {
namespace __detail_log {

template <typename P, typename E>
class Logger {
private:
    static_assert(std::is_enum<E>::value, "must be enum type");

    std::ostream *stream;
    bool close;

    /**
     * not delete it
     */
    const bool *whiteList;

    Logger() : stream(nullptr), close(false), whiteList(nullptr) {
        this->stream = &std::cerr;
        this->whiteList = P::init();
    }

public:
    ~Logger() {
        if(this->close) {
            delete this->stream;
            this->stream = nullptr;
        }
    }

    static Logger<P, E> &instance() {
        static Logger<P, E> logger;
        return logger;
    }

    static bool checkPolicy(E e) {
        return instance().whiteList[e];
    }

    static std::ostream &format2digit(std::ostream &stream, int num) {
        return stream << (num < 10 ? "0" : "") << num;
    }

    static std::ostream &header(const char *funcName);
};

template <typename P, typename E>
std::ostream &Logger<P, E>::header(const char *funcName) {
    std::ostream &stream = *instance().stream;

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
bool *initPolicy(const char *prefix, bool (&policyList)[N], const char *arg) {
    for(unsigned int i = 0; i < N; i++) {
        policyList[i] = false;
    }

    // parse arg
    const unsigned int prefixSize = strlen(prefix);
    unsigned int indexCount = 0;
    std::string buf(prefix);
    for(unsigned int i = 0; arg[i] != '\0'; i++) {
        char ch = arg[i];
        if(ch == ' ') {
            continue;
        }
        if(ch == ',') {
            policyList[indexCount++] = getenv(buf.c_str()) != nullptr;
            buf = prefix;
        } else {
            buf += ch;
        }
    }
    if(buf.size() > prefixSize) {
        policyList[indexCount] = getenv(buf.c_str()) != nullptr;
    }
    return policyList;
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
    static bool *init() { \
        static bool policyList[sizeof((Policy[]){ __VA_ARGS__ }) / sizeof(Policy)];\
        return initPolicy(PREFIX, policyList, #__VA_ARGS__);\
    }\
};\
}\
typedef __detail_log::Logger<__detail_log::LoggingPolicy, __detail_log::LoggingPolicy::Policy> Logger;\
}}

#endif //YDSH_LOGGER_BASE_HPP
