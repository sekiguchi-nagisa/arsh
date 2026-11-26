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
 * Differential test of the arsh regex engine against the regexp engine used by Node.js (V8).
 *
 * Every case in the case files is compiled and matched twice: once by arsh (in process, see
 * `runArsh`) and once by the Node.js oracle (see `oracle.js`). arsh reports UTF-8 byte offsets,
 * JavaScript reports UTF-16 code unit indices, so the oracle side is translated with
 * `Utf16Index` before the two are compared.
 *
 * Both engines are only driven in the unicode (`u`) and unicode sets (`v`) modes, which are the
 * code point based modes, so the comparison is meaningful; a case in any other mode is rejected as
 * a corpus error.
 *
 * Two things are deliberately kept out of the comparison so a run reports engine bugs only:
 *
 *   - Unicode data version (`\p{Emoji}` and friends): a case marked `"unicode": "versioned"` is
 *     skipped unless arsh and Node.js bundle the same Unicode version, and the harness prints both
 *     so it is obvious why cases were skipped.
 *   - Syntax the oracle implements only in newer releases (`v` sets, inline modifiers, properties
 *     of strings): a case whose `requires` list mentions a missing feature is skipped.
 *
 * A skipped case is never a failure. An `xfail` case documents a known divergence and pins both
 * sides to the result that was recorded when it was filed, so it neither breaks the build nor lets
 * the bug drift unnoticed (see `cases/known-bugs.jsonl`).
 */

#include <algorithm>
#include <fstream>
#include <functional>
#include <utility>

#include <misc/unicode_version.in>

#include "../../test_common.h"
#include "difftest.h"
#include "oracle.h"

#ifndef NODE_PATH
#error require NODE_PATH
#endif

#ifndef ORACLE_SCRIPT_PATH
#error require ORACLE_SCRIPT_PATH
#endif

#ifndef DIFFTEST_CASE_DIR
#error require DIFFTEST_CASE_DIR
#endif

using namespace arsh;
using namespace arsh::difftest;

namespace {

/**
 * Read every `*.jsonl` case file under the corpus directory, in a stable order.
 */
std::vector<std::pair<std::string, Case>> loadCorpus(const char *dir) {
  auto files = getFileList(dir, true);
  files.erase(std::remove_if(files.begin(), files.end(),
                             [](const std::string &v) { return !StringRef(v).endsWith(".jsonl"); }),
              files.end());
  std::sort(files.begin(), files.end());

  std::vector<std::pair<std::string, Case>> ret;
  for (const auto &file : files) {
    std::ifstream stream(file);
    if (!stream.is_open()) {
      ADD_FAILURE() << "cannot open " << file;
      continue;
    }
    for (std::string line; std::getline(stream, line);) {
      if (line.empty()) {
        continue;
      }
      auto json = json::JSON::fromString(line.c_str());
      if (!json.isObject()) {
        ADD_FAILURE() << "malformed case in " << file << ": " << line;
        continue;
      }
      ret.emplace_back(file, Case::fromJSON(json));
    }
  }
  return ret;
}

/**
 * The Unicode version bundled with arsh, from `misc/unicode_version.in`.
 */
const char *arshUnicodeVersion() { return UNICODE_VERSION_STR; }

/**
 * Node.js and the Unicode version it bundles are read from the oracle banner. The oracle lives for
 * the whole test binary: starting Node.js per case would dominate the run time.
 *
 * A `node` older than `NODE_MIN_VERSION_STR` is refused by `Oracle::start`, so the reason is kept
 * here and reported once by `environment()`.
 */
Oracle &oracle(std::string *err = nullptr) {
  static Oracle instance;
  static bool started = false;
  static std::string error;
  if (!started) {
    started = true;
    instance.start(NODE_PATH, ORACLE_SCRIPT_PATH, error);
  }
  if (err != nullptr) {
    *err = error;
  }
  return instance;
}

/**
 * Which cases this run compares. The oracle is asked once, so the skips are decided up front and
 * can be reported together instead of per case.
 */
struct Environment {
  bool oracleAvailable{false};
  std::string error; // why the oracle could not be started
  std::string nodeVersion;
  std::string unicodeVersion;
  std::map<std::string, bool> features;
  bool sameUnicodeVersion{false};
};

const Environment &environment() {
  static Environment env;
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    auto &o = oracle(&env.error);
    env.oracleAvailable = o.isStarted();
    if (!env.oracleAvailable) {
      // a completely skipped run must not look like a passing one, so say it out loud once,
      // together with the reason (an unusable Node.js version is the usual one)
      fprintf(stderr,
              "[warning] no usable Node.js oracle (" NODE_PATH ", at least " NODE_MIN_VERSION_STR
              " is required); the differential test compares nothing");
      if (!env.error.empty()) {
        fprintf(stderr, ": %s", env.error.c_str());
      }
      fputc('\n', stderr);
    }
    if (env.oracleAvailable) {
      env.nodeVersion = o.getNodeVersion();
      env.unicodeVersion = o.getUnicodeVersion();
      env.features = o.getCapabilities().features;
      env.sameUnicodeVersion = env.unicodeVersion == arshUnicodeVersion();
    }
  }
  return env;
}

