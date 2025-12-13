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

#ifndef ARSH_LOGGER_H
#define ARSH_LOGGER_H

#include "misc/flag_util.hpp"
#include "misc/logger_base.hpp"
#include <config.h>

#define EACH_LOGGING_POLICY(E)                                                                     \
  E(TRACE_TOKEN)                                                                                   \
  E(DUMP_EXEC)                                                                                     \
  E(DUMP_WAIT)                                                                                     \
  E(DUMP_TCSETPGRP)                                                                                \
  E(TRACE_MODULE)                                                                                  \
  E(TRACE_EDIT)                                                                                    \
  E(TRACE_JOB)                                                                                     \
  E(TRACE_COMP)

namespace arsh {

#ifdef USE_LOGGING
constexpr bool useLogging = true;
#else
constexpr bool useLogging = false;
#endif

class Logger : public SingletonLogger<Logger> {
private:
  unsigned int whiteList{0};

public:
  enum Policy : unsigned char {
#define GEN_ENUM(E) E,
    EACH_LOGGING_POLICY(GEN_ENUM)
#undef GEN_ENUM
  };

  Logger() : SingletonLogger<Logger>("ARSH") {
    this->sync([&] {
      constexpr const char *policies[] = {
#define GEN_STR(E) "ARSH_" #E,
          EACH_LOGGING_POLICY(GEN_STR)
#undef GEN_STR
      };
      for (unsigned int i = 0; i < std::size(policies); i++) {
        if (getenv(policies[i])) {
          setFlag(this->whiteList, 1u << i);
        }
      }
      this->severity = LogLevel::INFO;
    });
  }

  bool checkPolicy(Policy policy) const {
    return hasFlag(this->whiteList, 1u << static_cast<unsigned int>(policy));
  }
};

} // namespace arsh

#define LOG(P, fmt, ...)                                                                           \
  do {                                                                                             \
    using namespace arsh;                                                                          \
    if (useLogging && Logger::instance().checkPolicy(Logger::P)) {                                 \
      int __old = errno;                                                                           \
      Logger::Info("%s(%s):%d: " fmt, __FILE_NAME__, __func__, __LINE__, ##__VA_ARGS__);           \
      errno = __old;                                                                               \
    }                                                                                              \
  } while (false)

#define LOG_IF(P, B)                                                                               \
  do {                                                                                             \
    using namespace arsh;                                                                          \
    if (useLogging && Logger::instance().checkPolicy(Logger::P)) {                                 \
      B                                                                                            \
    }                                                                                              \
  } while (false)

#define LOG_EXPR(P, FUNC) LOG_IF(P, { LOG(P, "%s", FUNC().c_str()); })

#endif // ARSH_LOGGER_H
