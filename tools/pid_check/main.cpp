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
#include <misc/opt.hpp>

using namespace ydsh;

#define EACH_OPT(OP)                                                                               \
  OP(PID, "--pid", opt::HAS_ARG, "specify pid")                                                    \
  OP(PPID, "--ppid", opt::HAS_ARG, "specify ppid")                                                 \
  OP(FIRST, "--first", opt::NO_ARG, "treat as first process of pipeline")                          \
  OP(HELP, "--help", opt::NO_ARG, "show help message")

enum class OptionSet : unsigned int {
#define GEN_ENUM(E, S, F, D) E,
  EACH_OPT(GEN_ENUM)
#undef GEN_ENUM
};

static int toInt32(const char *str) {
  long value = std::stol(str);
  if (value > INT32_MAX || value < INT32_MIN) {
    fatal("broken number: %s\n", str);
  }
  return static_cast<int>(value);
}

static void assertPID(pid_t pid, pid_t ppid) {
  if (pid > -1 && pid != getpid()) {
    std::cout << "expect pid: " << pid << ", but actual: " << getpid() << std::endl;
    exit(1);
  }

  if (ppid > -1 && ppid != getppid()) {
    std::cout << "expect ppid: " << ppid << ", but actual: " << getppid() << std::endl;
    exit(1);
  }

  std::cout << "OK" << std::endl;
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
  std::cout << str << std::endl;
}

int main(int argc, char **argv) {
  opt::Parser<OptionSet> parser = {
#define GEN_OPT(E, S, F, D) {OptionSet::E, S, (F), D},
      EACH_OPT(GEN_OPT)
#undef GEN_OPT
  };

  pid_t pid = -1;
  pid_t ppid = -1;
  bool isFirst = false;

  char **begin = argv + 1;
  char **end = argv + argc;
  opt::Result<OptionSet> result;
  while ((result = parser(begin, end))) {
    switch (result.value()) {
    case OptionSet::PID:
      pid = toInt32(result.arg());
      break;
    case OptionSet::PPID:
      ppid = toInt32(result.arg());
      break;
    case OptionSet::FIRST:
      isFirst = true;
      break;
    case OptionSet::HELP:
      parser.printOption(stdout);
      exit(1);
    }
  }
  if (result.error() != opt::END) {
    fprintf(stderr, "%s\n", result.formatError().c_str());
    parser.printOption(stderr);
    exit(1);
  }

  if (pid > -1 || ppid > -1) {
    assertPID(pid, ppid);
  } else {
    dumpPID(isFirst);
  }
  return 0;
}