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
#include <misc/num_util.hpp>
#include <misc/opt.hpp>

#include "../tools/process/process.h"
#include "client.h"
#include "server.h"

using namespace ydsh;
using namespace lsp;

#define EACH_OPT(OP)                                                                               \
  OP(LOG, "--log", opt::HAS_ARG,                                                                   \
     "specify log level (debug, info, warning, error, fatal). default is `info'")                  \
  OP(HELP, "--help", opt::NO_ARG, "show this help message")                                        \
  OP(LSP, "--language-server", opt::NO_ARG, "enable language server features (default)")           \
  OP(DEBOUNCE_TIME, "--debounce-time", opt::HAS_ARG,                                               \
     "time deadline of re-build (ms). default is 800")                                             \
  OP(TEST, "--test", opt::HAS_ARG, "run in test mode")

enum class OptionKind {
#define GEN_ENUM(E, S, F, D) E,
  EACH_OPT(GEN_ENUM)
#undef GEN_ENUM
};

struct Options {
  LogLevel level{LogLevel::ERROR};
  int debounceTime{800};
  bool lsp{true};
  const char *testInput{nullptr};
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
  return LogLevel::INFO;
}

static Options parseOptions(int argc, char **argv) {
  opt::Parser<OptionKind> optParser = {
#define GEN_OPT(E, S, F, D) {OptionKind::E, S, F, D},
      EACH_OPT(GEN_OPT)
#undef GEN_OPT
  };
  auto begin = argv + (argc > 0 ? 1 : 0);
  auto end = argv + argc;
  const char *debounceTime = nullptr;
  opt::Result<OptionKind> result;
  Options options;
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
    auto pair = convertToNum<int>(debounceTime);
    if (!pair.second) {
      fprintf(stderr, "require valid number (0~): %s\n", debounceTime);
      exit(1);
    }
    options.debounceTime = pair.first;
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

struct Driver {
  virtual ~Driver() = default;

  [[noreturn]] virtual void run(const std::function<int()> &func) = 0;
};

struct SimpleDriver : public Driver {
  void run(const std::function<int()> &func) override {
    int s = func(); // run in same process
    exit(s);
  }
};

class TestClientServerDriver : public Driver {
private:
  LogLevel level;
  std::vector<ClientRequest> requests;

public:
  TestClientServerDriver(LogLevel level, std::vector<ClientRequest> &&requests)
      : level(level), requests(std::move(requests)) {}

  void run(const std::function<int()> &func) override {
    using namespace process;
    IOConfig ioConfig;
    ioConfig.in = IOConfig::PIPE;
    ioConfig.out = IOConfig::PIPE;
    auto proc = ProcBuilder::spawn(ioConfig, [&func] { return func(); });

    ClientLogger logger;
    logger.setSeverity(this->level);
    logger(LogLevel::INFO, "run lsp test client");
    Client client(logger, createFilePtr(fdopen, proc.out(), "r"),
                  createFilePtr(fdopen, proc.in(), "w"));
    client.setReplyCallback([](rpc::Message &&msg) -> bool {
      if (is<rpc::Error>(msg)) {
        auto &error = get<rpc::Error>(msg);
        prettyprint(error.toJSON());
      } else if (is<rpc::Request>(msg)) {
        auto &req = get<rpc::Request>(msg);
        prettyprint(req.toJSON());
      } else if (is<rpc::Response>(msg)) {
        auto &res = get<rpc::Response>(msg);
        prettyprint(res.toJSON());
      } else {
        fatal("broken\n");
      }
      return true;
    });
    client.run(this->requests);
    proc.waitWithTimeout(100);
    proc.kill(SIGKILL);
    auto ret = proc.wait();
    exit(ret.toShellStatus());
  }

private:
  static void prettyprint(const JSON &json) {
    std::string value = json.serialize(2);
    fputs(value.c_str(), stdout);
    fflush(stdout);
  }
};

static Result<std::unique_ptr<Driver>, std::string> createDriver(const Options &options) {
  if (options.testInput) {
    auto inputs = loadInputScript(options.testInput);
    if (!inputs) {
      return Err(std::move(inputs).takeError());
    }
    return Ok(std::make_unique<TestClientServerDriver>(options.level, std::move(inputs).take()));
  } else {
    return Ok(std::make_unique<SimpleDriver>());
  }
}

int main(int argc, char **argv) {
  auto options = parseOptions(argc, argv);
  auto driver = createDriver(options);
  if (!driver) {
    fprintf(stderr, "%s\n", driver.asErr().c_str());
    return 1;
  }
  driver.asOk()->run([&] {
    LSPLogger logger;
    logger.setSeverity(options.level);
    logger.setAppender(FilePtr(stderr));
    showInfo(argv, logger);
    LSPServer server(logger, FilePtr(stdin), FilePtr(stdout), options.debounceTime);
    server.run();
    return 1;
  });
}
