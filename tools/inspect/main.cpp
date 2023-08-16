/*
 * Copyright (C) 2019 Nagisa Sekiguchi
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

#include <unistd.h>

#include <fstream>
#include <iomanip>
#include <iostream>

#include <misc/fatal.h>
#include <misc/opt_parser.hpp>

#include "../platform/platform.h"
#include <signals.h>

using namespace ydsh;

enum class OptionSet : unsigned int {
  OUT,
  HELP,
};

static const OptParser<OptionSet>::Option options[] = {
    {OptionSet::OUT, 'o', "out", OptParseOp::HAS_ARG, "specify output file. default is stdout"},
    {OptionSet::HELP, 'h', "help", OptParseOp::NO_ARG, "show help message"},
};

static std::ostream &format(std::ostream &stream) {
  return stream << std::left << std::setw(8) << std::setfill(' ');
}

static void showPIDs(std::ostream &stream) {
  stream << "+++++  PIDs  +++++" << '\n';

  struct {
    const char *name;
    long id;
  } lists[] = {
      {"PID", getpid()},
      {"PPID", getppid()},
      {"PGID", getpgrp()},
      {"SID", getsid(0)},
  };
  for (auto &e : lists) {
    stream << format << e.name << " => " << e.id << '\n';
  }
  stream << '\n';
}

static void showTCPGID(int fd, std::ostream &stream) {
  std::string name = "TCPGID_";
  name += std::to_string(fd);

  errno = 0;
  auto pid = tcgetpgrp(fd);
  int errNum = errno;
  stream << format << name << " => " << pid << '\n';
  stream << format << "errno"
         << " => " << errNum << ": " << strerror(errNum) << '\n';
}

static void showPGroup(std::ostream &stream) {
  stream << "+++++  foreground process group  +++++" << '\n';

  stream << format << "STDIN"
         << " => " << (isatty(STDIN_FILENO) ? "TTY" : "NOTTY") << '\n';
  stream << format << "STDOUT"
         << " => " << (isatty(STDOUT_FILENO) ? "TTY" : "NOTTY") << '\n';
  stream << format << "STDERR"
         << " => " << (isatty(STDERR_FILENO) ? "TTY" : "NOTTY") << '\n';

  showTCPGID(STDIN_FILENO, stream);
  showTCPGID(STDOUT_FILENO, stream);
  showTCPGID(STDERR_FILENO, stream);
  stream << '\n';
}

static void showSignals(std::ostream &stream) {
  auto lists = getUniqueSignalList();
  stream << "+++++  signal handler setting  +++++" << '\n';
  for (auto &e : lists) {
    stream << format << getSignalName(e) << " => ";
    struct sigaction action {};
    sigaction(e, nullptr, &action);
    if (action.sa_handler == SIG_DFL) {
      stream << "SIG_DFL";
    } else if (action.sa_handler == SIG_IGN) {
      stream << "SIG_IGN";
    } else {
      stream << std::hex << reinterpret_cast<uintptr_t>(action.sa_handler);
    }
    stream << '\n';
  }
  stream << '\n';
}

static void showPlat(std::ostream &stream) {
  stream << "+++++  platform info  +++++" << '\n';
  stream << format << "platform"
         << " => " << platform::toString(platform::platform()) << '\n';
  stream << format << "arch"
         << " => " << platform::toString(platform::arch()) << '\n';
  stream << '\n';
}

static void showInfo(std::ostream &stream) {
  showPlat(stream);
  showPIDs(stream);
  showPGroup(stream);
  showSignals(stream);
}

int main(int argc, char **argv) {
  auto parser = createOptParser(options);

  StringRef output;

  char **begin = argv + 1;
  char **end = argv + argc;
  OptParseResult<OptionSet> result;
  while ((result = parser(begin, end))) {
    switch (result.getOpt()) {
    case OptionSet::OUT:
      output = result.getValue();
      break;
    case OptionSet::HELP:
      printf("%s\n", parser.formatOptions().c_str());
      exit(0);
    }
  }
  if (result.isError()) {
    fprintf(stderr, "%s\n%s\n", result.formatError().c_str(), parser.formatOptions().c_str());
    exit(1);
  }

  if (!output.empty()) {
    std::ofstream stream(output.toString());
    if (!stream) {
      fatal_perror("cannot open file: %s", output.toString().c_str());
    }
    showInfo(stream);
  } else {
    showInfo(std::cout);
  }
  return 0;
}