/**
 * Decide whether a case may be compared, appending the reason to `reason` when it may not.
 */
bool isComparable(const Environment &env, const Case &c, std::string &reason) {
  for (const auto &feature : c.requiredFeatures) {
    auto iter = env.features.find(feature);
    if (iter == env.features.end() || !iter->second) {
      reason = "the oracle (Node.js " + env.nodeVersion + ") does not support `" + feature + "'";
      return false;
    }
  }
  if (c.isVersioned() && !env.sameUnicodeVersion) {
    reason = "Unicode version dependent case (arsh=" + std::string(arshUnicodeVersion()) +
             ", node=" + env.unicodeVersion + ')';
    return false;
  }
  if (!c.isUnicodeMode()) {
    reason = "the case is not run in the unicode / unicode sets mode";
    return false;
  }
  return true;
}

class RegexDiffTest : public ::testing::TestWithParam<std::pair<std::string, Case>> {
protected:
  void SetUp() override {
    const auto &c = this->GetParam().second;
    if (!c.isUnicodeMode()) {
      FAIL() << "every case must be run in the unicode or unicode sets mode: " << formatCase(c);
    }
  }
};

TEST_P(RegexDiffTest, base) {
  const auto &[file, c] = this->GetParam();
  const auto &env = environment();

  if (!env.oracleAvailable) {
    if (!env.error.empty()) {
      GTEST_SKIP() << "no usable Node.js oracle: " << env.error;
    }
    GTEST_SKIP() << "no Node.js oracle";
  }
  std::string skipReason;
  if (!isComparable(env, c, skipReason)) {
    GTEST_SKIP() << skipReason;
  }

  const auto arsh = runArsh(c);
  if (arsh.isSkip()) {
    GTEST_SKIP() << "arsh could not run the case: " << arsh.message;
  }
  if (arsh.kind == MatchResult::Kind::RUNTIME_ERROR) {
    FAIL() << "arsh failed: " << arsh.message << "\n  case: " << formatCase(c);
  }
  const auto node = oracle().match(c);
  if (node.isSkip()) {
    GTEST_SKIP() << "the oracle could not run the case: " << node.message;
  }
  if (node.kind == MatchResult::Kind::RUNTIME_ERROR) {
    GTEST_SKIP() << "the oracle failed: " << node.message;
  }

  std::string report;
  const bool agree = compareResult(arsh, node, report);
  if (c.xfail) {
    // the case documents a known divergence: it has to keep failing in exactly the recorded way,
    // so a fixed bug (drop the marker) and a changed bug (update the record) are both reported
    if (agree) {
      FAIL() << "the case is marked xfail but arsh now agrees with the oracle; drop the marker\n"
             << "  case: " << formatCase(c) << "\n"
             << "  result: " << arsh.toCompactString();
    }
    const std::string arshStr = arsh.toCompactString();
    const std::string nodeStr = node.toCompactString();
    if (arshStr != c.xfailArsh || nodeStr != c.xfailOracle) {
      FAIL() << "the recorded xfail result changed\n"
             << "  case: " << formatCase(c) << "\n"
             << "  arsh  : recorded=`" << c.xfailArsh << "' actual=`" << arshStr << "'\n"
             << "  oracle: recorded=`" << c.xfailOracle << "' actual=`" << nodeStr << "'";
    }
    GTEST_SKIP() << "known divergence (xfail)\n" << report;
  }
  if (!agree) {
    FAIL() << "arsh and the Node.js oracle disagree\n"
           << "  file: " << file << '\n'
           << "  case: " << formatCase(c) << '\n'
           << report;
  }
}

