
#include "../test_common.h"

#include <config.h> // for USE_REGEX_JIT

#include <regex/emit.h>
#include <regex/instruction.h>
#include <regex/match_context.h>
#include <regex/parser.h>
#include <regex/regex.h>

#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#ifdef USE_REGEX_JIT

#include "stencil_data.h"

#endif

using namespace arsh;

namespace {

Optional<regex::Regex> compileRegex(const StringRef pattern,
                                     const regex::Flag &flag = regex::Flag()) {
  regex::Parser parser;
  if (auto tree = parser(pattern, flag)) {
    regex::CodeGen codeGen;
    return codeGen(std::move(tree));
  }
  return {};
}

std::string toString(const std::vector<regex::Capture> &captures) {
  std::string out;
  for (const auto &capture : captures) {
    out += '[';
    out += std::to_string(capture.offset);
    out += ',';
    out += std::to_string(capture.size);
    out += ']';
  }
  return out;
}

/**
 * run the same pattern through the JIT path and the interpreter and compare.
 *
 * when the JIT is disabled (`USE_REGEX_JIT` not defined) `regex::match()` *is* the interpreter, so
 * this only checks self-consistency. the real assertion is the `USE_REGEX_JIT` build.
 */
::testing::AssertionResult matchEquals(regex::Regex &regex, const StringRef text) {
  std::vector<regex::Capture> jitCaptures;
  const auto jitStatus = regex::match(regex, text, jitCaptures, nullptr);

  std::vector<regex::Capture> interpCaptures;
  regex::MatchStatus interpStatus;
  if (auto ctx = regex::tryToCreateMatchContext(regex, text, 0, interpCaptures)) {
    interpStatus = regex::interpret(ctx.asOk(), nullptr);
  } else {
    interpStatus = ctx.asErr();
  }

  if (jitStatus != interpStatus) {
    return ::testing::AssertionFailure()
           << "status differs: jit=" << static_cast<int>(jitStatus)
           << " interp=" << static_cast<int>(interpStatus);
  }
  const auto a = toString(jitCaptures);
  const auto b = toString(interpCaptures);
  if (a != b) {
    return ::testing::AssertionFailure() << "captures differ: jit=" << a << " interp=" << b;
  }
  return ::testing::AssertionSuccess();
}

} // namespace

#ifdef USE_REGEX_JIT

namespace {

constexpr size_t OPCODE_COUNT = 0
#define GEN_OPCODE_COUNT(E) +1
    EACH_RE_OPCODE(GEN_OPCODE_COUNT)
#undef GEN_OPCODE_COUNT
    ;

} // namespace

/**
 * every opcode must have a stencil. `gen_jit_stencil` drops any instruction it cannot patch, so a
 * missing entry would silently degrade that opcode to the interpreter.
 */
TEST(RegexJitTest, allOpcodesHaveStencil) {
  ASSERT_EQ(OPCODE_COUNT, std::size(regex::jit::STENCILS));
  for (const auto &stencil : regex::jit::STENCILS) {
    EXPECT_GT(stencil.size, 0u) << stencil.opcode;
    // a stencil must be patchable, otherwise it could not continue to the next instruction
    EXPECT_GT(stencil.holeCount, 0u) << stencil.opcode;
    for (unsigned int i = 0; i < stencil.holeCount; i++) {
      EXPECT_LE(stencil.holes[i].offset + stencil.holes[i].size, stencil.size) << stencil.opcode;
    }
  }
}

TEST(RegexJitTest, stencilNamesAreUnique) {
  std::set<std::string> names;
  for (const auto &stencil : regex::jit::STENCILS) {
    EXPECT_TRUE(names.insert(stencil.opcode).second) << stencil.opcode;
  }
}

#endif

/**
 * the core contract: the JIT and the interpreter must agree on the status and every capture.
 */
struct RegexJitCase {
  const char *pattern;
  const char *flag;
};

class RegexJitDifferentialTest : public ::testing::TestWithParam<RegexJitCase> {};

