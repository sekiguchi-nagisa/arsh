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
 * Differential testing of the regex search prefilter.
 *
 * `MatchContext::prefilter` only chooses which start offset is tried next (`searchPrefix` in
 * src/regex/vm.cpp), so turning it off must not change anything the caller can observe. This
 * suite pins that invariant down by scanning the same compiled regex twice and comparing the
 * resulting match sequence:
 *
 *   * `on`  - the production path: `regex::matchAll`, which runs with the prefilter enabled
 *   * `off` - the same scan with `MatchContext::setPrefilter(false)`, so the VM is retried at
 *             every code point instead of jumping to the next candidate
 *
 * The comparison covers the match position, the size and the unset state of every capture group,
 * and the number of matches, which together also catch a capture leaking from one start offset
 * into the next.
 *
 * Cases are generated from a fixed seed, so a failure is reproducible. Raise the corpus with:
 *
 *   ARSH_RE_PREFILTER_SEED=42 ARSH_RE_PREFILTER_ITER=100000 ./test/regex/prefilter_test
 *
 * A failure is printed as one JSON line to stderr. `redump -f` accepts exactly that format (see
 * test/regex/dump.cpp), so the case can be replayed by hand.
 */

#include "../test_common.h"
#include "dump.hpp"

#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include <misc/num_util.hpp>
#include <regex/emit.h>
#include <regex/flag.h>
#include <regex/match_context.h>
#include <regex/parser.h>

#include "../tools/json/serialize.h"

using namespace arsh;

namespace {

constexpr unsigned int DEFAULT_SEED = 20261105;
constexpr unsigned int DEFAULT_ITER = 10000;

/**
 * @return
 * true if the status depends on how many start offsets are attempted, so the prefilter is
 * allowed to change whether it is reached. Such a case carries no information about correctness.
 */
bool isEnvironmentDependent(const regex::MatchStatus status) {
  switch (status) {
  case regex::MatchStatus::OK:
  case regex::MatchStatus::FAIL:
    return false;
  default:
    // TIMEOUT / STACK_LIMIT / CANCEL
    return true;
  }
}

/**
 * Repeat `match()` until it fails, like `regex::matchAll` does, but with an explicit prefilter
 * state. `regex::matchAll` cannot be used for the `off` run because it creates its own context
 * with the default state.
 *
 * @param out
 * receives the capture groups of every match.
 * @return
 * the status of the scan. `OK` also covers "no more match".
 */
regex::MatchStatus scanAll(const regex::Regex &re, const StringRef text, const bool prefilter,
                           std::string &out) {
  std::vector<regex::Capture> captures;
  auto ctx = regex::tryToCreateMatchContext(re, text, 0, captures);
  if (!ctx) {
    return ctx.asErr();
  }
  ctx.asOk().setPrefilter(prefilter);
  for (;;) {
    const unsigned int startOffset = ctx.asOk().getInput().getOffset();
    const auto status = regex::match(re, ctx.asOk(), nullptr);
    if (status == regex::MatchStatus::FAIL) {
      return regex::MatchStatus::OK; // no more matches, like `regex::matchAll`
    }
    if (status != regex::MatchStatus::OK) {
      return status;
    }
    out += regex::formatCaptures(captures);
    if (captures[0].endOffset() == startOffset) { // empty match, avoid an infinite loop
      if (!ctx.asOk().refInput().available()) {
        return regex::MatchStatus::OK;
      }
      ctx.asOk().refInput().consumeForward();
    }
  }
}

/**
 * The production scan: `regex::matchAll` with its default context, i.e. prefilter enabled.
 *
 * @param out
 * receives the capture groups of every match.
 * @return
 * the status of the scan. `OK` also covers "no more match".
 */
regex::MatchStatus scanAllProduction(const regex::Regex &re, const StringRef text,
                                     std::string &out) {
  auto consumer = [&out](const ArrayRef<regex::Capture> captures) {
    out += regex::formatCaptures(captures);
    return out.size() < (1u << 20u);
  };
  const auto status = regex::matchAll(re, text, 0, consumer, nullptr);
  // the consumer stops the scan once the output is large enough
  return status == regex::MatchStatus::CONSUMED_LIMIT ? regex::MatchStatus::OK : status;
}

void dumpFailure(const regex::Target &target, const regex::MatchStatus onStatus,
                 const std::string &onMatches, const regex::MatchStatus offStatus,
                 const std::string &offMatches) {
  auto saved = target;
  saved.beforeSerialize();
  json::JSONSerializer serializer;
  serializer(std::move(saved));
  fprintf(stderr, "prefilter-diff: %s\n", serializer.get().serialize().c_str());

  size_t pos = 0;
  const size_t size = std::min(onMatches.size(), offMatches.size());
  while (pos < size && onMatches[pos] == offMatches[pos]) {
    pos++;
  }
  fprintf(stderr, "  on : %s\n  off: %s\n", toString(onStatus), toString(offStatus));
  fprintf(stderr, "  first difference at byte %zu\n", pos);
  fprintf(stderr, "  on : %s\n", onMatches.substr(pos, 160).c_str());
  fprintf(stderr, "  off: %s\n", offMatches.substr(pos, 160).c_str());
}

// #########################
// ##     case generator  ##
// #########################

/**
 * Generates patterns and inputs which exercise the search layer: leading literals and character
 * classes (the cases the prefilter optimizes), empty matches, look-around, and nested quantifiers.
 * Patterns rejected by the parser in the drawn mode are skipped by the caller.
 */
class CaseGenerator {
private:
  std::mt19937 rng;

