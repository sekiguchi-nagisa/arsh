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

#include <misc/num_util.hpp>
#include <misc/opt.hpp>

#include "driver.h"

using namespace ydsh;
using namespace lsp;

#define XSTR(E) #E
#define STR(E) XSTR(E)

#define EACH_OPT(OP)                                                                               \
  OP(LOG, "--log", opt::HAS_ARG,                                                                   \
     "specify log level (debug, info, warning, error, fatal). default is `warning'")               \
  OP(HELP, "--help", opt::NO_ARG, "show this help message")                                        \
  OP(LSP, "--language-server", opt::NO_ARG, "enable language server features (default)")           \
  OP(DEBOUNCE_TIME, "--debounce-time", opt::HAS_ARG,                                               \
     "time deadline of re-build (ms). default is " STR(DEFAULT_DEBOUNCE_TIME))                     \
  OP(TEST, "--test", opt::HAS_ARG, "run in test mode")                                             \
  OP(TEST_OPEN, "--test-open", opt::HAS_ARG, "run in test mode (explicitly open specified file)")

enum class OptionKind {
#define GEN_ENUM(E, S, F, D) E,
  EACH_OPT(GEN_ENUM)
#undef GEN_ENUM
};

static LogLevel parseLogLevel(const char *value) {
  LogLevel levels[] = {LogLevel::DEBUG, LogLevel::INFO, LogLevel::WARNING, LogLevel::ERROR,
                       LogLevel::FATAL};
  for (auto &l : levels) {
    const char *ls = toString(l);
    if (strcasecmp(ls, value) == 0) {
      return l;
    }
  }
  return LogLevel::WARNING;
}

static DriverOptions parseOptions(int argc, char **argv) {
  opt::Parser<OptionKind> optParser = {
#define GEN_OPT(E, S, F, D) {OptionKind::E, S, F, D},
      EACH_OPT(GEN_OPT)
#undef GEN_OPT
  };
  auto begin = argv + (argc > 0 ? 1 : 0);
  auto end = argv + argc;
  const char *debounceTime = nullptr;
  opt::Result<OptionKind> result;
  DriverOptions options;
  while ((result = optParser(begin, end))) {
    switch (result.value()) {
    case OptionKind::LOG:
      options.level = parseLogLevel(result.arg());
      break;
    case OptionKind::HELP:
      optParser.printOption(stdout);
      exit(0);
    case OptionKind::LSP:
      options.lsp = true;
      break;
    case OptionKind::TEST:
      options.open = false;
      options.testInput = result.arg();
      break;
    case OptionKind::TEST_OPEN:
      options.open = true;
      options.testInput = result.arg();
      break;
    case OptionKind::DEBOUNCE_TIME:
      debounceTime = result.arg();
      break;
    }
  }
  if (result.error() != opt::END) {
    fprintf(stderr, "%s\n", result.formatError().c_str());
    optParser.printOption(stderr);
    exit(1);
  }
  if (debounceTime) {
    auto pair = convertToDecimal<int>(debounceTime);
    if (!pair.second) {
      fprintf(stderr, "require valid number (0~): %s\n", debounceTime);
      exit(1);
    }
    options.debounceTime = pair.first;
  }
  return options;
}

int main(int argc, char **argv) {
  auto options = parseOptions(argc, argv);
  auto driver = createDriver(options);
  if (!driver) {
    fprintf(stderr, "%s\n", driver.asErr().c_str());
    return 1;
  }
  return run(options, argv, *driver.asOk());
}
