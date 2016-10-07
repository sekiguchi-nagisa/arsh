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

#ifndef YDSH_LOGGER_H
#define YDSH_LOGGER_H

#include <config.h>

#ifdef USE_LOGGING

#include <unistd.h>

#include <iostream>
#include <fstream>
#include <cstring>
#include <ctime>

#include "misc/flag_util.hpp"

#define EACH_LOGGING_POLICY(E) \
    E(TRACE_TOKEN) \
    E(DUMP_EXEC) \
    E(TRACE_SIGNAL) \
    E(DUMP_CONSOLE)


namespace __detail_logger {

enum LoggingPolicy : unsigned int {
#define GEN_ENUM(E) E,
    EACH_LOGGING_POLICY(GEN_ENUM)
#undef GEN_ENUM
};

class Logger {
private:
    std::ostream *stream_;

    unsigned int whiteList_;

public:
    ~Logger() {
        if(this->stream_ != &std::cerr) {
            delete this->stream_;
        }
    }

private:
    Logger() : stream_(nullptr), whiteList_(0) {
        // init appender
        const char *appender = getenv("YDSH_APPENDER");
        if(appender != nullptr) {
            std::ostream *os = new std::ofstream(appender);
            if(!(*os)) {
                delete os;
                os = nullptr;
            }
            this->stream_ = os;
        }
        if(this->stream_ == nullptr) {
            this->stream_ = &std::cerr;
        }

        // init policy
        const char *policies[] = {
#define GEN_STR(E) #E,
                EACH_LOGGING_POLICY(GEN_STR)
#undef GEN_STR
        };

        unsigned int index = 0;
        for(auto &p : policies) {
            const char *env = getenv(p);
            if(env != nullptr) {
                ydsh::setFlag(this->whiteList_, (1u << index));
            }
            index++;
        }
    }

    bool checkPolicy(LoggingPolicy policy) const {
        return ydsh::hasFlag(this->whiteList_, 1u << static_cast<unsigned int>(policy));
    }

    void writeHeader(const char *funcName) {
        time_t timer = time(nullptr);
        struct tm *local = localtime(&timer);

        char buf[32];
        strftime(buf, 32, "%F %T", local);

        *this->stream_ << buf << " ["  << getpid() << "] " << funcName << "(): ";
    }

public:
    template <typename FUNC>
    void operator()(LoggingPolicy policy, const char *funcName, FUNC func) {
        if(this->checkPolicy(policy)) {
            this->writeHeader(funcName);
            func(*this->stream_);
            *this->stream_ << std::endl;
        }
    }

    static Logger &get() {
        static Logger logger;
        return logger;
    }
};

} // namespace __detail_logger


#define LOG_L(P, L) __detail_logger::Logger::get()(__detail_logger::P, __func__, L)
#define LOG(P, V) LOG_L(P, [&](std::ostream & ___s) { ___s << V; })

#else

#define LOG_L(P, L)
#define LOG(P, L) 

#endif

#endif //YDSH_LOGGER_H
