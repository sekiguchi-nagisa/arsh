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

#include <fstream>
#include <iostream>

#include <misc/opt.hpp>

#include "factory.h"

namespace {

using namespace ydsh;
using namespace ydsh::highlighter;

#define EACH_OPT(OP)                                                                               \
  OP(HELP, "--help", opt::NO_ARG, "show help message")                                             \
  OP(HELP2, "-h", opt::NO_ARG, "show help message")                                                \
  OP(OUTPUT, "-o", opt::HAS_ARG, "specify output file (default is stdout)")                        \
  OP(FORMAT, "-f", opt::HAS_ARG, "specify output formatter (default is `ansi' formatter)")         \
  OP(STYLE, "-s", opt::HAS_ARG, "specify highlighter color style (default is `darcula' style)")

enum class OptionSet : unsigned int {
#define GEN_ENUM(E, S, F, D) E,
  EACH_OPT(GEN_ENUM)
#undef GEN_ENUM
};

void usage(std::ostream &stream, char **argv) {
  stream << "usage: " << argv[0] << " ([option..]) [source file]" << std::endl;
}

static Optional<std::string> readAll(const char *sourceName) {
  std::string buf;
  auto file = createFilePtr(fopen, sourceName, "rb");
  if (!file) {
    std::cerr << "cannot open file: " << sourceName << ", by `" << strerror(errno) << "'"
              << std::endl;
    return {};
  }

  if (!readAll(file, buf)) {
    std::cerr << "cannot read file: " << sourceName << ", by `" << strerror(errno) << "'"
              << std::endl;
    return {};
  }

  if (buf.empty() || buf.back() != '\n') {
    buf += '\n';
  }
  return buf;
}

static bool colorize(FormatterFactory &factory, const char *sourceName, std::ostream &output) {
  auto content = readAll(sourceName);
  if (!content.hasValue()) {
    return false;
  }

  factory.setSource(content.unwrap());
  auto ret = factory.create(output);
  if (!ret) {
    std::cerr << ret.asErr() << std::endl;
    return false;
  }
  auto formatter = std::move(ret).take();
  assert(formatter);

  doHighlight(*formatter, sourceName, content.unwrap());
  formatter->flush();
  return true;
}

} // namespace

int main(int argc, char **argv) {
  opt::Parser<OptionSet> parser = {
#define GEN_OPT(E, S, F, D) {OptionSet::E, S, (F), D},
      EACH_OPT(GEN_OPT)
#undef GEN_OPT
  };

  auto begin = argv + 1;
  auto end = argv + argc;
  opt::Result<OptionSet> result;

  const char *outputFileName = "/dev/stdout";
  FormatterFactory factory;
  while ((result = parser(begin, end))) {
    switch (result.value()) {
    case OptionSet::HELP:
    case OptionSet::HELP2:
      usage(std::cout, argv);
      parser.printOption(stdout);
      return 0;
    case OptionSet::OUTPUT:
      outputFileName = result.arg();
      break;
    case OptionSet::FORMAT:
      factory.setFormatName(result.arg());
      break;
    case OptionSet::STYLE:
      factory.setStyleName(result.arg());
      break;
    }
  }
  if (result.error() != opt::END) {
    std::cerr << result.formatError() << std::endl;
    parser.printOption(std::cerr);
    return 1;
  }
  if (begin == end) {
    usage(std::cerr, argv);
    return 1;
  }

  const char *sourceName = *begin;
  std::ofstream output(outputFileName);
  if (!output) {
    std::cerr << "cannot open file: " << outputFileName << std::endl;
    return 1;
  }

  if (!colorize(factory, sourceName, output)) {
    return 1;
  }
  return 0;
}
