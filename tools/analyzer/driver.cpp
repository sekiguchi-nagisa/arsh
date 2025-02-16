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

#include <misc/files.hpp>
#include <misc/time_util.hpp>

#include "client.h"
#include "driver.h"
#include "server.h"

namespace arsh::lsp {

struct SimpleDriver : public Driver {
  int run(const DriverOptions &options, std::function<int(const DriverOptions &)> &&func) override {
    return func(options); // run in same process
  }
};

Result<std::unique_ptr<Driver>, std::string> createDriver(const DriverOptions &options) {
  if (options.testInput) {
    auto input =
        loadInputScript(options.testInput, options.open, options.waitTime + options.debounceTime);
    if (!input) {
      return Err(std::move(input).takeError());
    }
    return Ok(std::make_unique<TestClientServerDriver>(options.level, std::move(input).take()));
  } else {
    return Ok(std::make_unique<SimpleDriver>());
  }
}

static void showInfo(char **const argv, LSPLogger &logger) {
  std::string cmdline;
  for (unsigned int i = 0; argv[i]; i++) {
    if (!cmdline.empty()) {
      cmdline += ' ';
    }
    cmdline += argv[i];
  }
  fprintf(stderr, "start arsh code analyzer with the following options\n");
  fprintf(stderr, "    %s\n", cmdline.c_str());
  fflush(stderr);
  logger(LogLevel::INFO, "working directory: %s", getCWD().get());
}

static std::string getBaseDir(const char *path) {
  auto fullPath = getRealpath(path);
  if (!fullPath) {
    fatal_perror("broken path: %s", path);
  }
  StringRef ref = fullPath.get();
  auto index = ref.lastIndexOf("/");
  return index == 0 ? "/" : ref.slice(0, index).toString();
}

int run(const DriverOptions &opts, char **const argv, Driver &driver) {
  return driver.run(opts, [&argv](const DriverOptions &options) {
    LSPLogger logger;
    logger.setSeverity(options.level);
    logger.setAppender(FilePtr(stderr));
    showInfo(argv, logger);
    const bool testMode = options.testInput && !options.open;
    uint64_t seed = testMode ? 42 : getCurrentTimestamp().time_since_epoch().count();
    LSPServer server(logger, dupFD(STDIN_FILENO), dupFD(STDOUT_FILENO), options.debounceTime,
                     testMode ? getBaseDir(options.testInput) : "", seed);
    server.run();
    return 1;
  });
}

} // namespace arsh::lsp