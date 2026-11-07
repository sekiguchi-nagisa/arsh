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
 * The generator draws patterns and inputs that mix ASCII with 2, 3 and 4 byte UTF-8 code points.
 * That matters for the prefilter, which indexes its first byte set by raw UTF-8 lead byte, and
 * for the scan layer, which must never start a retry inside a multi-byte character. It also
 * draws constructs whose first byte set is empty or hard to derive: the zero width `\b` and `\B`,
 * back references (including one inside a look-behind), and, in `v` mode, the string valued
 * character class elements `\q{...}`, set subtraction/intersection and the emoji sequences.
 *
 * Cases are generated from a fixed seed with `L64X128MixRNG` (see src/misc/split_random.hpp), so
 * a failure is reproducible. Raise the corpus with:
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
#include <string>
#include <vector>

#include <misc/num_util.hpp>
#include <misc/split_random.hpp>
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
  regex::dumpDiffFailure(stderr, onStatus, onMatches, offStatus, offMatches);
}

// #########################
// ##     case generator  ##
// #########################

/**
 * Generates patterns and inputs which exercise the search layer: leading literals and character
 * classes (the cases the prefilter optimizes), empty matches, look-around, and nested quantifiers.
 * Word boundaries (`\b`, `\B`), back references and, in `v` mode, string valued character
 * classes (`\q{...}`, set subtraction/intersection and the emoji sequence properties) are drawn
 * as well, because a first byte set is either trivially empty or expensive to derive for them.
 * Patterns rejected by the parser in the drawn mode are skipped by the caller.
 */
class CaseGenerator {
private:
  L64X128MixRNG rng;
  regex::Mode mode{regex::Mode::UNICODE};
  unsigned int groupSerial{0}; // keeps the generated capture group names unique

  int pick(const int n) {
    return static_cast<int>(this->rng.next() % static_cast<unsigned int>(n));
  }

  /**
   * ASCII atoms plus literal 2, 3 and 4 byte UTF-8 characters, so the first byte set of the
   * prefilter is also exercised on multi-byte leading bytes. `\b` and `\B` are zero width, so
   * the prefilter has to look through them to find the first byte it can match.
   */
  static constexpr const char *ATOMS[] = {
      "a",
      "b",
      "T",
      "o",
      "m",
      "0",
      "9",
      " ",
      "\\n",
      "\\t",
      ".",
      "\\d",
      "\\w",
      "\\s",
      "x",
      "q",
      "z",
      "A",
      "-",
      "[a-c]",
      "[^a-c]",
      "[abc]",
      "\\p{Sm}",
      "\\u{1F600}",
      "\xC3\xA9",
      "\xC3\x9F",
      "\xCC\x81",
      "\xE3\x81\x82",
      "\xE2\x98\x83",
      "\xF0\x9F\x98\x80",
      "\\b",
      "\\B",
  };

  /**
   * Atoms which the parser only accepts in `v` mode: `\q{...}` string alternatives, set
   * subtraction and intersection (including the degenerate `[[]--[]]`), and the emoji sequence
   * properties, whose elements are multi code point strings instead of single code points.
   */
  static constexpr const char *UNICODE_SET_ATOMS[] = {
      "\\p{RGI_Emoji}",
      "\\p{Basic_Emoji}",
      "\\p{RGI_Emoji_ZWJ_Sequence}",
      "\\p{RGI_Emoji_Flag_Sequence}",
      "[\\q{ab|c}]",
      "[\\q{}]",
      "[\\q{ab|c|}]",
      "[\\q{\xF0\x9F\x98\x80|ab}]",
      "[\\p{RGI_Emoji}\\q{ab|c}]",
      "[[a-c]--[b]]",
      "[[a-c]&&[b-c]]",
      "[[]--[]]",
      "[[\\w]--[\\d]]",
      "[[\\p{Basic_Emoji}]--[\\q{a}]]",
      "[[\\w]&&[\\q{ab|c}]]",
  };

  bool inUnicodeSetMode() const { return this->mode == regex::Mode::UNICODE_SET; }

  std::string atom() {
    if (this->inUnicodeSetMode() && this->pick(2)) {
      return UNICODE_SET_ATOMS[this->pick(std::size(UNICODE_SET_ATOMS))];
    }
    return ATOMS[this->pick(std::size(ATOMS))];
  }

  std::string nextGroupName() { return "r" + std::to_string(this->groupSerial++); }

  /**
   * A named capture group followed by a back reference to it. The name keeps the reference
   * independent of the group numbering, so the sub-pattern stays valid wherever it is embedded.
   * A reference to an unset group matches empty, which is the kind of zero length match the
   * prefilter must not skip.
   */
  std::string backRef(const int depth) {
    const auto name = this->nextGroupName();
    return "(?<" + name + ">" + this->expression(depth + 1) + ")\\k<" + name + ">";
  }

