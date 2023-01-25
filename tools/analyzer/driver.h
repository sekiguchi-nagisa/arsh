/*
 * Copyright (C) 2022 Nagisa Sekiguchi
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

#ifndef YDSH_TOOLS_ANALYZER_DRIVER_H
#define YDSH_TOOLS_ANALYZER_DRIVER_H

#include <functional>

#include <misc/logger_base.hpp>
#include <misc/result.hpp>

namespace ydsh::lsp {

#define DEFAULT_DEBOUNCE_TIME 800

struct DriverOptions {
  LogLevel level{LogLevel::WARNING};
  int debounceTime{DEFAULT_DEBOUNCE_TIME};
  unsigned int waitTime{10};
  bool lsp{true};
  bool open{false};
  const char *testInput{nullptr};
};

struct Driver {
  virtual ~Driver() = default;

  virtual int run(const DriverOptions &options,
                  std::function<int(const DriverOptions &)> &&func) = 0;
};

Result<std::unique_ptr<Driver>, std::string> createDriver(const DriverOptions &options);

int run(const DriverOptions &options, char **argv, Driver &driver);

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_DRIVER_H