  int pick(const int n) { return static_cast<int>(this->rng() % static_cast<unsigned>(n)); }

  static constexpr const char *ATOMS[] = {
      "a",   "b",   "T", "o", "m", "0", "9", " ",     "\\n",    "\\t",   ".",       "\\d",
      "\\w", "\\s", "x", "q", "z", "A", "-", "[a-c]", "[^a-c]", "[abc]", "\\p{Sm}", "\\u{1F600}",
  };

  const char *atom() { return ATOMS[this->pick(std::size(ATOMS))]; }

  std::string quantified(const int depth) {
    std::string inner = this->expression(depth + 1);
    switch (this->pick(9)) {
    case 0:
      return "(" + inner + ")*";
    case 1:
      return "(" + inner + ")+";
    case 2:
      return "(" + inner + ")?";
    case 3:
      return "(" + inner + ")*?";
    case 4:
      return "(" + inner + ")+?";
    case 5:
      return "(" + inner + "){0,3}";
    case 6:
      return "(" + inner + "){1,2}?";
    case 7:
      return "(" + inner + "){2,4}";
    default:
      return inner;
    }
  }

  std::string expression(const int depth) {
    if (depth > 2) {
      return this->atom();
    }
    switch (this->pick(12)) {
    case 0:
    case 1:
    case 2:
      return this->atom();
    case 3:
      return "(" + this->expression(depth + 1) + this->expression(depth + 1) + ")";
    case 4:
      return "(" + this->expression(depth + 1) + "|" + this->expression(depth + 1) + ")";
    case 5:
      return this->quantified(depth);
    case 6:
      return "(?:" + this->expression(depth + 1) + ")";
    case 7:
      return "(?=" + this->expression(depth + 1) + ")" + this->expression(depth + 1);
    case 8:
      return "(?!" + std::string(this->atom()) + ")" + this->expression(depth + 1);
    case 9:
      return "(?<=" + std::string(this->atom()) + ")" + this->expression(depth + 1);
    case 10:
      return "^" + this->expression(depth + 1);
    default:
      return this->expression(depth + 1) + "$";
    }
  }

  std::string input() {
    static constexpr const char *PIECES[] = {"a", "b", "c", "T",  "o",       "m", " ",
                                             "0", "1", "9", "\n", "t",       "x", "q",
                                             "z", "A", "B", "-",  "\xC3\xA9"};
    std::string text;
    const int size = this->pick(13);
    for (int i = 0; i < size; i++) {
      text += PIECES[this->pick(std::size(PIECES))];
    }
    return text;
  }

public:
  explicit CaseGenerator(const unsigned int seed) : rng(seed) {}

  regex::Target next() {
    regex::Mode mode = regex::Mode::UNICODE;
    switch (this->pick(3)) {
    case 0:
      mode = regex::Mode::BMP;
      break;
    case 1:
      mode = regex::Mode::UNICODE;
      break;
    default:
      mode = regex::Mode::UNICODE_SET;
      break;
    }
    regex::Modifier modifiers{};
    if (this->pick(2)) {
      setFlag(modifiers, regex::Modifier::MULTILINE);
    }
    if (this->pick(4) == 0) {
      setFlag(modifiers, regex::Modifier::DOT_ALL);
    }
    if (this->pick(4) == 0) {
      setFlag(modifiers, regex::Modifier::IGNORE_CASE);
    }

    regex::Target target;
    target.pattern = this->expression(0);
    target.input = this->input();
    target.mode = mode;
    target.modifiers = modifiers;
    return target;
  }
};

// #####################
// ##     the suite   ##
// #####################

struct PrefilterTest : public ::testing::Test {
  static void compare(const regex::Target &target) {
    const StringRef pattern = get<std::string>(target.pattern);
    regex::Parser parser;
    auto tree = parser(pattern, target.flag());
    if (parser.hasError()) {
      return; // not a valid pattern in this mode; the prefilter cannot affect it
    }
    regex::CodeGen codeGen;
    auto re = codeGen(std::move(tree));
    if (!re.hasValue()) {
      return; // e.g. too many matchers; also independent of the prefilter
    }
    const auto &regex = re.unwrap();
    const StringRef text = get<std::string>(target.input);

    // The invariant: the same driver, only the prefilter flag differs.
    std::string onMatches;
    const auto onStatus = scanAll(regex, text, true, onMatches);
    if (isEnvironmentDependent(onStatus)) {
      return; // nothing comparable
    }
    std::string offMatches;
    const auto offStatus = scanAll(regex, text, false, offMatches);
    if (isEnvironmentDependent(offStatus)) {
      return;
    }

    if (onStatus != offStatus || onMatches != offMatches) {
      dumpFailure(target, onStatus, onMatches, offStatus, offMatches);
    }
    ASSERT_EQ(onStatus, offStatus) << "pattern: `" << pattern.toString() << "'";
    ASSERT_EQ(onMatches, offMatches) << "pattern: `" << pattern.toString() << "'";

    // And the prefilter-on run must agree with the production entry point, so the assertion
    // above also covers the real `regex::matchAll` and not just the test driver.
    std::string prodMatches;
    const auto prodStatus = scanAllProduction(regex, text, prodMatches);
    if (!isEnvironmentDependent(prodStatus)) {
      ASSERT_EQ(onStatus, prodStatus) << "pattern: `" << pattern.toString() << "'";
      ASSERT_EQ(onMatches, prodMatches) << "pattern: `" << pattern.toString() << "'";
    }
  }