  /**
   * The same, but the group and its reference are inside a look-behind, so the VM executes the
   * `LBBackRef` path and the prefilter cannot derive a first byte at all.
   */
  std::string backRefInLookBehind(const int depth) {
    const auto name = this->nextGroupName();
    return "(?<=(?<" + name + ">" + this->atom() + ")\\k<" + name + ">)" +
           this->expression(depth + 1);
  }

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
    switch (this->pick(14)) {
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
      return "(?!" + this->atom() + ")" + this->expression(depth + 1);
    case 9:
      return "(?<=" + this->atom() + ")" + this->expression(depth + 1);
    case 10:
      return "^" + this->expression(depth + 1);
    case 11:
      return this->backRef(depth);
    case 12:
      return this->backRefInLookBehind(depth);
    default:
      return this->expression(depth + 1) + "$";
    }
  }

  std::string input() {
    /**
     * `\xC3\xA9`, `\xE3\x81\x82` and `\xF0\x9F\x98\x80` are 2, 3 and 4 byte code points, so a
     * candidate start offset can land inside a multi-byte character. The emoji sequences and the
     * `ab`/`c` pieces give the string valued classes (`\q{ab|c}`, `\p{RGI_Emoji}`) something to
     * match, and let a retry start inside a ZWJ sequence, a keycap sequence and so on.
     */
    static constexpr const char *PIECES[] = {
        "a",
        "b",
        "c",
        "T",
        "o",
        "m",
        " ",
        "0",
        "1",
        "9",
        "\n",
        "t",
        "x",
        "q",
        "z",
        "A",
        "B",
        "-",
        "\xC3\xA9",
        "\xC3\x9F",
        "\xCC\x81",
        "\xE3\x81\x82",
        "\xE2\x98\x83",
        "\xF0\x9F\x98\x80",
        "ab",
        "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA7", // ZWJ sequence
        "\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5",             // flag sequence
        "#\xEF\xB8\x8F\xE2\x83\xA3",                    // keycap sequence
        "\xE2\x9C\x8C\xF0\x9F\x8F\xBC",                 // modifier sequence
    };
    std::string text;
    const int size = this->pick(13);
    for (int i = 0; i < size; i++) {
      text += PIECES[this->pick(std::size(PIECES))];
    }
    return text;
  }