/**
 * Turn a corpus file name into a gtest parameter name: `literal.jsonl` becomes `literal_<index>`.
 * The index keeps the names unique and the file name makes a failure easy to find.
 */
std::string toParamName(const std::string &file, const size_t index) {
  std::string name = file.substr(file.find_last_of('/') + 1);
  if (StringRef(name).endsWith(".jsonl")) {
    name.resize(name.size() - 6);
  }
  for (char &ch : name) {
    if ((ch < 'a' || ch > 'z') && (ch < 'A' || ch > 'Z') && (ch < '0' || ch > '9')) {
      ch = '_';
    }
  }
  name += '_';
  name += std::to_string(index);
  return name;
}

INSTANTIATE_TEST_SUITE_P(RegexDiffTest, RegexDiffTest,
                         ::testing::ValuesIn(loadCorpus(DIFFTEST_CASE_DIR)),
                         [](const ::testing::TestParamInfo<std::pair<std::string, Case>> &info) {
                           return toParamName(info.param.first, info.index);
                         });

/**
 * The corpus has to stay inside the modes both engines can be compared in. This is cheap and
 * catches a mistyped `flags` entry in a case file, which would otherwise be silently skipped.
 */
TEST(RegexDiffCorpus, unicodeModeOnly) {
  for (const auto &[file, c] : loadCorpus(DIFFTEST_CASE_DIR)) {
    EXPECT_TRUE(c.isUnicodeMode()) << file << ": " << formatCase(c);
  }
}

/**
 * The loop cases are the ones guarding the backtrack stack, where a change to the way a loop saves
 * and restores its state (or merges backtrack entries) has historically caused regressions. Keep
 * them present and broad enough that such a change cannot land without being exercised: nested
 * loops, empty-matching bodies, capture groups, lazy quantifiers, bounded repeats and lookarounds
 * all have to be represented.
 */
TEST(RegexDiffCorpus, loopCoverage) {
  size_t total = 0;
  size_t nested = 0;
  size_t emptyBody = 0;
  size_t captures = 0;
  size_t lazy = 0;
  size_t bounded = 0;
  size_t lookaround = 0;
  for (const auto &[file, c] : loadCorpus(DIFFTEST_CASE_DIR)) {
    if (!StringRef(file).endsWith("loops.jsonl")) {
      continue;
    }
    total++;
    const auto &p = c.pattern;
    if (p.find(")*") != std::string::npos && p.find("(?") != std::string::npos) {
      nested++;
    }
    if (p.find("(?:)*") != std::string::npos || p.find("a?)*") != std::string::npos ||
        p.find("a*)*") != std::string::npos || p.find("x{0})") != std::string::npos ||
        p.find("(?:|a)") != std::string::npos || p.find("a|)*") != std::string::npos) {
      emptyBody++;
    }
    if (p.find("(a") != std::string::npos || p.find("(?<") != std::string::npos) {
      captures++;
    }
    if (p.find("*?") != std::string::npos || p.find("+?") != std::string::npos ||
        p.find("??") != std::string::npos || p.find("}?") != std::string::npos) {
      lazy++;
    }
    if (p.find('{') != std::string::npos) {
      bounded++;
    }
    if (p.find("(?=") != std::string::npos || p.find("(?!") != std::string::npos ||
        p.find("(?<=") != std::string::npos || p.find("(?<!") != std::string::npos) {
      lookaround++;
    }
  }
  EXPECT_GT(total, 500u);
  EXPECT_GT(nested, 20u);
  EXPECT_GT(emptyBody, 5u);
  EXPECT_GT(captures, 20u);
  EXPECT_GT(lazy, 10u);
  EXPECT_GT(bounded, 10u);
  EXPECT_GT(lookaround, 10u);
}