  static unsigned int envOr(const char *name, const unsigned int fallback) {
    if (const char *str = getenv(name)) {
      if (auto ret = convertToNum10<unsigned int>(str); ret) {
        return ret.value;
      }
    }
    return fallback;
  }
};

TEST_F(PrefilterTest, generated) {
  const unsigned int seed = envOr("ARSH_RE_PREFILTER_SEED", DEFAULT_SEED);
  unsigned int iter = envOr("ARSH_RE_PREFILTER_ITER", DEFAULT_ITER);
  if (iter == 0) {
    iter = DEFAULT_ITER;
  }
  printf(" seed: %u, iter: %u\n", seed, iter);

  CaseGenerator generator(seed);
  for (unsigned int i = 0; i < iter; i++) {
    if (this->HasFatalFailure()) {
      break; // keep the first failing case as the reported one
    }
    ASSERT_NO_FATAL_FAILURE(compare(generator.next())) << "iteration " << i;
  }
}

/**
 * Regression cases: the smallest inputs that used to make the two runs differ, before the retry
 * loop was changed to reset the capture and loop state for every start offset
 * (see `ctx.resetState()` in src/regex/vm.cpp).
 */
struct PrefilterRegressionEntry {
  const char *pattern;
  const char *text;
  regex::Modifier modifiers;
  const char *expected;
};

std::ostream &operator<<(std::ostream &stream, const PrefilterRegressionEntry &entry) {
  return stream << "/" << entry.pattern << "/ on `" << entry.text << "'";
}

struct PrefilterRegressionTest : public ::testing::TestWithParam<PrefilterRegressionEntry> {
  static void doTest() {
    auto &param = GetParam();
    regex::Target target;
    target.pattern = param.pattern;
    target.input = param.text;
    target.mode = regex::Mode::UNICODE;
    target.modifiers = param.modifiers;

    const StringRef pattern = get<std::string>(target.pattern);
    regex::Parser parser;
    auto tree = parser(pattern, target.flag());
    ASSERT_FALSE(parser.hasError());
    regex::CodeGen codeGen;
    auto re = codeGen(std::move(tree));
    ASSERT_TRUE(re.hasValue());

    const StringRef text = get<std::string>(target.input);
    std::string offMatches;
    const auto offStatus = scanAll(re.unwrap(), text, false, offMatches);
    ASSERT_EQ(regex::MatchStatus::OK, offStatus);
    ASSERT_EQ(param.expected, offMatches);

    // and the prefilter must not change it
    std::string onMatches;
    const auto onStatus = scanAll(re.unwrap(), text, true, onMatches);
    ASSERT_EQ(regex::MatchStatus::OK, onStatus);
    ASSERT_EQ(offMatches, onMatches);
  }
};

TEST_P(PrefilterRegressionTest, base) { ASSERT_NO_FATAL_FAILURE(doTest()); }

static constexpr PrefilterRegressionEntry prefilterRegressionCases[] = {
    // the look-ahead of the attempt at offset 1 sets group 1, then backtracks
    {"(?=(.)*)\\s", "ab\n", regex::Modifier::MULTILINE, "(offset=2, size=1)\n(unset)\n"},
    // the attempt at offset 1 leaves group 1 behind; the match happens at offset 2
    {"(?:q|(?=(T))o)", "amq", regex::Modifier::NONE, "(offset=2, size=1)\n(unset)\n"},
    {"(?:(?=(x)?)\\d)", "xm\no1Axz9xx", regex::Modifier::MULTILINE,
     "(offset=4, size=1)\n(unset)\n(offset=8, size=1)\n(unset)\n"},
};

INSTANTIATE_TEST_SUITE_P(PrefilterRegressionTest, PrefilterRegressionTest,
                         ::testing::ValuesIn(prefilterRegressionCases));

} // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
