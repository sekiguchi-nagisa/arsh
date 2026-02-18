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

#include <misc/opt.hpp>

#include "regex/dump.h"
#include "regex/emit.h"
#include "regex/parser.h"

using namespace arsh;

static void usage(FILE *fp, char **argv) {
  fprintf(fp, "usage: %s [-m text] pattern [modifiers]\n", argv[0]);
}

static void invalidOption(char **argv, int opt) {
  fprintf(stderr, "invalid option: -%c", opt);
  usage(stderr, argv);
}

static std::pair<unsigned int, unsigned int> formatLoc(StringRef src, Token token) {
  unsigned int line = 1;
  unsigned int lastLineOffset = 0;
  for (unsigned int i = 0; i <= token.pos && i < src.size(); i++) {
    if (src[i] == '\n') {
      lastLineOffset = i;
      line++;
    }
  }
  unsigned int pos = token.pos + 1 - lastLineOffset;
  return {line, pos};
}

static const char *toString(const regex::MatchStatus s) {
  switch (s) {
  case regex::MatchStatus::OK:
    break;
  case regex::MatchStatus::FAIL:
    return "failed";
  case regex::MatchStatus::INVALID_UTF8:
    return "input string is invalid UTF-8";
  case regex::MatchStatus::INPUT_LIMIT:
    return "size of input string is too large";
  case regex::MatchStatus::CANCEL:
    return "canceled";
  case regex::MatchStatus::TIMEOUT:
    return "timeout";
  case regex::MatchStatus::STACK_LIMIT:
    return "stack depth reaches limit";
  }
  return "";
}

static std::string formatCaptures(const FlexBuffer<regex::Capture> &captures) {
  std::string ret;
  for (auto &c : captures) {
    if (!c) {
      ret += "(unset)\n";
      continue;
    }
    ret += "(offset=";
    ret += std::to_string(c.offset);
    ret += ", size=";
    ret += std::to_string(c.size);
    ret += ")\n";
  }
  return ret;
}

int main(int argc, char **argv) {
  opt::GetOptState optState("hdm:");
  StringRef text;
  bool shouldMatch = false;
  bool dumpRegex = false;
  auto iter = argv + 1;
  const auto end = argv + argc;
  for (int opt; (opt = optState(iter, end)) != -1;) {
    switch (opt) {
    case 'm':
      shouldMatch = true;
      text = optState.optArg.data();
      break;
    case 'd':
      dumpRegex = true;
      break;
    case 'h':
      usage(stdout, argv);
      return 2;
    default:
      invalidOption(argv, opt);
      return 1;
    }
  }
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
    auto token = parser.getError()->token;
    auto [line, pos] = formatLoc(pattern, token);
    fprintf(stderr, "%d:%d [error] %s\n at %s\n", line, pos, parser.getError()->message.c_str(),
            token.str().c_str());
    return 1;
  }

  if (dumpRegex || shouldMatch) {
    regex::CodeGen codeGen;
    auto re = codeGen(std::move(tree));
    if (!re.hasValue()) {
      fprintf(stderr, "%s\n", codeGen.getError().c_str());
      return 1;
    }
    if (dumpRegex) {
      regex::RegexDumper dumper;
      auto buf = dumper(re.unwrap());
      fwrite(buf.c_str(), sizeof(char), buf.size(), stdout);
      return 0;
    }
    FlexBuffer<regex::Capture> captures;
    auto status = regex::match(re.unwrap(), text, captures);
    fprintf(stdout, "input: `%s'\n", text.toString().c_str());
    if (status == regex::MatchStatus::OK) {
      auto str = formatCaptures(captures);
      fwrite(str.c_str(), sizeof(char), str.size(), stdout);
      return 0;
    }
    fprintf(stdout, "%s\n", toString(status));
    return 1;
  }
  regex::TreeDumper dumper;
  auto buf = dumper(tree);
  fwrite(buf.c_str(), sizeof(char), buf.size(), stdout);
  return 0;
}