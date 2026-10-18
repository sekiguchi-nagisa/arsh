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

/**
 * Standalone regex match program embedding the JavaScript-compatible engines used by
 * tools/regex-bench.
 *
 * It is the `-m` mode of the arsh `redump` dump tool (test/regex/dump.cpp) with the engine
 * selectable via `-e`: it compiles `pattern` (with the optional `modifiers`, the same
 * `u`/`v`/`i`/`m`/`s` flag string redump accepts) and reports the first match against `-m`
 * input, printing the same `input: ...` / `(offset=, size=)` / `failed` output but with UTF-8
 * byte offsets.
 *
 *   rematch -e <hermes|quickjs|srell> -m <input> <pattern> [modifiers]
 *
 * The engine adapters live in rematch_hermes.cpp / rematch_quickjs.c / rematch_srell.cpp and
 * implement the small C interface in rematch_abi.h.
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <misc/opt.hpp>

#include "regex/flag.h"
#include "rematch_abi.h"

using namespace arsh;

#define MAX_CAPTURES 256

namespace {

using MatchFn = int (*)(const char *, int, const char *, int, int, int, int, int,
                        rematch_capture *, int, char *, int);

struct Engine {
  const char *name;
  MatchFn match;
};

const Engine ENGINES[] = {
#ifdef REMATCH_ENABLE_HERMES
        {"hermes", rematch_hermes_match},
#endif
#ifdef REMATCH_ENABLE_QUICKJS
        {"quickjs", rematch_quickjs_match},
#endif
#ifdef REMATCH_ENABLE_SRELL
        {"srell", rematch_srell_match},
#endif
};

const Engine *findEngine(const char *name) {
  for (const auto &engine : ENGINES) {
    if (std::strcmp(engine.name, name) == 0) {
      return &engine;
    }
  }
  return nullptr;
}

void usage(FILE *fp, char **argv) {
  fprintf(fp, "usage: %s -e <engine> [-m input] pattern [modifiers]\n", argv[0]);
  fprintf(fp, "engines:");
  for (const auto &engine : ENGINES) {
    fprintf(fp, " %s", engine.name);
  }
  fputc('\n', fp);
}

void invalidOption(char **argv, int opt) {
  fprintf(stderr, "invalid option: -%c\n", opt);
  usage(stderr, argv);
}

void printCaptures(const rematch_capture *captures, int count) {
  for (int i = 0; i < count; i++) {
    if (captures[i].offset == REMATCH_CAPTURE_UNSET) {
      fputs("(unset)\n", stdout);
    } else {
      printf("(offset=%u, size=%u)\n", captures[i].offset, captures[i].size);
    }
  }
}

} // namespace

int main(int argc, char **argv) {
  const char *engineName = nullptr;
  StringRef text;
  auto iter = argv + 1;
  const auto end = argv + argc;
  opt::GetOptState optState("he:m:");
  for (int opt; (opt = optState(iter, end)) != -1;) {
    switch (opt) {
    case 'e':
      engineName = optState.optArg.data();
      break;
    case 'm':
      text = optState.optArg;
      break;
    case 'h':
      usage(stdout, argv);
      return 2;
    default:
      invalidOption(argv, opt);
      return 1;
    }
  }

  if (engineName == nullptr) {
    fputs("need engine (-e)\n", stderr);
    usage(stderr, argv);
    return 1;
  }
  const Engine *engine = findEngine(engineName);
  if (engine == nullptr) {
    fprintf(stderr, "unknown engine: %s\n", engineName);
    usage(stderr, argv);
    return 1;
  }

  if (iter == end) {
    fputs("need pattern\n", stderr);
    usage(stderr, argv);
    return 1;
  }
  const char *pattern = *iter++;
  const char *modifiers = iter != end ? *iter : nullptr;

  std::string err;
  auto flag = regex::Flag::parse(modifiers, regex::Mode::BMP, &err);
  if (!flag.hasValue()) {
    fprintf(stderr, "[error] %s\n", err.c_str());
    return 1;
  }

  const int mode = static_cast<int>(flag.unwrap().mode());
  const int icase = flag.unwrap().has(regex::Modifier::IGNORE_CASE) ? 1 : 0;
  const int multiline = flag.unwrap().has(regex::Modifier::MULTILINE) ? 1 : 0;
  const int dotall = flag.unwrap().has(regex::Modifier::DOT_ALL) ? 1 : 0;

  const std::string input = text.toString();
  std::vector<rematch_capture> captures(MAX_CAPTURES);
  char errMsg[512];
  errMsg[0] = '\0';
  const int ret = engine->match(pattern, static_cast<int>(std::strlen(pattern)), input.c_str(),
                                static_cast<int>(input.size()), mode, icase, multiline, dotall,
                                captures.data(), MAX_CAPTURES, errMsg, sizeof(errMsg));
  if (ret < 0) {
    fprintf(stderr, "[error] %s\n", errMsg);
    return 1;
  }

  printf("input: `%s'\n", input.c_str());
  if (ret == 0) {
    fputs("failed\n", stdout);
    return 1;
  }
  printCaptures(captures.data(), ret);
  return 0;
}