TEST_P(RegexJitDifferentialTest, matchesInterpreter) {
  const auto &param = this->GetParam();
  std::string err;
  auto flag = regex::Flag::parse(param.flag, &err);
  ASSERT_TRUE(flag.hasValue()) << err;
  auto regex = compileRegex(param.pattern, flag.unwrap());
  ASSERT_TRUE(regex.hasValue());

  static const char *inputs[] = {
      "",          "a",           "b",        "abc",        "aaa",      "abab",
      "aabbcc",    "xyz",         "foo",      "foobar",     "barbaz",   "123",
      "abc123",    "A1",          "  \t\n",   "aaaab",      "The quick brown fox",
      "a\nb",      "\n",          "a.b",      "Hello World", "aabb",
      "AbC",       "abcabc",      "aaab",     "bbb",        "aXb",      "\xc3\xa9",
      "\xe3\x81\x82\xe3\x81\x84",
  };
  for (const char *input : inputs) {
    ASSERT_TRUE(matchEquals(regex.unwrap(), StringRef(input)))
        << "pattern=/" << param.pattern << "/ flag=" << param.flag << " input=\"" << input << '"';
  }
}

INSTANTIATE_TEST_SUITE_P(
    RegexJit, RegexJitDifferentialTest,
    ::testing::ValuesIn(std::vector<RegexJitCase>{
        // basic
        {"a", ""},
        {"abc", ""},
        {"a.c", ""},
        {".", ""},
        {".*", "s"},
        {"", ""},
        {"^$", ""},
        // boundary
        {"^a", ""},
        {"a$", ""},
        {"^abc$", "m"},
        {"\\ba\\b", ""},
        {"\\Ba\\B", ""},
        {"\\b\\w+\\b", ""},
        {"a{1,2}b", ""},
        {"x*y?z+", ""},
        // class
        {"[abc]", ""},
        {"[^abc]", ""},
        {"[a-z]+", ""},
        {"[a-zA-Z]+", ""},
        {"[a-z]{2,4}", ""},
        {"[^\\d]+", ""},
        {"\\d+", ""},
        {"\\w+", ""},
        {"\\s+", ""},
        {"\\D", ""},
        {"\\W", ""},
        {"\\S", ""},
        {"\\p{Alpha}+", ""},
        {"\\p{Nd}+", ""},
        // caseless
        {"abc", "i"},
        {"[a-z]+", "i"},
        {"(?i:abc)", ""},
        {"(?-i:AbC)", ""},
        {"(?i:a)b", ""},
        // quantifier
        {"a*", ""},
        {"a+", ""},
        {"a?", ""},
        {"a{2,3}", ""},
        {"a{0,3}", ""},
        {"a{2,}", ""},
        {"a*?b", ""},
        {"a+?", ""},
        {"a??", ""},
        // alternation
        {"a|", ""},
        {"|a", ""},
        {"(a|b)+", ""},
        {"(a|b)*c", ""},
        {"(?:a|b)c", ""},
        {"(?:a|ab)+c", ""},
        {"(a)|(b)", ""},
        {"fox|dog|cat", ""},
        // capture
        {"(a)(b)(c)", ""},
        {"(.)(.)(.)\\3\\2\\1", ""},
        {"(a)(b)?c", ""},
        {"(a)?b", ""},
        {"((a)|(b))*", ""},
        {"(?<name>[a-z]+)(?<num>[0-9]+)", ""},
        // backref
        {"(a)\\1", ""},
        {"(?<x>a)\\k<x>", ""},
        {"(?<n>a)?b\\k<n>", ""},
        // lookaround
        {"(?=a)a", ""},
        {"(?!b)a", ""},
        {"(?<=a)b", ""},
        {"(?<!a)b", ""},
        {"(?<=foo)bar", ""},
        {"(?<!foo)bar", ""},
        {"(?:(?=a))a", ""},
        // nested loops
        {"(a+)+", ""},
        {"(a*)*b", ""},
        {"(a|a)*b", ""},
        {"(a?)*b", ""},
        {"((a)*)*", ""},
        {"(a*?)*", ""},
        {"(0|1)*2", ""},
        // misc
        {"x(?:y)?z", ""},
        {"\\s\\S", ""},
        {"\\X", ""},
        {"\\u0041", ""},
        {"\\x41", ""},
        {"\\cA", ""},
        {"[\\w&&\\d]+", ""},
        {"(?:ab)+", ""},
    }));

/**
 * the compiled code must be cached on the `Regex` and compiled only once.
 */