public:
  explicit CaseGenerator(const uint64_t seed) : rng(seed) {}

  regex::Target next() {
    this->groupSerial = 0;
    switch (this->pick(3)) {
    case 0:
      this->mode = regex::Mode::BMP;
      break;
    case 1:
      this->mode = regex::Mode::UNICODE;
      break;
    default:
      this->mode = regex::Mode::UNICODE_SET;
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

    std::string pattern = this->expression(0);
    if (this->pick(6) == 0) {
      // group 1 is the outermost group, so `\1` is a well defined numeric back reference
      pattern = "(" + pattern + ")\\1";
    }

    regex::Target target;
    target.pattern = std::move(pattern);
    target.input = this->input();
    target.mode = this->mode;
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

  static uint64_t envOr(const char *name, const uint64_t fallback) {
    if (const char *str = getenv(name)) {
      if (auto ret = convertToNum10<uint64_t>(str); ret) {
        return ret.value;
      }
    }
    return fallback;
  }
};

TEST_F(PrefilterTest, generated) {
  const auto seed = envOr("ARSH_RE_PREFILTER_SEED", DEFAULT_SEED);
  auto iter = envOr("ARSH_RE_PREFILTER_ITER", DEFAULT_ITER);
  if (iter == 0) {
    iter = DEFAULT_ITER;
  }
  printf(" seed: %llu, iter: %llu\n", static_cast<unsigned long long>(seed),
         static_cast<unsigned long long>(iter));

  CaseGenerator generator(seed);
  for (unsigned int i = 0; i < iter; i++) {
    if (this->HasFatalFailure()) {
      break; // keep the first failing case as the reported one
    }
    ASSERT_NO_FATAL_FAILURE(compare(generator.next())) << "iteration " << i;
  }
}

/**
 * The constructs the generator draws at random, pinned down so that lowering the number of
 * iterations or changing the seed cannot silently drop them. Every entry is the smallest
 * pattern/input pair which exercises one of them; nothing is hard coded about the result, only
 * the prefilter on/off invariant is checked.
 *
 * The first byte set is empty for `\b` and `\B` (zero width), for a back reference (it depends on
 * a previous match) and for `[[]--[]]` (an empty set), and it is expensive to derive for the
 * string valued classes `\q{...}`, `[... -- ...]`, `[... && ...]` and the emoji sequences, whose
 * elements are multi code point strings.
 */
struct PrefilterConstructEntry {
  const char *pattern;
  const char *text;
  regex::Mode mode;
  regex::Modifier modifiers;
};

std::ostream &operator<<(std::ostream &stream, const PrefilterConstructEntry &entry) {
  return stream << "/" << entry.pattern << "/ on `" << entry.text << "'";
}

struct PrefilterConstructTest : public PrefilterTest,
                                public ::testing::WithParamInterface<PrefilterConstructEntry> {
  static void doTest() {
    auto &param = GetParam();
    regex::Target target;
    target.pattern = param.pattern;
    target.input = param.text;
    target.mode = param.mode;
    target.modifiers = param.modifiers;
    compare(target);
  }
};

TEST_P(PrefilterConstructTest, base) { ASSERT_NO_FATAL_FAILURE(doTest()); }

static const PrefilterConstructEntry prefilterConstructCases[] = {
    // zero width word boundary: the prefilter must look through it
    {"\\b", "ab c", regex::Mode::UNICODE, regex::Modifier::NONE},
    {"\\B", "ab c", regex::Mode::UNICODE, regex::Modifier::NONE},
    {"\\b\\B", "ab c", regex::Mode::UNICODE, regex::Modifier::NONE},
    {"\\b(a)\\1", "aab aab", regex::Mode::UNICODE, regex::Modifier::NONE},
    {"\\b[\\q{ab|c}]", "abc", regex::Mode::UNICODE_SET, regex::Modifier::NONE},
    {"[\\q{ab|c}]\\B", "abc", regex::Mode::UNICODE_SET, regex::Modifier::NONE},
    // back reference, numeric and named, including the look-behind form
    {"(a)\\1", "aab", regex::Mode::UNICODE, regex::Modifier::NONE},
    {"(?<g0>a)\\k<g0>", "aab", regex::Mode::UNICODE, regex::Modifier::NONE},
    {"(?<=(a)\\1)b", "aab", regex::Mode::UNICODE, regex::Modifier::NONE},
    {"(?<=(?<g0>a)\\k<g0>)b", "aab", regex::Mode::UNICODE, regex::Modifier::NONE},
    {"(a)\\1", "aAab", regex::Mode::UNICODE, regex::Modifier::IGNORE_CASE},
    // empty string class, plain and as a set operand
    {"[\\q{}]", "abc", regex::Mode::UNICODE_SET, regex::Modifier::NONE},
    {"[\\q{ab|c|}]", "abc", regex::Mode::UNICODE_SET, regex::Modifier::NONE},
    {"[[]--[]]", "abc", regex::Mode::UNICODE_SET, regex::Modifier::NONE},
    {"[[a-c]--[b]]", "abc", regex::Mode::UNICODE_SET, regex::Modifier::NONE},
    {"[[a-c]&&[b-c]]", "abc", regex::Mode::UNICODE_SET, regex::Modifier::NONE},
    {"[[\\w]--[\\d]]", "a1c", regex::Mode::UNICODE_SET, regex::Modifier::NONE},
    {"[[\\w]&&[\\q{ab|c}]]", "abc", regex::Mode::UNICODE_SET, regex::Modifier::NONE},
    // string valued classes: \q{...} alternatives and emoji sequences
    {"[\\q{ab|c}]", "abc", regex::Mode::UNICODE_SET, regex::Modifier::NONE},
    {"[\\p{RGI_Emoji}\\q{ab|c}]", "abc", regex::Mode::UNICODE_SET, regex::Modifier::NONE},
    {"[[\\p{Basic_Emoji}]--[\\q{a}]]", "abc", regex::Mode::UNICODE_SET, regex::Modifier::NONE},
    {"\\p{RGI_Emoji}", "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA7", regex::Mode::UNICODE_SET,
     regex::Modifier::NONE},
    {"[\\p{RGI_Emoji}]", "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA7", regex::Mode::UNICODE_SET,
     regex::Modifier::NONE},
    {"[\\p{RGI_Emoji_ZWJ_Sequence}]", "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA7",
     regex::Mode::UNICODE_SET, regex::Modifier::NONE},
    {"[\\p{RGI_Emoji_Flag_Sequence}]", "\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5", regex::Mode::UNICODE_SET,
     regex::Modifier::NONE},
    {"[\\p{RGI_Emoji_Tag_Sequence}]",
     "\xF0\x9F\x8F\xB4\xF3\xA0\x81\xA7\xF3\xA0\x81\xA2\xF3\xA0\x81\xA5\xF3\xA0\x81\xAE"
     "\xF3\xA0\x81\xA7\xF3\xA0\x81\xBF",
     regex::Mode::UNICODE_SET, regex::Modifier::NONE},
    // the retry offset may land inside a keycap, modifier or flag sequence
    {"[\\p{Basic_Emoji}]", "#\xEF\xB8\x8F\xE2\x83\xA3\xE2\x9C\x8C\xF0\x9F\x8F\xBC",
     regex::Mode::UNICODE_SET, regex::Modifier::NONE},
    {"[\\p{Basic_Emoji}]", "\xC3\xA9\xF0\x9F\x98\x80\xC3\x9F", regex::Mode::UNICODE_SET,
     regex::Modifier::NONE},
    {"[\\p{RGI_Emoji}]", "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA7", regex::Mode::UNICODE_SET,
     regex::Modifier::IGNORE_CASE},
};

INSTANTIATE_TEST_SUITE_P(PrefilterConstructTest, PrefilterConstructTest,
                         ::testing::ValuesIn(prefilterConstructCases));

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
