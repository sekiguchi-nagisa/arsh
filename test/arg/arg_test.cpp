#include "gtest/gtest.h"

#include <misc/opt.hpp>

using namespace ydsh;

enum class Kind : unsigned int {
  A,
  B,
  C,
  D,
  E,
};

TEST(ArgsTest, base1) {
  opt::Parser<Kind> parser = {
      {Kind::A, "--a", opt::NO_ARG, "hogehoge"},
      {Kind::B, "--out", opt::HAS_ARG, "hogehoge"},
      {Kind::C, "--dump", opt::OPT_ARG, "hogehoge"},
  };

  const char *args[] = {"--a", "-",         "--out",  "hello", "world",
                        "--",  "--dump=!!", "--dump", "123",   "--out"};

  auto begin = std::begin(args);
  auto end = std::end(args);

  opt::Result<Kind> result;
  ASSERT_FALSE(result);
  ASSERT_STREQ(nullptr, result.recog());
  ASSERT_STREQ(nullptr, result.arg());
  ASSERT_EQ(opt::END, result.error());

  result = parser(begin, end);
  ASSERT_TRUE(result);
  ASSERT_STREQ("--a", result.recog());
  ASSERT_STREQ(nullptr, result.arg());
  ASSERT_EQ(Kind::A, result.value());
  ASSERT_STREQ("-", *begin);

  result = parser(begin, end);
  ASSERT_FALSE(result);
  ASSERT_STREQ("-", result.recog());
  ASSERT_STREQ(nullptr, result.arg());
  ASSERT_EQ(opt::END, result.error());
  ASSERT_STREQ("-", *begin);
  ++begin;

  result = parser(begin, end);
  ASSERT_TRUE(result);
  ASSERT_STREQ("--out", result.recog());
  ASSERT_STREQ("hello", result.arg());
  ASSERT_EQ(Kind::B, result.value());
  ASSERT_STREQ("world", *begin);

  result = parser(begin, end);
  ASSERT_FALSE(result);
  ASSERT_STREQ("world", result.recog());
  ASSERT_STREQ(nullptr, result.arg());
  ASSERT_EQ(opt::END, result.error());
  ASSERT_STREQ("world", *begin);
  ++begin;

  result = parser(begin, end);
  ASSERT_FALSE(result);
  ASSERT_STREQ("--", result.recog());
  ASSERT_STREQ(nullptr, result.arg());
  ASSERT_EQ(opt::END, result.error());
  ASSERT_STREQ("--dump=!!", *begin);

  result = parser(begin, end);
  ASSERT_TRUE(result);
  ASSERT_STREQ("--dump=!!", result.recog());
  ASSERT_STREQ("!!", result.arg());
  ASSERT_EQ(Kind::C, result.value());
  ASSERT_STREQ("--dump", *begin);

  result = parser(begin, end);
  ASSERT_TRUE(result);
  ASSERT_STREQ("--dump", result.recog());
  ASSERT_STREQ(nullptr, result.arg());
  ASSERT_EQ(Kind::C, result.value());
  ASSERT_STREQ("123", *begin);
  ++begin;

  result = parser(begin, end);
  ASSERT_FALSE(result);
  ASSERT_STREQ("--out", result.recog());
  ASSERT_STREQ(nullptr, result.arg());
  ASSERT_EQ(opt::NEED_ARG, result.error());
  ASSERT_STREQ("--out", *begin);
  ++begin;

  result = parser(begin, end);
  ASSERT_FALSE(result);
  ASSERT_STREQ(nullptr, result.recog());
  ASSERT_STREQ(nullptr, result.arg());
  ASSERT_EQ(opt::END, result.error());
  ASSERT_EQ(end, begin);
}

TEST(ArgsTest, base2) {
  opt::Parser<Kind> parser = {
      {Kind::C, "--dump", opt::OPT_ARG, "hogehoge"},
      {Kind::D, "--dump2", opt::NO_ARG, "hogehoge"},
  };

  const char *args[] = {"--dump2", "--dump=34"};

  auto begin = std::begin(args);
  auto end = std::end(args);

  opt::Result<Kind> result;
  ASSERT_FALSE(result);
  ASSERT_STREQ(nullptr, result.recog());
  ASSERT_STREQ(nullptr, result.arg());
  ASSERT_EQ(opt::END, result.error());

  result = parser(begin, end);
  ASSERT_TRUE(result);
  ASSERT_STREQ("--dump2", result.recog());
  ASSERT_STREQ(nullptr, result.arg());
  ASSERT_EQ(Kind::D, result.value());
  ASSERT_STREQ("--dump=34", *begin);

  result = parser(begin, end);
  ASSERT_TRUE(result);
  ASSERT_STREQ("--dump=34", result.recog());
  ASSERT_STREQ("34", result.arg());
  ASSERT_EQ(Kind::C, result.value());
}

