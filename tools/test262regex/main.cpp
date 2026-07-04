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
#include "meta.h"

using namespace arsh;

static void usage(FILE *fp, char **argv) {
  fprintf(fp, "usage: %s [-d] [-n] [test case path]\n", argv[0]);
}

static void invalidOption(char **argv, int opt) {
  fprintf(stderr, "invalid option: -%c\n", opt);
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

static void print(FILE *fp, const re262::TestMetaData &data) {
  fprintf(fp,
          "--- meta-data ---\n"
          "author: %s\ndescription: %s\ninfo: %s\nesid: %s\n"
          "features: %s\nincludes: %s\n",
          data.author.c_str(), data.description.c_str(), data.info.c_str(), data.esid.c_str(),
          toString(data.features).c_str(), toString(data.includes).c_str());
  if (data.negative.has_value()) {
    auto &negative = data.negative.value();
    fprintf(fp, "negative:\n  phase: %s\n  type: %s\n", re262::toString(negative.phase),
            negative.type.c_str());
  }
}

int main(int argc, char **argv) {
  opt::GetOptState optState("hdn");
  auto iter = argv + 1;
  const auto end = argv + argc;
  bool debug = false;
  bool checkMeta = true;
  for (int opt; (opt = optState(iter, end)) != -1;) {
    if (opt == 'd') {
      debug = true;
      continue;
    }
    if (opt == 'n') {
      checkMeta = false;
      continue;
    }
    if (opt == 'h') {
      usage(stdout, argv);
      return 2;
    }
    invalidOption(argv, optState.optOpt);
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

  std::optional<re262::TestMetaData> metaData;
  if (checkMeta) {
    std::string err;
    metaData = re262::TestMetaData::extractFrom(input, &err);
    if (!metaData.has_value()) {
      fprintf(stderr, "[meta-data error] %s\n  at %s\n", err.c_str(), filename);
      return 1;
    }
    if (debug) {
      print(stderr, metaData.value());
    }
  }

  auto env = re262::initJSEnv();
  re262::includeHarness(env);
  std::string syntaxErr;
  auto ret = re262::jsEval(filename, input, env, debug, &syntaxErr);
  auto out = re262::formatEvalResult(env, ret);

  if (!syntaxErr.empty()) {
    fputs(syntaxErr.c_str(), stderr);
  }
  if (metaData.has_value() && metaData.value().negative.has_value()) {
    auto &negative = metaData.value().negative.value();
    const auto actualPhase =
        syntaxErr.empty() ? re262::TestMetaData::Phase::RUNTIME : re262::TestMetaData::Phase::PARSE;
    if (ret) {
      fprintf(stderr, "expected: %s\nactual: %s\n", re262::format(negative).c_str(), out.c_str());
      return 1;
    }
    if (actualPhase != negative.phase) {
      fprintf(stderr, "expected: phase=%s\nactual: phase=%s\n", re262::toString(negative.phase),
              re262::toString(actualPhase));
      return 1;
    }

    auto error = env->findOrUndef(negative.type);
    if (auto v = re262::isInstanceOf(env, 1, ret.asErr().value, error);
        !v || !std::holds_alternative<bool>(v.asOk()) || !std::get<bool>(v.asOk())) {
      fprintf(stderr, "expected: %s\nactual: %s\n", re262::format(negative).c_str(), out.c_str());
      return 1;
    }
  } else if (!ret) {
    fprintf(stderr, "%s\n", out.c_str());
    return 1;
  }
  if (debug) {
    fprintf(stderr, "%s\n", out.c_str());
  }
  return 0;
}