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
  OP(HELP, "-h", OptArgOp::NO_ARG, "show help message")                                            \
  OP(OUTPUT, "-o", OptArgOp::HAS_ARG, "specify output file (default is stdout)")                   \
  OP(FORMAT, "-f", OptArgOp::HAS_ARG, "specify output formatter (default is `ansi' formatter)")    \
  OP(STYLE, "-s", OptArgOp::HAS_ARG,                                                               \
     "specify highlighter color style (default is `darcula' style)")                               \
  OP(LIST, "-l", OptArgOp::NO_ARG, "show supported formatters/styles")                             \
  OP(HTML_FULL, "--html-full", OptArgOp::NO_ARG,                                                   \
     "generate self-contained html (for html formatter)")                                          \
  OP(HTML_LINENO, "--html-lineno", OptArgOp::OPT_ARG,                                              \
     "emit line number starts with ARG (for html formatter)")                                      \
  OP(HTML_LINENO_TABLE, "--html-lineno-table", OptArgOp::NO_ARG,                                   \
     "emit line number as table (for html formatter)")                                             \
  OP(DUMP, "--dump", OptArgOp::NO_ARG, "dump ansi color code of theme")

enum class OptionSet : unsigned int {
#define GEN_ENUM(E, S, F, D) E,
  EACH_OPT(GEN_ENUM)
#undef GEN_ENUM
};

void usage(std::ostream &stream, char **argv) {
  stream << "usage: " << argv[0] << " [option ...] [source file] or " << argv[0]
         << " --dump [option ...]" << '\n'
         << std::flush;
}

Optional<std::string> readAll(const char *sourceName) {
  std::string buf;
  auto file = createFilePtr(fopen, sourceName, "rb");
  if (!file) {
    std::cerr << "cannot open file: " << sourceName << ", by `" << strerror(errno) << "'" << '\n'
              << std::flush;
    return {};
  }

  if (!readAll(file, buf, SYS_LIMIT_INPUT_SIZE)) {
    std::cerr << "cannot read file: " << sourceName << ", by `" << strerror(errno) << "'" << '\n'
              << std::flush;
    return {};
  }

  if (buf.empty() || buf.back() != '\n') {
    buf += '\n';
  }
  return buf;
}

int colorize(FormatterFactory &factory, const char *sourceName, std::ostream &output, bool dump) {
  auto ret = factory.create(output);
  if (!ret) {
    std::cerr << ret.asErr() << '\n';
    return 1;
  }
  auto formatter = std::move(ret).take();
  assert(formatter);

  if (dump) {
    auto value = formatter->dump();
    output << value << '\n';
    return 0;
  }

  auto content = readAll(sourceName);
  if (!content.hasValue()) {
    return 1;
  }

  formatter->initialize(content.unwrap());
  formatter->tokenizeAndEmit();
  formatter->finalize();
  return 0;
}

const char *getFormatterDescription(FormatterType type) {
  switch (type) {
  case FormatterType::NULL_:
    return "output text without any formatting";
  case FormatterType::TERM_TRUECOLOR:
    return "format tokens with ANSI color codes (for true-color terminal)";
  case FormatterType::TERM_256:
    return "format tokens with ANSI color codes (for 256-color terminal)";
  case FormatterType::HTML:
    return "format tokens as HTML codes";
  }
  return ""; // normally unreachable, but suppress gcc warning
}

void showSupported(const FormatterFactory &factory, std::ostream &output) {
  // style
  std::vector<StringRef> names;
  for (auto &e : factory.getStyleMap().getValues()) {
    names.push_back(e.first);
  }
  std::sort(names.begin(), names.end());
  output << "Styles:" << '\n';
  for (auto &e : names) {
    output << "* " << e.toString() << '\n';
  }
  output << '\n';

  // formatter
  std::unordered_map<FormatterType, std::vector<StringRef>> values;
  for (auto &e : factory.getSupportedFormats()) {
    values[e.second].emplace_back(e.first);
  }
  output << "Formatters:" << '\n';
  for (unsigned int i = 0; i < values.size(); i++) {
    auto type = static_cast<FormatterType>(i);
    auto iter = values.find(type);
    assert(iter != values.end());
    auto &nameList = iter->second;
    std::sort(nameList.begin(), nameList.end());
    output << "*";
    for (auto &e : nameList) {
      output << " " << e.toString();
    }
    output << '\n';
    output << "  - " << getFormatterDescription(type) << '\n';
  }
}

} // namespace

int main(int argc, char **argv) {
  OptParser<OptionSet> parser = {
#define GEN_OPT(E, S, F, D) {OptionSet::E, S, (F), D},
      EACH_OPT(GEN_OPT)
#undef GEN_OPT
  };

  auto begin = argv + 1;
  auto end = argv + argc;
  OptParseResult<OptionSet> result;

  const char *outputFileName = "/dev/stdout";
  bool listing = false;
  bool dump = false;
  StyleMap styleMap;
  FormatterFactory factory(styleMap);
  while ((result = parser(begin, end))) {
    switch (result.value()) {
    case OptionSet::HELP:
      usage(std::cout, argv);
      parser.printOption(std::cout);
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
    case OptionSet::LIST:
      listing = true;
      break;
    case OptionSet::HTML_FULL:
      factory.setHTMLFull(true);
      break;
    case OptionSet::HTML_LINENO:
      factory.setLineno(result.arg() != nullptr ? result.arg() : "1");
      break;
    case OptionSet::HTML_LINENO_TABLE:
      factory.setHTMLTable(true);
      break;
    case OptionSet::DUMP:
      dump = true;
      break;
    }
  }
  if (result.error() != OptParseError::END && !dump) {
    std::cerr << result.formatError() << '\n';
    parser.printOption(std::cerr);
    return 1;
  }

  if (listing) {
    showSupported(factory, std::cout);
    return 0;
  }

  const char *sourceName = "/dev/stdin";
  if (begin != end) {
    sourceName = *begin;
  }

  std::ofstream output(outputFileName);
  if (!output) {
    std::cerr << "cannot open file: " << outputFileName << '\n';
    return 1;
  }
  return colorize(factory, sourceName, output, dump);
}
