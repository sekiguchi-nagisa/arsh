
#include "gtest/gtest.h"

#include <format_util.h>

using namespace arsh;

struct QuoteTest : public ::testing::Test {
  static void checkQuote(const StringRef prefix, const StringRef remain, const QuoteParam param,
                         const std::string &expected, const bool noHex = true) {
    std::string out = prefix.toString();
    bool r = quoteAsCmdOrShellArg(remain, out, param);
    ASSERT_EQ(noHex, r);
    ASSERT_EQ(expected, out);
  }

  static void checkArgQuote(const StringRef remain, const std::string &expected,
                            const bool noHex = true) {
    checkQuote("", remain, {.asCmd = false, .carryBackslash = false}, expected, noHex);
  }

  static void checkCmdQuote(const StringRef remain, const std::string &expected,
                            const bool noHex = true) {
    checkQuote("", remain, {.asCmd = true, .carryBackslash = false}, expected, noHex);
  }
};

TEST_F(QuoteTest, base) {
  ASSERT_NO_FATAL_FAILURE(checkCmdQuote("", ""));
  ASSERT_NO_FATAL_FAILURE(checkArgQuote("", ""));
  ASSERT_NO_FATAL_FAILURE(checkCmdQuote(" ", "\\ "));
  ASSERT_NO_FATAL_FAILURE(checkArgQuote(" ", "\\ "));
  ASSERT_NO_FATAL_FAILURE(checkCmdQuote("+", "\\+"));
  ASSERT_NO_FATAL_FAILURE(checkArgQuote("+", "+"));
  ASSERT_NO_FATAL_FAILURE(checkCmdQuote("-", "\\-"));
  ASSERT_NO_FATAL_FAILURE(checkArgQuote("-", "-"));
  ASSERT_NO_FATAL_FAILURE(checkCmdQuote("7z", "\\7z"));
  ASSERT_NO_FATAL_FAILURE(checkArgQuote("7z", "7z"));
  ASSERT_NO_FATAL_FAILURE(checkCmdQuote("23 \\[]", "\\23\\ \\\\\\[\\]"));
  ASSERT_NO_FATAL_FAILURE(checkArgQuote("23 \\[]", "23\\ \\\\\\[\\]"));
  ASSERT_NO_FATAL_FAILURE(checkCmdQuote(";\"'", "\\;\\\"\\'"));
  ASSERT_NO_FATAL_FAILURE(checkArgQuote(";\"'", "\\;\\\"\\'"));
  ASSERT_NO_FATAL_FAILURE(
      checkCmdQuote("!$<>?#|&22(){}", "\\!\\$\\<\\>\\?\\#\\|\\&22\\(\\)\\{\\}"));
  ASSERT_NO_FATAL_FAILURE(
      checkArgQuote("!$<>?#|&22(){}", "\\!\\$\\<\\>\\?\\#\\|\\&22\\(\\)\\{\\}"));
  ASSERT_NO_FATAL_FAILURE(checkCmdQuote("ls\n12", "ls$'\\x0a'12", false));
  ASSERT_NO_FATAL_FAILURE(checkArgQuote("ls\n12", "ls$'\\x0a'12", false));
  ASSERT_NO_FATAL_FAILURE(checkCmdQuote("*\xFF\xFF", "\\*$'\\xff'$'\\xff'", false));
  ASSERT_NO_FATAL_FAILURE(checkArgQuote("*\xFF\xFF", "\\*$'\\xff'$'\\xff'", false));

  // backslash carry
  ASSERT_NO_FATAL_FAILURE(
      checkQuote("--ll\\", "vm ", {.asCmd = false, .carryBackslash = false}, "--ll\\vm\\ "));
  ASSERT_NO_FATAL_FAILURE(
      checkQuote("--ll\\", "vm ", {.asCmd = false, .carryBackslash = true}, "--ll\\vm\\ "));
  ASSERT_NO_FATAL_FAILURE(
      checkQuote(R"(--\ll\)", " @", {.asCmd = false, .carryBackslash = false}, R"(--\ll\\ @)"));
  ASSERT_NO_FATAL_FAILURE(
      checkQuote(R"(--\ll\)", " @", {.asCmd = false, .carryBackslash = true}, R"(--\ll\ @)"));
  ASSERT_NO_FATAL_FAILURE(
      checkQuote(R"(--out=\)", "7z", {.asCmd = true, .carryBackslash = false}, R"(--out=\\7z)"));
  ASSERT_NO_FATAL_FAILURE(
      checkQuote(R"(--out=\)", "7z", {.asCmd = true, .carryBackslash = true}, R"(--out=\7z)"));
  ASSERT_NO_FATAL_FAILURE(
      checkQuote(R"(--out=\)", " 23", {.asCmd = false, .carryBackslash = false}, R"(--out=\\ 23)"));
  ASSERT_NO_FATAL_FAILURE(
      checkQuote(R"(--out=\)", " 23", {.asCmd = false, .carryBackslash = true}, R"(--out=\ 23)"));
  ASSERT_NO_FATAL_FAILURE(
      checkQuote(R"(--out=\)", "\\@", {.asCmd = false, .carryBackslash = false}, R"(--out=\\\@)"));
  ASSERT_NO_FATAL_FAILURE(
      checkQuote(R"(--out=\)", "\\@", {.asCmd = false, .carryBackslash = true}, R"(--out=\\@)"));
  ASSERT_NO_FATAL_FAILURE(checkQuote(R"(-o\)", "\n@", {.asCmd = false, .carryBackslash = false},
                                     R"(-o\$'\x0a'@)", false));
  ASSERT_NO_FATAL_FAILURE(checkQuote(R"(-o\)", "\n@", {.asCmd = false, .carryBackslash = true},
                                     R"(-o\$'\x0a'@)", false));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}