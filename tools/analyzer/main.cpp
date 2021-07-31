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

#include <misc/files.h>
#include <misc/opt.hpp>

#include "server.h"

using namespace ydsh;
using namespace lsp;

#define EACH_OPT(OP)                                                                               \
  OP(LOG, "--log", opt::HAS_ARG,                                                                   \
     "specify log level (debug, info, warning, error, fatal). default is `info'")                  \
  OP(HELP, "--help", opt::NO_ARG, "show this help message")                                        \
  OP(LSP, "--language-server", opt::NO_ARG, "enable language server features (default)")

enum class OptionKind {
#define GEN_ENUM(E, S, F, D) E,
  EACH_OPT(GEN_ENUM)
#undef GEN_ENUM
};

struct Options {
  LogLevel level{LogLevel::INFO};
  bool lsp{true};
};

static LogLevel parseLogLevel(const char *value, LogLevel v) {
  LogLevel levels[] = {LogLevel::DEBUG, LogLevel::INFO, LogLevel::WARNING, LogLevel::ERROR,
                       LogLevel::FATAL};
  for (auto &l : levels) {
    const char *ls = toString(l);
    if (strcasecmp(ls, value) == 0) {
      return l;
    }
  }
  return v;
}

static Options parseOptions(int argc, char **argv) {
  opt::Parser<OptionKind> optParser = {
#define GEN_OPT(E, S, F, D) {OptionKind::E, S, F, D},
      EACH_OPT(GEN_OPT)
#undef GEN_OPT
  };
  auto begin = argv + (argc > 0 ? 1 : 0);
  auto end = argv + argc;
  opt::Result<OptionKind> result;
  Options options;
  while ((result = optParser(begin, end))) {
    switch (result.value()) {
    case OptionKind::LOG:
      options.level = parseLogLevel(result.arg(), LogLevel::INFO);
      break;
    case OptionKind::HELP:
      optParser.printOption(stdout);
      exit(0);
    case OptionKind::LSP:
      options.lsp = true;
      break;
    }
  }
  if (result.error() != opt::END) {
    fprintf(stderr, "%s\n", result.formatError().c_str());
    optParser.printOption(stderr);
    exit(1);
  }
  return options;
}

static void showInfo(char **const argv, LSPLogger &logger) {
  std::string cmdline;
  for (unsigned int i = 0; argv[i]; i++) {
    if (!cmdline.empty()) {
      cmdline += ' ';
    }
    cmdline += argv[i];
  }
  fprintf(stderr, "start ydsh code analyzer with the following options\n");
  fprintf(stderr, "    %s\n", cmdline.c_str());
  fflush(stderr);
  logger(LogLevel::INFO, "working directory: %s", getCWD().get());
}

int main(int argc, char **argv) {
  auto options = parseOptions(argc, argv);
  LSPLogger logger;
  logger.setSeverity(options.level);
  logger.setAppender(FilePtr(stderr));
  showInfo(argv, logger);
  LSPServer server(logger, FilePtr(stdin), FilePtr(stdout));
  server.run();
}
