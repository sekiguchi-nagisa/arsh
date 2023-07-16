/*
 * Copyright (C) 2018 Nagisa Sekiguchi
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

#ifndef MISC_LIB_LOGGER_BASE_HPP
#define MISC_LIB_LOGGER_BASE_HPP

#include <fcntl.h>
#include <unistd.h>

#include <cstdarg>
#include <cstring>
#include <mutex>
#include <string>

#include "fatal.h"
#include "resource.hpp"
#include "time_util.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

enum class LogLevel : unsigned int { DEBUG, INFO, WARNING, ERROR, FATAL, NONE };

inline const char *toString(LogLevel level) {
  const char *str[] = {"debug", "info", "warning", "error", "fatal", "none"};
  return str[static_cast<unsigned int>(level)];
}

namespace detail_logger {

template <bool T>
class LoggerBase {
protected:
  static_assert(T, "not allowed instantiation");

  std::string prefix;
  FilePtr filePtr;
  LogLevel severity{LogLevel::FATAL};
  std::mutex outMutex;

  /**
   * if prefix is empty string, treat as null logger
   * @param prefix
   */
  explicit LoggerBase(const char *prefix) : prefix(prefix) {
    this->sync([&] {
      this->syncSeverityWithEnv();
      this->syncAppenderWithEnv();
    });
  }

  ~LoggerBase() = default;

  void log(LogLevel level, const char *fmt, va_list list);

public:
  // helper method for logger setting.
  template <typename Func>
  void sync(Func func) {
    std::lock_guard<std::mutex> guard(this->outMutex);
    func();
  }

  bool operator()(LogLevel level, const char *fmt, ...) __attribute__((format(printf, 3, 4))) {
    va_list arg;
    va_start(arg, fmt);
    this->log(level, fmt, arg);
    va_end(arg);
    return true;
  }

  bool enabled(LogLevel level) const {
    return static_cast<unsigned int>(level) < static_cast<unsigned int>(LogLevel::NONE) &&
           static_cast<unsigned int>(level) >= static_cast<unsigned int>(this->severity);
  }

  // not-thread safe api.

  void syncSeverityWithEnv();

  void setSeverity(LogLevel level) { this->severity = level; }

  void syncAppenderWithEnv();

  void setAppender(FilePtr &&file) { this->filePtr = std::move(file); }

  const FilePtr &getAppender() const { return this->filePtr; }
};

template <bool T>
void LoggerBase<T>::log(LogLevel level, const char *fmt, va_list list) {
  if (!this->enabled(level)) {
    return;
  }

  // create body
  char *str = nullptr;
  if (vasprintf(&str, fmt, list) == -1) {
    fatal_perror("");
  }

  // create header
  char header[128];
  header[0] = '\0';

  const auto now = timestampToTimespec(getCurrentTimestamp());
  tzset();
  struct tm local {};
  if (localtime_r(&now.tv_sec, &local)) {
    char buf[32];
    strftime(buf, std::size(buf), "%F %T", &local);
    snprintf(header, std::size(header), "%s.%06d <%s> [%d] ", buf,
             static_cast<unsigned int>(now.tv_nsec) / 1000, toString(level), getpid());
  }

  // print body
  fprintf(this->filePtr.get(), "%s%s\n", header, str);
  fflush(this->filePtr.get());
  free(str);

  if (level == LogLevel::FATAL) {
    abort();
  }
}

template <bool T>
void LoggerBase<T>::syncSeverityWithEnv() {
  if (this->prefix.empty()) {
    this->setSeverity(LogLevel::NONE);
    return;
  }

  std::string key = this->prefix;
  key += "_LEVEL";
  const char *level = getenv(key.c_str());
  if (level != nullptr) {
    for (auto i = static_cast<unsigned int>(LogLevel::INFO);
         i < static_cast<unsigned int>(LogLevel::NONE) + 1; i++) {
      auto s = static_cast<LogLevel>(i);
      if (strcasecmp(toString(s), level) == 0) {
        this->setSeverity(s);
        return;
      }
    }
  }
}

template <bool T>
void LoggerBase<T>::syncAppenderWithEnv() {
  std::string key = this->prefix;
  key += "_APPENDER";
  const char *appender = getenv(key.c_str());
  FilePtr file;
  if (appender && *appender != '\0') {
    file = createFilePtr(fopen, appender, "we");
  }
  if (!file) {
    file = createFilePtr(fdopen, fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 0), "w");
  }
  this->setAppender(std::move(file));
}

} // namespace detail_logger

using LoggerBase = detail_logger::LoggerBase<true>;

struct NullLogger : public LoggerBase {
  NullLogger() : LoggerBase("") {}
};

template <typename T>
class SingletonLogger : public LoggerBase, public Singleton<T> {
protected:
  explicit SingletonLogger(const char *prefix) : LoggerBase(prefix) {}

public:
  static void Info(const char *fmt, ...) __attribute__((format(printf, 1, 2))) {
    va_list arg;
    va_start(arg, fmt);
    Singleton<T>::instance().log(LogLevel::INFO, fmt, arg);
    va_end(arg);
  }

  static void Warning(const char *fmt, ...) __attribute__((format(printf, 1, 2))) {
    va_list arg;
    va_start(arg, fmt);
    Singleton<T>::instance().log(LogLevel::WARNING, fmt, arg);
    va_end(arg);
  }

  static void Error(const char *fmt, ...) __attribute__((format(printf, 1, 2))) {
    va_list arg;
    va_start(arg, fmt);
    Singleton<T>::instance().log(LogLevel::ERROR, fmt, arg);
    va_end(arg);
  }

  static void Fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2))) {
    va_list arg;
    va_start(arg, fmt);
    Singleton<T>::instance().log(LogLevel::FATAL, fmt, arg);
    va_end(arg);
  }

  static bool Enabled(LogLevel level) { return Singleton<T>::instance().enabled(level); }
};

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_LOGGER_BASE_HPP
