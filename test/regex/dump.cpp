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

#include <cstdio>
#include <utility>

#include "regex/dump.h"
#include "regex/parser.h"

using namespace arsh;

static void usage(FILE *fp, char **argv) {
  fprintf(fp, "usage: %s pattern [modifiers]\n", argv[0]);
}

static std::pair<unsigned int, unsigned int> formatLoc(StringRef src, Token token) {
  unsigned int line = 1;
  unsigned int lastLineOffset = 0;
  for (unsigned int i = 0; i <= token.pos; i++) {
    if (src[i] == '\n') {
      lastLineOffset = i;
      line++;
    }
  }
  unsigned int pos = token.pos + 1 - lastLineOffset;
  return {line, pos};
}

int main(int argc, char **argv) {
  auto iter = argv + 1;
  const auto end = argv + argc;
  if (iter == end) {
    fputs("need pattern\n", stderr);
    usage(stderr, argv);
    return 1;
  }
  const char *pattern = *iter++;
  const char *modifiers = nullptr;
  if (iter != end) {
    modifiers = *iter;
  }

  std::string err;
  auto flag = regex::Flag::parse(modifiers, regex::Mode::BMP, &err);
  if (!flag.hasValue()) {
    fprintf(stderr, "[error] %s\n", err.c_str());
    return 1;
  }

  regex::Parser parser;
  auto tree = parser(pattern, flag.unwrap());
  if (parser.hasError()) {
    auto [line, pos] = formatLoc(pattern, parser.getError()->token);
    fprintf(stderr, "%d:%d [error] %s\n", line, pos, parser.getError()->message.c_str());
    return 1;
  }

  regex::TreeDumper dumper;
  auto buf = dumper(tree);
  fwrite(buf.c_str(), sizeof(char), buf.size(), stdout);
  return 0;
}