/**
 * The features of the ECMAScript grammar that do not exist in a byte oriented engine have their
 * own case files, and each of them is easy to trim down to a handful of cases without noticing
 * that a whole family stopped being compared. This keeps the breadth of the three feature groups
 * honest: every lookaround kind on both sides of an alternation, every way a back reference can be
 * written, and every operand kind the unicode sets operations accept.
 */
TEST(RegexDiffCorpus, featureCoverage) {
  auto countIn = [](const char *suffix,
                    const std::function<bool(const std::string &)> &pred) -> size_t {
    size_t count = 0;
    for (const auto &[file, c] : loadCorpus(DIFFTEST_CASE_DIR)) {
      if (StringRef(file).endsWith(suffix) && pred(c.pattern)) {
        count++;
      }
    }
    return count;
  };
  auto hasAny = [](const std::string &p, const std::vector<const char *> &needles) {
    for (const auto *n : needles) {
      if (p.find(n) != std::string::npos) {
        return true;
      }
    }
    return false;
  };

  // lookaround: all four kinds, nesting, captures inside an assertion, assertions in a loop and
  // astral input (a look-behind has to walk a code point back, not a code unit)
  EXPECT_GT(countIn("assertion.jsonl",
                    [](const std::string &p) { return p.find("(?=") != std::string::npos; }),
            20u);
  EXPECT_GT(countIn("assertion.jsonl",
                    [](const std::string &p) { return p.find("(?!") != std::string::npos; }),
            10u);
  EXPECT_GT(countIn("assertion.jsonl",
                    [](const std::string &p) { return p.find("(?<=") != std::string::npos; }),
            30u);
  EXPECT_GT(countIn("assertion.jsonl",
                    [](const std::string &p) { return p.find("(?<!") != std::string::npos; }),
            10u);
  EXPECT_GT(countIn("assertion.jsonl",
                    [&](const std::string &p) {
                      return hasAny(p, {"(?=(?=", "(?!(?!", "(?<=(?<", "(?=(?=(?=", "(?<=(?<=(?<"});
                    }),
            8u);
  EXPECT_GT(countIn("assertion.jsonl",
                    [&](const std::string &p) {
                      return hasAny(p, {"(?=(", "(?<=(", "(?=(?<", "(?<=(a"});
                    }),
            8u);
  EXPECT_GT(countIn("assertion.jsonl",
                    [](const std::string &p) { return p.find("(?:(?") != std::string::npos; }),
            8u);
  EXPECT_GT(countIn("assertion.jsonl",
                    [](const std::string &p) { return p.find("\xF0") != std::string::npos; }),
            5u);

  // a capture saved below a successful lookaround has to be undone again when the enclosing
  // alternative is abandoned; the VM currently keeps such a capture alive, so the shapes that
  EXPECT_GT(countIn("assertion.jsonl",
                    [&](const std::string &p) {
                      return hasAny(p, {"(?<=(a)|(b))", "(?=(a)|(b))", "(?<!(a)|(b))"});
                    }),
            4u);

  // the three groups below are the ones the lookaround fix proposal called out as worth covering
  // before changing `cleanupLookAround`: a loop that contains an assertion and a capture (the
  // `ResetCaptures` of the loop and the `SetCapture` of the assertion end up interleaved on the
  // stack), and patterns deep enough to push the backtrack stack hard
  EXPECT_GT(countIn("loops.jsonl",
                    [&](const std::string &p) {
                      return hasAny(p, {"(?=", "(?<=", "(?!"}) && p.find('\\') != std::string::npos;
                    }),
            20u);
  EXPECT_GT(countIn("loops.jsonl",
                    [&](const std::string &p) {
                      return hasAny(p, {"(?=", "(?<=", "(?!"}) && p.find('{') != std::string::npos;
                    }),
            4u);
  EXPECT_GT(countIn("deep-nesting.jsonl",
                    [](const std::string &p) { return p.find("(?=(?=") != std::string::npos; }),
            8u);
  EXPECT_GT(countIn("deep-nesting.jsonl",
                    [](const std::string &p) { return p.find("(?<=(?<=") != std::string::npos; }),
            3u);
  EXPECT_GT(countIn("deep-nesting.jsonl",
                    [](const std::string &p) { return p.find("(?:(?:") != std::string::npos; }),
            8u);

  // back reference: numeric, named, multi digit, forward (a group that has not participated yet),
  // a group skipped by its alternative, caseless folding and astral text
  EXPECT_GT(countIn("backreference.jsonl",
                    [](const std::string &p) {
                      return p.find("\\1") != std::string::npos ||
                             p.find("\\2") != std::string::npos;
                    }),
            30u);
  EXPECT_GT(countIn("backreference.jsonl",
                    [](const std::string &p) { return p.find("\\k<") != std::string::npos; }),
            15u);
  EXPECT_GT(countIn("backreference.jsonl",
                    [](const std::string &p) { return p.find("\\10") != std::string::npos; }),
            2u);
  EXPECT_GT(countIn("backreference.jsonl",
                    [](const std::string &p) {
                      return p.find("\\1(a)") != std::string::npos ||
                             p.find("\\k<x>(?<x>") != std::string::npos;
                    }),
            3u);
  EXPECT_GT(countIn("backreference.jsonl",
                    [](const std::string &p) {
                      return p.find("(?:(a)|b)") != std::string::npos ||
                             p.find("(a)|\\1") != std::string::npos ||
                             p.find("(?:(a)|(b))") != std::string::npos;
                    }),
            6u);
  EXPECT_GT(countIn("backreference.jsonl",
                    [](const std::string &p) { return p.find("\xF0") != std::string::npos; }),
            3u);

  // unicode sets: both operators, class strings, properties of strings, negation and nesting
  EXPECT_GT(countIn("unicode-sets.jsonl",
                    [](const std::string &p) { return p.find("&&") != std::string::npos; }),
            15u);
  EXPECT_GT(countIn("unicode-sets.jsonl",
                    [](const std::string &p) { return p.find("--") != std::string::npos; }),
            40u);
  EXPECT_GT(countIn("unicode-sets.jsonl",
                    [](const std::string &p) { return p.find("\\q{") != std::string::npos; }),
            40u);
  EXPECT_GT(countIn("unicode-sets.jsonl",
                    [](const std::string &p) { return p.find("[[") != std::string::npos; }),
            10u);
  EXPECT_GT(countIn("unicode-sets.jsonl",
                    [](const std::string &p) { return p.find("[^") != std::string::npos; }),
            5u);
  EXPECT_GT(countIn("unicode-sets.jsonl",
                    [&](const std::string &p) {
                      return hasAny(p, {"RGI_Emoji}", "Basic_Emoji", "Emoji_Keycap_Sequence",
                                        "RGI_Emoji_Flag_Sequence", "Modifier_Sequence",
                                        "Tag_Sequence"});
                    }),
            15u);
}

