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

#include <misc/opt_parser.hpp>

#include "factory.h"

namespace {

using namespace arsh;
using namespace arsh::highlighter;

enum class OptionSet : unsigned char {
  HELP,
  OUTPUT,
  FORMAT,
  STYLE,
  LIST,
  HTML_FULL,
  HTML_LINENO,
  HTML_LINENO_TABLE,
  DUMP,
  CUSTOM_STYLE,
};

const OptParser<OptionSet>::Option options[] = {
    {OptionSet::OUTPUT, 'o', "", OptParseOp::HAS_ARG, "file",
     "specify output file (default is stdout)"},
    {OptionSet::FORMAT, 'f', "", OptParseOp::HAS_ARG, "formatter",
     "specify output formatter (default is `ansi' formatter)"},
    {OptionSet::STYLE, 's', "", OptParseOp::HAS_ARG, "style",
     "specify highlighter color style (default is `darcula' style)"},
    {OptionSet::LIST, 'l', "", OptParseOp::NO_ARG, "show supported formatters/styles"},
    {OptionSet::HTML_FULL, 0, "html-full", OptParseOp::NO_ARG,
     "generate self-contained html (for html formatter)"},
    {OptionSet::HTML_LINENO, 0, "html-lineno", OptParseOp::OPT_ARG, "num",
     "emit line number starts with NUM (for html formatter)"},
    {OptionSet::HTML_LINENO_TABLE, 0, "html-lineno-table", OptParseOp::NO_ARG,
     "emit line number as table (for html formatter)"},
    {OptionSet::DUMP, 0, "dump", OptParseOp::NO_ARG, "dump ansi color code of theme"},
    {OptionSet::CUSTOM_STYLE, 0, "custom-style", OptParseOp::HAS_ARG,
     "set custom color style (name=color ...)"},
    {OptionSet::HELP, 'h', "help", OptParseOp::NO_ARG, "show help message"},
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

int colorize(const FormatterFactory &factory, const char *sourceName, std::ostream &output,
             bool dump) {
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
    names.emplace_back(e.first);
  }
  std::sort(names.begin(), names.end());
  output << "Styles:" << '\n';
  for (auto &e : names) {
    output << "* " << e.toString() << '\n';
  }
  output << '\n';

  // formatter
  std::unordered_map<FormatterType, std::vector<StringRef>> values;
  for (auto &[name, type] : factory.getSupportedFormats()) {
    values[type].emplace_back(name);
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
  auto parser = createOptParser(options);

  auto begin = argv + 1;
  auto end = argv + argc;
  OptParseResult<OptionSet> result;

  StringRef outputFileName = "/dev/stdout";
  bool listing = false;
  bool dump = false;
  StyleMap styleMap;
  FormatterFactory factory(styleMap);
  std::vector<StringRef> customStyles;
  while ((result = parser(begin, end))) {
    switch (result.getOpt()) {
    case OptionSet::HELP:
      usage(std::cout, argv);
      std::cout << parser.formatOptions() << std::endl; // NOLINT
      return 0;
    case OptionSet::OUTPUT:
      outputFileName = result.getValue();
      break;
    case OptionSet::FORMAT:
      factory.setFormatName(result.getValue());
      break;
    case OptionSet::STYLE:
      factory.setStyleName(result.getValue());
      break;
    case OptionSet::LIST:
      listing = true;
      break;
    case OptionSet::HTML_FULL:
      factory.setHTMLFull(true);
      break;
    case OptionSet::HTML_LINENO:
      factory.setLineno(result.hasArg() ? result.getValue() : "1");
      break;
    case OptionSet::HTML_LINENO_TABLE:
      factory.setHTMLTable(true);
      break;
    case OptionSet::DUMP:
      dump = true;
      break;
    case OptionSet::CUSTOM_STYLE:
      customStyles.push_back(result.getValue());
      for (; begin != end && **begin != '-'; ++begin) {
        customStyles.emplace_back(*begin);
      }
      break;
    }
  }
  if (result.isError() && !dump) {
    std::cerr << result.formatError() << '\n' << parser.formatOptions() << std::endl; // NOLINT
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
  factory.setCustomStyles(std::move(customStyles));

  std::ofstream output(outputFileName.toString());
  if (!output) {
    std::cerr << "cannot open file: " << outputFileName.toString() << '\n';
    return 1;
  }
  return colorize(factory, sourceName, output, dump);
}
