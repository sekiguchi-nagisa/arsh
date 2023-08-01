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
#include <misc/opt.hpp>

using namespace ydsh;

#define EACH_OPT(OP)                                                                               \
  OP(VAR_NAME, "-v", OptArgOp::HAS_ARG, "specify generated variable name")                         \
  OP(FILE_NAME, "-f", OptArgOp::HAS_ARG, "specify target file name")                               \
  OP(OUTPUT, "-o", OptArgOp::HAS_ARG, "specify output header file name")

enum OptionKind {
#define GEN_ENUM(E, S, F, D) E,
  EACH_OPT(GEN_ENUM)
#undef GEN_ENUM
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
  OptParser<OptionKind> parser = {
#define GEN_OPT(E, S, F, D) {E, S, F, D},
      EACH_OPT(GEN_OPT)
#undef GEN_OPT
  };

  const char *varName = nullptr;
  const char *targetFileName = nullptr;
  const char *outputFileName = nullptr;

  auto begin = argv + 1;
  auto end = argv + argc;
  OptParseResult<OptionKind> result;
  while ((result = parser(begin, end))) {
    switch (result.value()) {
    case VAR_NAME:
      varName = result.arg();
      break;
    case FILE_NAME:
      targetFileName = result.arg();
      break;
    case OUTPUT:
      outputFileName = result.arg();
      break;
    }
  }
  if (result.error() != OptParseError::END) {
    fprintf(stderr, "%s\n", result.formatError().c_str());
    parser.printOption(stderr);
  }
  if (varName == nullptr) {
    parser.printOption(stderr);
    fatal("require -v\n");
  }
  if (targetFileName == nullptr) {
    parser.printOption(stderr);
    fatal("require -f\n");
  }
  if (outputFileName == nullptr) {
    parser.printOption(stderr);
    fatal("require -o\n");
  }

  std::ifstream input(targetFileName);
  if (!input) {
    fatal("cannot open file: \n%s", targetFileName);
  }

  std::string line;

  FILE *fp = fopen(outputFileName, "w");
  if (fp == nullptr) {
    fatal_perror("%s", outputFileName);
  }

  // generate file
  std::string headerSuffix(varName);
  std::transform(headerSuffix.begin(), headerSuffix.end(), headerSuffix.begin(), ::toupper);
  std::string headerName = "SRC_TO_STR__";
  headerName += headerSuffix;
  headerName += "_H";

  fprintf(fp, "// this is an auto-generated file. not change it directly\n");
  fprintf(fp, "#ifndef %s\n", headerName.c_str());
  fprintf(fp, "#define %s\n", headerName.c_str());
  fputs("\n", fp);
  fprintf(fp, "static const char *%s = \"\"\n", varName);
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
