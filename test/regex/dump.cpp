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
#include <fstream>
#include <iostream>

#include <misc/opt.hpp>

#include "../tools/json/serialize.h"
#include "dump.hpp"
#include "regex/dump.h"
#include "regex/emit.h"
#include "regex/match_context.h"
#include "regex/parser.h"

using namespace arsh;

static void usage(FILE *fp, char **argv) {
  fprintf(fp,
          "usage: %s [-d][-t timeout-msec][-m text][-f file][-p on|off|both] pattern [modifiers]\n",
          argv[0]);
}

static void invalidOption(char **argv, int opt) {
  fprintf(stderr, "invalid option: -%c\n", opt);
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

static Optional<std::chrono::milliseconds> parseMsec(const char *str) {
  if (str) {
    auto ret = convertToNum10<int>(str);
    if (!ret) {
      return {};
    }
    if (ret.value > -1) {
      return std::chrono::milliseconds(ret.value);
    }
  }
  return std::chrono::milliseconds::max();
}

static Optional<std::vector<regex::Target>> load(std::istream &stream, FILE *fp) {
  std::vector<regex::Target> targets;
  for (std::string line; std::getline(stream, line);) {
    auto json = json::JSON::fromString(line.c_str());
    json::JSONDeserializer deserializer(std::move(json));
    regex::Target target;
    deserializer(target);
    if (deserializer.hasError()) {
      fprintf(fp, "%s\n", deserializer.getValidationError().formatError().c_str());
      return {};
    }
    target.afterDeserialize();
    targets.push_back(std::move(target));
  }
  return targets;
}

enum class PrefilterOp : unsigned char {
  ON,
  OFF,
  BOTH,
};

static regex::MatchStatus doMatch(const regex::Regex &re, const regex::Target &target,
                                  const ObserverPtr<regex::Timer> timer, const PrefilterOp op,
                                  std::string &out) {
  if (op != PrefilterOp::BOTH) { // try once
    return regex::matchWithPrefilter(re, get<std::string>(target.input), op == PrefilterOp::ON,
                                     timer, out);
  }

  // on
  std::string onMatches;
  const auto onStatus =
      regex::matchWithPrefilter(re, get<std::string>(target.input), true, timer, onMatches);

  // off
  std::string offMatches;
  const auto offStatus =
      regex::matchWithPrefilter(re, get<std::string>(target.input), false, timer, offMatches);

  // compare results
  if (onStatus != offStatus || onMatches != offMatches) {
    auto saved = target;
    saved.beforeSerialize();
    json::JSONSerializer serializer;
    serializer(std::move(saved));
    fprintf(stderr, "prefilter-diff: %s\n", serializer.get().serialize().c_str());
    regex::dumpDiffFailure(stderr, onStatus, onMatches, offStatus, offMatches);
    return regex::MatchStatus::FAIL;
  }
  out = onMatches;
  return onStatus;
}

int main(int argc, char **argv) {
  static StrRefMap<PrefilterOp> prefilterOps = {
      {"on", PrefilterOp::ON},
      {"off", PrefilterOp::OFF},
      {"both", PrefilterOp::BOTH},
  };

  opt::GetOptState optState("hdm:t:f:p:");
  StringRef text;
  bool dumpTree = true;
  bool shouldMatch = false;
  bool dumpRegex = false;
  const char *timeoutMsecStr = nullptr;
  const char *targetFileName = nullptr;
  PrefilterOp op = PrefilterOp::ON;
  auto iter = argv + 1;
  const auto end = argv + argc;
  for (int opt; (opt = optState(iter, end)) != -1;) {
    switch (opt) {
    case 'm':
      shouldMatch = true;
      dumpTree = false;
      text = optState.optArg;
      break;
    case 'd':
      dumpRegex = true;
      dumpTree = false;
      break;
    case 't':
      timeoutMsecStr = optState.optArg.data();
      break;
    case 'f':
      targetFileName = optState.optArg.data();
      break;
    case 'p':
      if (auto i = prefilterOps.find(optState.optArg); i != prefilterOps.end()) {
        op = i->second;
      } else {
        fprintf(stderr, "invalid argument for -p: %s, \n", optState.optArg.toString().c_str());
        usage(stderr, argv);
        return 1;
      }
      break;
    case 'h':
      usage(stdout, argv);
      return 2;
    default:
      invalidOption(argv, opt);
      return 1;
    }
  }
  auto timeout = parseMsec(timeoutMsecStr);
  if (!timeout.hasValue()) {
    fprintf(stderr, "invalid timeout msec: %s\n", timeoutMsecStr);
    usage(stderr, argv);
    return 1;
  }

  std::vector<regex::Target> targets;
  if (targetFileName) {
    std::ifstream stream(targetFileName);
    if (!stream.is_open()) {
      fprintf(stderr, "cannot open file: %s\n", targetFileName);
      return 1;
    }
    if (auto ret = load(stream, stderr); ret.hasValue()) {
      targets = std::move(ret.unwrap());
      if (targets.empty()) {
        fprintf(stderr, "empty json line: %s\n", targetFileName);
        return 1;
      }
    } else {
      return 1;
    }
    dumpTree = dumpRegex;
    shouldMatch = true;
  } else {
    if (iter == end) {
      fputs("need pattern\n", stderr);
      usage(stderr, argv);
      return 1;
    }
    regex::Target target;
    target.pattern = *iter++;
    target.input = text.toString();
    const char *modifiers = nullptr;
    if (iter != end) {
      modifiers = *iter;
    }
    std::string err;
    if (auto flag = regex::Flag::parse(modifiers, regex::Mode::BMP, &err); flag.hasValue()) {
      target.mode = flag.unwrap().mode();
      target.modifiers = flag.unwrap().modifiers();
    } else {
      fprintf(stderr, "[error] %s\n", err.c_str());
      return 1;
    }
    targets.push_back(std::move(target));
  }

  assert(!targets.empty());
  int lastStatus = 0;
  for (auto &target : targets) {
    const StringRef pattern = get<std::string>(target.pattern);
    regex::Parser parser;
    auto tree = parser(pattern, target.flag());
    if (parser.hasError()) {
      auto token = parser.getError()->token;
      auto [line, pos] = formatLoc(pattern, token);
      fprintf(stderr, "%d:%d [error] %s\n at %s\n", line, pos, parser.getError()->message.c_str(),
              token.str().c_str());
      lastStatus = 1;
      continue;
    }

    if (dumpTree) {
      regex::TreeDumper dumper;
      auto buf = dumper(tree);
      fwrite(buf.c_str(), sizeof(char), buf.size(), stdout);
    }

    regex::CodeGen codeGen;
    auto re = codeGen(std::move(tree));
    if (!re.hasValue()) {
      fprintf(stderr, "%s\n", codeGen.getError().c_str());
      lastStatus = 1;
      continue;
    }
    if (dumpRegex) {
      regex::RegexDumper dumper;
      auto buf = dumper(re.unwrap());
      fwrite(buf.c_str(), sizeof(char), buf.size(), stdout);
    }
    if (shouldMatch) {
      std::string str;
      regex::Timer timer(timeout.unwrap());
      auto status = doMatch(re.unwrap(), target, makeObserver(timer), op, str);
      fprintf(stdout, "input: `%s'\n", get<std::string>(target.input).c_str());
      if (status != regex::MatchStatus::OK) {
        fprintf(stdout, "%s\n", toString(status));
        lastStatus = 1;
        continue;
      }
      fwrite(str.c_str(), sizeof(char), str.size(), stdout);
    }
    lastStatus = 0;
  }
  return lastStatus;
}