/**
 * The minimum Node.js version decides whether the oracle may be used at all, so both the parsing
 * and the ordering are checked directly: a malformed version must not be read as `0.0.0`, and the
 * comparison must be numeric rather than lexicographic (`26.8.2` is older than `26.10.0`).
 */
TEST(RegexDiffNodeVersion, parse) {
  {
    const auto v = NodeVersion::parse("26.8.2");
    ASSERT_TRUE(v.valid);
    EXPECT_EQ(26u, v.major);
    EXPECT_EQ(8u, v.minor);
    EXPECT_EQ(2u, v.patch);
  }
  {
    // the form `node --version` prints; the banner reports `process.versions.node` without it
    const auto v = NodeVersion::parse("v26.8.2");
    ASSERT_TRUE(v.valid);
    EXPECT_EQ(26u, v.major);
    EXPECT_EQ(8u, v.minor);
    EXPECT_EQ(2u, v.patch);
  }
  {
    // a missing component counts as 0, so a bare major version is usable as a requirement
    const auto v = NodeVersion::parse("26");
    ASSERT_TRUE(v.valid);
    EXPECT_EQ(26u, v.major);
    EXPECT_EQ(0u, v.minor);
    EXPECT_EQ(0u, v.patch);
  }
  {
    const auto v = NodeVersion::parse("24.1");
    ASSERT_TRUE(v.valid);
    EXPECT_EQ(24u, v.major);
    EXPECT_EQ(1u, v.minor);
    EXPECT_EQ(0u, v.patch);
  }
  {
    // a pre-release suffix belongs to a development build of that line; only the numbers compare
    const auto v = NodeVersion::parse("27.0.0-nightly20260101");
    ASSERT_TRUE(v.valid);
    EXPECT_EQ(27u, v.major);
    EXPECT_EQ(0u, v.minor);
    EXPECT_EQ(0u, v.patch);
  }
  {
    // nothing numerical: unusable, not 0.0.0
    EXPECT_FALSE(NodeVersion::parse("").valid);
    EXPECT_FALSE(NodeVersion::parse("v").valid);
    EXPECT_FALSE(NodeVersion::parse("node").valid);
  }
}