TEST(GetOptTest, base) {
  const char *argv[] = {
      "-a", "2", "hello", "-bcd", "-", "--", "hoge", "-f", "-e",
  };
  const char *optstr = "de:ba:c";
  opt::GetOptState optState;

  auto begin = std::begin(argv);
  auto end = std::end(argv);

  int opt = optState(begin, end, optstr);
  ASSERT_EQ('a', opt);
  ASSERT_EQ(nullptr, optState.nextChar.data());
  ASSERT_EQ("2", optState.optArg);
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "hello");

  opt = optState(begin, end, optstr);
  ASSERT_EQ(-1, opt);
  ASSERT_EQ(nullptr, optState.nextChar.data());
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "hello");

  ++begin;
  opt = optState(begin, end, optstr);
  ASSERT_EQ('b', opt);
  ASSERT_EQ("cd", optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "-bcd");

  opt = optState(begin, end, optstr);
  ASSERT_EQ('c', opt);
  ASSERT_EQ("d", optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "-bcd");

  opt = optState(begin, end, optstr);
  ASSERT_EQ('d', opt);
  ASSERT_EQ("", optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "-");

  opt = optState(begin, end, optstr);
  ASSERT_EQ(-1, opt);
  ASSERT_EQ(nullptr, optState.nextChar.data());
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "-");

  ++begin;
  opt = optState(begin, end, optstr);
  ASSERT_EQ(-1, opt);
  ASSERT_EQ(nullptr, optState.nextChar.data());
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "hoge");

  ++begin;
  opt = optState(begin, end, optstr);
  ASSERT_EQ('?', opt);
  ASSERT_EQ("f", optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ('f', optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "-f");

  ++begin;
  optState.reset();
  opt = optState(begin, end, optstr);
  ASSERT_EQ('?', opt);
  ASSERT_EQ("", optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ('e', optState.optOpt);
  ASSERT_EQ(begin, end);
}

TEST(GetOptTest, opt) {
  const char *argv[] = {
      "-aba",
      "-a",
      "hoge",
      "-b",
  };
  const char *optstr = ":a::b:";
  opt::GetOptState optState;

  auto begin = std::begin(argv);
  auto end = std::end(argv);

  int opt = optState(begin, end, optstr);
  ASSERT_EQ('a', opt);
  ASSERT_EQ(nullptr, optState.nextChar.data());
  ASSERT_EQ("ba", optState.optArg);
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "-a");

  opt = optState(begin, end, optstr);
  ASSERT_EQ('a', opt);
  ASSERT_EQ(nullptr, optState.nextChar.data());
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "hoge");

  ++begin;
  optState.reset();
  opt = optState(begin, end, optstr);
  ASSERT_EQ(':', opt);
  ASSERT_EQ("", optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ('b', optState.optOpt);
  ASSERT_EQ(begin, end);
}

TEST(GetOptTest, help) {
  const char *argv[] = {
      "-ab",
      "--help",
      "-c",
      "--help1",
  };
  const char *optStr = "abch";
  opt::GetOptState optState;
  auto begin = std::begin(argv);
  auto end = std::end(argv);

  int opt = optState(begin, end, optStr);
  ASSERT_EQ('a', opt);
  ASSERT_EQ("b", optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);

  opt = optState(begin, end, optStr);
  ASSERT_EQ('b', opt);
  ASSERT_EQ(nullptr, optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);

  // normally long options are unrecognized
  opt = optState(begin, end, optStr);
  ASSERT_EQ('?', opt);
  ASSERT_EQ("-help", optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ('-', optState.optOpt);

  // if remapHelp is true, remap --help to -h
  optState.reset();
  optState.remapHelp = true;
  opt = optState(begin, end, optStr);
  ASSERT_EQ('h', opt);
  ASSERT_EQ(nullptr, optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);

  opt = optState(begin, end, optStr);
  ASSERT_EQ('c', opt);
  ASSERT_EQ(nullptr, optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);

  // long options are still unrecognized except for --help
  opt = optState(begin, end, optStr);
  ASSERT_EQ('?', opt);
  ASSERT_EQ("-help1", optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ('-', optState.optOpt);
}

TEST(GetOptTest, invalid) {
  const char *argv[] = {
      "-:",
  };
  const char *optstr = ":a::b:";
  opt::GetOptState optState;

  auto begin = std::begin(argv);
  auto end = std::end(argv);

  int opt = optState(begin, end, optstr);
  ASSERT_EQ('?', opt);
  ASSERT_EQ(":", optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(':', optState.optOpt);
  ++begin;
  ASSERT_EQ(begin, end);
}

TEST(GetOptTest, empty) {
  const char *argv[] = {
      "-a",
      "",
  };
  const char *optstr = "a:";
  opt::GetOptState optState;

  auto begin = std::begin(argv);
  auto end = std::end(argv);

  int opt = optState(begin, end, optstr);
  ASSERT_EQ('a', opt);
  ASSERT_EQ(nullptr, optState.nextChar.data());
  ASSERT_EQ("", optState.optArg);
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_EQ(begin, end);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
