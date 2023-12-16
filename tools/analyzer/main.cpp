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
#include <misc/opt_parser.hpp>

#include "driver.h"

using namespace arsh;
using namespace lsp;

#define XSTR(E) #E
#define STR(E) XSTR(E)

enum class OptionKind : unsigned char {
  LOG,
  HELP,
  LSP,
  DEBOUNCE_TIME,
  TEST,
  TEST_OPEN,
  WAIT_TIME,
};

static const OptParser<OptionKind>::Option optOptions[] = {
    {OptionKind::LOG, 0, "log", OptParseOp::HAS_ARG, "level",
     "specify log level (debug, info, warning, error, fatal). default is `warning'"},
    {OptionKind::LSP, 0, "language-server", OptParseOp::NO_ARG,
     "enable language server features (default)"},
    {OptionKind::DEBOUNCE_TIME, 0, "debounce-time", OptParseOp::HAS_ARG, "msec",
     "time deadline of re-build (ms). default is " STR(DEFAULT_DEBOUNCE_TIME)},
    {OptionKind::TEST, 0, "test", OptParseOp::HAS_ARG, "file", "run in test mode"},
    {OptionKind::TEST_OPEN, 0, "test-open", OptParseOp::HAS_ARG, "file",
     "run in test mode (explicitly open specified file)"},
    {OptionKind::WAIT_TIME, 0, "wait-time", OptParseOp::HAS_ARG, "msec",
     "specify wait time (ms) for test-open mode, default is 10"},
    {OptionKind::HELP, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
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
  auto optParser = createOptParser(optOptions);
  auto begin = argv + (argc > 0 ? 1 : 0);
  auto end = argv + argc;
  const char *debounceTime = nullptr;
  const char *waitTime = nullptr;
  OptParseResult<OptionKind> result;
  DriverOptions options;
  while ((result = optParser(begin, end))) {
    switch (result.getOpt()) {
    case OptionKind::LOG:
      options.level = parseLogLevel(result.getValue().data());
      break;
    case OptionKind::HELP:
      printf("%s\n", optParser.formatOptions().c_str());
      exit(0);
    case OptionKind::LSP:
      options.lsp = true;
      break;
    case OptionKind::TEST:
      options.open = false;
      options.testInput = result.getValue().data();
      break;
    case OptionKind::TEST_OPEN:
      options.open = true;
      options.testInput = result.getValue().data();
      break;
    case OptionKind::DEBOUNCE_TIME:
      debounceTime = result.getValue().data();
      break;
    case OptionKind::WAIT_TIME:
      waitTime = result.getValue().data();
      break;
    }
  }
  if (result.isError()) {
    fprintf(stderr, "%s\n%s\n", result.formatError().c_str(), optParser.formatOptions().c_str());
    exit(1);
  }
  if (debounceTime) {
    auto pair = convertToDecimal<int>(debounceTime);
    if (!pair) {
      fprintf(stderr, "require valid number (0~): %s\n", debounceTime);
      exit(1);
    }
    options.debounceTime = pair.value;
  }
  if (waitTime) {
    auto pair = convertToDecimal<unsigned int>(waitTime);
    if (!pair) {
      fprintf(stderr, "require valid number (0~): %s\n", waitTime);
      exit(1);
    }
    options.waitTime = pair.value;
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