TEST(RegexDiffNodeVersion, compare) {
  const auto v = [](const char *str) { return NodeVersion::parse(str); };
  EXPECT_FALSE(v("26.8.2") < v("26.8.2"));
  EXPECT_LT(v("26.8.2"), v("26.8.10")); // numeric, not lexicographic
  EXPECT_LT(v("26.8.2"), v("26.9.0"));
  EXPECT_LT(v("26.8.2"), v("27.0.0"));
  EXPECT_LT(v("9.0.0"), v("26.0.0"));
  EXPECT_LT(v("26.7.0"), v("26.8.2"));
  EXPECT_FALSE(v("26.9.0") < v("26.8.2"));

  // `26` and `26.0.0` denote the same version, so a `26` requirement accepts both
  EXPECT_FALSE(v("26") < v("26.0.0"));
  EXPECT_FALSE(v("26.0.0") < v("26"));
  EXPECT_FALSE(v("26.8.2") < v("26"));

  // the requirement baked in at configure time (`-DNODE_MIN_VERSION`) has to be a usable version
  const auto required = NodeVersion::required();
  ASSERT_TRUE(required.valid) << "NODE_MIN_VERSION_STR is not a version: " NODE_MIN_VERSION_STR;
  EXPECT_FALSE(required < v("26.0.0"));
  EXPECT_LT(v("25.9.9"), required);
}

/**
 * The translation from UTF-16 code unit indices to UTF-8 byte offsets is the piece that makes the
 * two engines comparable, so it is checked directly: BMP text must map one to one in length, and a
 * supplementary code point must occupy two code units and four bytes.
 */
TEST(RegexDiffUtf16Index, base) {
  {
    const auto index = Utf16Index::create("abc");
    ASSERT_EQ(3, index.getCodeUnitCount());
    EXPECT_EQ(0u, index.toByteOffset(0));
    EXPECT_EQ(1u, index.toByteOffset(1));
    EXPECT_EQ(3u, index.toByteOffset(3));
    EXPECT_EQ(Utf16Index::INVALID, index.toByteOffset(4));
  }
  {
    // U+00E9 is 2 UTF-8 bytes but a single code unit
    const StringRef input = "\u00e9";
    const auto index = Utf16Index::create(input);
    ASSERT_EQ(1, index.getCodeUnitCount());
    EXPECT_EQ(0u, index.toByteOffset(0));
    EXPECT_EQ(2u, index.toByteOffset(1));
  }
  {
    // U+1F600 is 4 UTF-8 bytes and two code units, so the middle index has no byte offset
    const StringRef input = "\U0001F600";
    const auto index = Utf16Index::create(input);
    ASSERT_EQ(2, index.getCodeUnitCount());
    EXPECT_EQ(0u, index.toByteOffset(0));
    EXPECT_EQ(Utf16Index::INVALID, index.toByteOffset(1));
    EXPECT_EQ(4u, index.toByteOffset(2));
  }
  {
    // a mix, to be sure the table stays aligned after a surrogate pair
    const StringRef input = "a\U0001F600b\u00e9";
    const auto index = Utf16Index::create(input);
    ASSERT_EQ(5, index.getCodeUnitCount());
    EXPECT_EQ(0u, index.toByteOffset(0)); // a
    EXPECT_EQ(1u, index.toByteOffset(1)); // U+1F600 start
    EXPECT_EQ(5u, index.toByteOffset(3)); // b
    EXPECT_EQ(6u, index.toByteOffset(4)); // U+00E9 start
    EXPECT_EQ(8u, index.toByteOffset(5)); // end
  }
}

} // namespace
