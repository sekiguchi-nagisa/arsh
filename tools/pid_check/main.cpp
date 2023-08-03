/*
 * Copyright (C) 2017 Nagisa Sekiguchi
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

#include <iostream>
#include <vector>

#include <misc/fatal.h>
#include <misc/opt_parser.hpp>

using namespace ydsh;

enum class OptionSet : unsigned int {
  PID,
  PPID,
  FIRST,
  HELP,
};

static constexpr OptParser<OptionSet>::Option options[] = {
    {OptionSet::PID, 0, "pid", OptParseOp::HAS_ARG, "specify pid"},
    {OptionSet::PPID, 0, "ppid", OptParseOp::HAS_ARG, "specify ppid"},
    {OptionSet::FIRST, 0, "first", OptParseOp::NO_ARG, "treat as first process of pipeline"},
    {OptionSet::HELP, 'h', "help", OptParseOp::NO_ARG, "show help message"},
};

static int toInt32(StringRef ref) {
  std::string str = ref.toString();
  long value = std::stol(str);
  if (value > INT32_MAX || value < INT32_MIN) {
    fatal("broken number: %s\n", str.c_str());
  }
  return static_cast<int>(value);
}

static void assertPID(pid_t pid, pid_t ppid) {
  if (pid > -1 && pid != getpid()) {
    std::cout << "expect pid: " << pid << ", but actual: " << getpid() << '\n' << std::flush;
    exit(1);
  }

  if (ppid > -1 && ppid != getppid()) {
    std::cout << "expect ppid: " << ppid << ", but actual: " << getppid() << '\n' << std::flush;
    exit(1);
  }

  std::cout << "OK" << '\n' << std::flush;
}

/**
 * [pid,ppid,pgid]
 * @return
 */
static std::string getFormattedPID() {
  std::string str = "[";
  str += std::to_string(getpid());
  str += ",";
  str += std::to_string(getppid());
  str += ",";
  str += std::to_string(getpgrp());
  str += "]";
  return str;
}

/**
 * dump pid, ppid, pgid.
 */
static void dumpPID(bool isFirst) {
  std::string str = getFormattedPID();

  if (!isFirst) {
    if (isatty(STDIN_FILENO) != 0) {
      fatal("standard input must not be tty\n");
    }

    std::vector<std::string> buf;
    for (std::string line; std::getline(std::cin, line);) {
      buf.push_back(std::move(line));
    }

    if (buf.size() != 1) {
      fatal("broken standard input\n");
    }
    std::cout << buf.back() << " ";
  }
  std::cout << str << '\n' << std::flush;
}

int main(int argc, char **argv) {
  auto parser = createOptParser(options);

  pid_t pid = -1;
  pid_t ppid = -1;
  bool isFirst = false;

  char **begin = argv + 1;
  char **end = argv + argc;
  OptParseResult<OptionSet> result;
  while ((result = parser(begin, end))) {
    switch (result.getOpt()) {
    case OptionSet::PID:
      pid = toInt32(result.getValue());
      break;
    case OptionSet::PPID:
      ppid = toInt32(result.getValue());
      break;
    case OptionSet::FIRST:
      isFirst = true;
      break;
    case OptionSet::HELP:
      printf("%s\n", parser.formatUsage().c_str());
      return 1;
    }
  }
  if (result.isError()) {
    fprintf(stderr, "%s\n%s\n", result.formatError().c_str(), parser.formatUsage().c_str());
    return 1;
  }

  if (pid > -1 || ppid > -1) {
    assertPID(pid, ppid);
  } else {
    dumpPID(isFirst);
  }
  return 0;
}