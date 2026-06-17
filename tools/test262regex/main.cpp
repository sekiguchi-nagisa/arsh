/*
 * Copyright (C) 2026 Nagisa Sekiguchi
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

#include <misc/opt.hpp>
#include <misc/resource.hpp>

#include "js.h"
#include "js_lexer.h"
#include "meta.h"

using namespace arsh;

static void usage(FILE *fp, char **argv) {
  fprintf(fp, "usage: %s [-d] [test case path]\n", argv[0]);
}

static void invalidOption(char **argv, int opt) {
  fprintf(stderr, "invalid option: -%c", opt);
  usage(stderr, argv);
}

static std::string toString(const std::vector<std::string> &values) {
  std::string ret = "[";
  for (unsigned int i = 0; i < values.size(); i++) {
    if (i > 0) {
      ret += ", ";
    }
    ret += values[i];
  }
  ret += "]";
  return ret;
}

static const char *toString(re262::TestMetaData::Phase phase) {
  switch (phase) {
  case re262::TestMetaData::Phase::PARSE:
    return "parse";
  case re262::TestMetaData::Phase::RUNTIME:
    return "runtime";
  }
  return "";
}

static void print(FILE *fp, const re262::TestMetaData &data) {
  fprintf(fp,
          "--- meta-data ---\n"
          "author: %s\ndescription: %s\ninfo: %s\nesid: %s\n"
          "features: %s\nincludes: %s\n",
          data.author.c_str(), data.description.c_str(), data.info.c_str(), data.esid.c_str(),
          toString(data.features).c_str(), toString(data.includes).c_str());
  if (data.negative.has_value()) {
    auto &negative = data.negative.value();
    fprintf(fp, "negative:\n  phase: %s\n  type: %s\n", toString(negative.phase),
            negative.type.c_str());
  }
}

int main(int argc, char **argv) {
  opt::GetOptState optState("hd");
  auto iter = argv + 1;
  const auto end = argv + argc;
  bool debug = false;
  for (int opt; (opt = optState(iter, end)) != -1;) {
    if (opt == 'd') {
      debug = true;
      continue;
    }
    if (opt == 'h') {
      usage(stdout, argv);
      return 2;
    }
    invalidOption(argv, opt);
    return 1;
  }
  if (iter == end) {
    fprintf(stderr, "need test case path\n");
    usage(stderr, argv);
    return 1;
  }
  const char *filename = *iter;
  std::string input;
  if (FILE *fp = fopen(filename, "r"); fp && readAll(fp, input, UINT32_MAX)) {
    fclose(fp);
  } else {
    perror(filename);
    return 1;
  }

  std::string err;
  auto metaData = re262::TestMetaData::extractFrom(input, &err);
  if (!metaData.has_value()) {
    fprintf(stderr, "[meta-data error] %s\n  at %s\n", err.c_str(), filename);
    return 1;
  }
  if (debug) {
    print(stderr, metaData.value());
  }

  re262::JSLexer lexer(filename, input);
  re262::JSTokenKind kind = re262::JSTokenKind::INVALID;
  do {
    Token token;
    kind = lexer.nextToken(token);
    if (debug) {
      fprintf(stderr, "(%s, %s)\n", re262::toString(kind), lexer.toTokenText(token).c_str());
    }
    if (re262::isInvalidToken(kind)) {
      fprintf(stderr, "[syntax error] invalid token: %s\n  at %s\n",
              lexer.toTokenText(token).c_str(), filename);
      return 1;
    }
  } while (kind != re262::JSTokenKind::EOS);
  return 0;
}