TEST(RegexJitTest, codeIsCached) {
  auto regex = compileRegex("(\\w+)\\s+(\\w+)");
  ASSERT_TRUE(regex.hasValue());
  auto &re = regex.unwrap();

#ifdef USE_REGEX_JIT
  // the JIT is gated at run time by ARSH_REGEX_JIT, so the assertion depends on it too
  const bool expectJit = std::getenv("ARSH_REGEX_JIT") != nullptr;
  ASSERT_EQ(regex::Regex::JitState::NotTried, re.getJitState());

  std::vector<regex::Capture> captures;
  ASSERT_EQ(regex::MatchStatus::OK, regex::match(re, StringRef("hello world"), captures, nullptr));
  if (!expectJit) {
    ASSERT_EQ(nullptr, re.getJitCode().get());
    return;
  }
  ASSERT_EQ(regex::Regex::JitState::Compiled, re.getJitState());
  const auto *first = re.getJitCode().get();
  ASSERT_NE(nullptr, first);

  // a second match must reuse the same code object
  ASSERT_EQ(regex::MatchStatus::OK, regex::match(re, StringRef("foo bar"), captures, nullptr));
  ASSERT_EQ(first, re.getJitCode().get());
#else
  std::vector<regex::Capture> captures;
  ASSERT_EQ(regex::MatchStatus::OK, regex::match(re, StringRef("hello world"), captures, nullptr));
  ASSERT_EQ(nullptr, re.getJitCode().get());
#endif
}

/**
 * a deep greedy loop must not grow the machine stack: the loop back edge is a tail call into the
 * `jit_goto` trampoline, which itself tail-calls the target block.
 */
TEST(RegexJitTest, deepLoopKeepsStackFlat) {
  std::string text(200000, 'a');
  auto regex = compileRegex("(a)*");
  ASSERT_TRUE(regex.hasValue());

  std::vector<regex::Capture> captures;
  const auto status = regex::match(regex.unwrap(), StringRef(text), captures, nullptr);
  ASSERT_EQ(regex::MatchStatus::OK, status);
  ASSERT_EQ(2, captures.size()); // whole match + group 1
  ASSERT_EQ(0, captures[0].offset);
  ASSERT_EQ(text.size(), captures[0].size);
}

/**
 * the JIT must reproduce the interpreter's stack limit.
 */
TEST(RegexJitTest, stackLimitMatchesInterpreter) {
  std::string text(20000000, 'a'); // enough repetitions to exhaust the backtrack stack
  auto regex = compileRegex("(a?)*");
  ASSERT_TRUE(regex.hasValue());
  auto &re = regex.unwrap();

  std::vector<regex::Capture> jitCaptures;
  const auto jitStatus = regex::match(re, StringRef(text), jitCaptures, nullptr);

  std::vector<regex::Capture> interpCaptures;
  regex::MatchStatus interpStatus;
  if (auto ctx = regex::tryToCreateMatchContext(re, StringRef(text), 0, interpCaptures)) {
    interpStatus = regex::interpret(ctx.asOk(), nullptr);
  } else {
    interpStatus = ctx.asErr();
  }
  ASSERT_EQ(interpStatus, jitStatus);
}

/**
 * error paths that are decided before the JIT is involved must be unaffected.
 */
TEST(RegexJitTest, inputErrorsArePreserved) {
  auto regex = compileRegex("a");
  ASSERT_TRUE(regex.hasValue());
  auto &re = regex.unwrap();

  std::vector<regex::Capture> captures;
  ASSERT_EQ(regex::MatchStatus::INVALID_UTF8,
            regex::match(re, StringRef("\xFF\xFF\xFF"), captures, nullptr));

  const StringRef tooLarge("ss", static_cast<size_t>(UINT32_MAX) * 2);
  ASSERT_EQ(regex::MatchStatus::INPUT_LIMIT, regex::match(re, tooLarge, captures, nullptr));
}

/**
 * a long literal prefix uses the "find needle" fast path; the captures must still be correct.
 */
TEST(RegexJitTest, needleFastPath) {
  const std::string text = std::string(1000, 'z') + "needle" + std::string(1000, 'z');
  auto regex = compileRegex("needle");
  ASSERT_TRUE(regex.hasValue());

  std::vector<regex::Capture> captures;
  ASSERT_EQ(regex::MatchStatus::OK,
            regex::match(regex.unwrap(), StringRef(text), captures, nullptr));
  ASSERT_EQ(1, captures.size());
  ASSERT_EQ(1000, captures[0].offset);
  ASSERT_EQ(6, captures[0].size);
}
