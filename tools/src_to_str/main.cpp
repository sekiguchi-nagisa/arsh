/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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

#include <algorithm>
#include <fstream>
#include <iostream>

#include <misc/fatal.h>
#include <misc/opt_parser.hpp>

using namespace arsh;

enum class OptionKind : unsigned char {
  VAR_NAME,
  FILE_NAME,
  OUTPUT,
};

static const OptParser<OptionKind>::Option options[] = {
    {OptionKind::VAR_NAME, 'v', "", OptParseOp::HAS_ARG, "specify generated variable name"},
    {OptionKind::FILE_NAME, 'f', "", OptParseOp::HAS_ARG, "specify target file name"},
    {OptionKind::OUTPUT, 'o', "output", OptParseOp::HAS_ARG, "specify output header file name"},
};

static std::string escape(const std::string &line) {
  std::string out;
  for (const auto &ch : line) {
    switch (ch) {
    case '\n':
      out += '\\';
      out += 'n';
      break;
    case '\r':
      out += '\\';
      out += 'r';
      break;
    case '\t':
      out += '\\';
      out += 't';
      break;
    case '"':
      out += '\\';
      out += '"';
      break;
    case '\\':
      out += '\\';
      out += '\\';
      break;
    default:
      out += ch;
      break;
    }
  }
  return out;
}

int main(int argc, char **argv) {
  auto parser = createOptParser(options);

  StringRef varName;
  StringRef targetFileName;
  StringRef outputFileName;

  auto begin = argv + 1;
  auto end = argv + argc;
  OptParseResult<OptionKind> result;
  while ((result = parser(begin, end))) {
    switch (result.getOpt()) {
    case OptionKind::VAR_NAME:
      varName = result.getValue();
      break;
    case OptionKind::FILE_NAME:
      targetFileName = result.getValue();
      break;
    case OptionKind::OUTPUT:
      outputFileName = result.getValue();
      break;
    }
  }
  if (result.isError()) {
    fprintf(stderr, "%s\n%s\n", result.formatError().c_str(), parser.formatOptions().c_str());
    return 1;
  }
  if (!varName.data()) {
    fprintf(stderr, "%s\n", parser.formatOptions().c_str());
    fatal("require -v\n");
  }
  if (!targetFileName.data()) {
    fprintf(stderr, "%s\n", parser.formatOptions().c_str());
    fatal("require -f\n");
  }
  if (!outputFileName.data()) {
    fprintf(stderr, "%s\n", parser.formatOptions().c_str());
    fatal("require -o\n");
  }

  std::ifstream input(targetFileName.toString());
  if (!input) {
    fatal("cannot open file: \n%s", targetFileName.toString().c_str());
  }

  std::string line;

  FILE *fp = fopen(outputFileName.toString().c_str(), "w");
  if (fp == nullptr) {
    fatal_perror("%s", outputFileName.toString().c_str());
  }

  // generate file
  std::string headerSuffix(varName.toString());
  std::transform(headerSuffix.begin(), headerSuffix.end(), headerSuffix.begin(), ::toupper);
  std::string headerName = "SRC_TO_STR__";
  headerName += headerSuffix;
  headerName += "_H";

  fprintf(fp, "// this is an auto-generated file. not change it directly\n");
  fprintf(fp, "#ifndef %s\n", headerName.c_str());
  fprintf(fp, "#define %s\n", headerName.c_str());
  fputs("\n", fp);
  fprintf(fp, "static const char *%s = \"\"\n", varName.toString().c_str());
  while (std::getline(input, line)) {
    // skip empty line and comment
    if (line.empty() || line[0] == '#') {
      continue;
    }

    fprintf(fp, "    \"%s\\n\"\n", escape(line).c_str());
  }
  fprintf(fp, ";\n");
  fputs("\n", fp);
  fprintf(fp, "#endif //%s\n", headerName.c_str());

  fclose(fp);
  return 0;
}
