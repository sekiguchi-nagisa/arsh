#include "gtest/gtest.h"

#include <misc/opt.hpp>
#include <misc/opt_parser.hpp>

using namespace ydsh;

enum class Kind : unsigned int {
  A,
  B,
  C,
  D,
  E,
};

TEST(OptParseTest, base1) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
  };
  auto parser = createOptParser(options);
  const char *args[] = {
      "-h",
      "--help",
      "-",
      "AAA",
  };
  auto begin = std::begin(args);
  auto end = std::end(args);

  // -h
  auto ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(OptParseResult<Kind>::Status::OK, ret.getStatus());
  ASSERT_EQ(Kind::A, ret.getOpt());
  ASSERT_FALSE(ret.hasArg());
  ASSERT_STREQ("--help", *begin);

  // --help
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(OptParseResult<Kind>::Status::OK, ret.getStatus());
  ASSERT_EQ(Kind::A, ret.getOpt());
  ASSERT_FALSE(ret.hasArg());
  ASSERT_STREQ("-", *begin);

  // -
  ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isEnd());
  ASSERT_STREQ("-", *begin);
}

TEST(OptParseTest, base2) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
  };
  auto parser = createOptParser(options);
  const char *args[] = {
      "-h", "--", "-a", "-", "AAA",
  };
  auto begin = std::begin(args);
  auto end = std::end(args);

  // -h
  auto ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(OptParseResult<Kind>::Status::OK, ret.getStatus());
  ASSERT_EQ(Kind::A, ret.getOpt());
  ASSERT_FALSE(ret.hasArg());
  ASSERT_STREQ("--", *begin);

  // --
  ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isEnd());
  ASSERT_EQ("-a", *begin);
}

TEST(OptParseTest, shortOpt1) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
      {Kind::B, 'v', "verbose", OptParseOp::NO_ARG, "show verbose message"},
      {Kind::C, 'c', "config", OptParseOp::HAS_ARG, "set configuration"},
      {Kind::D, 'D', "", OptParseOp::OPT_ARG, "set configuration"},
      {Kind::E, 'o', "output", OptParseOp::OPT_ARG, "set configuration"},
  };
  auto parser = createOptParser(options);

  //
  std::vector<std::string> args = {"-v", "-o", "-voBBB", "AAA"};
  auto begin = args.begin();
  auto end = args.end();

  // -v
  auto ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::B, ret.getOpt());
  ASSERT_EQ("-o", *begin);

  // -o
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt());
  ASSERT_FALSE(ret.hasArg());
  ASSERT_EQ("-voBBB", *begin);

  // -voBBB
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::B, ret.getOpt());
  ASSERT_EQ("oBBB", parser.getRemain().toString());
  ASSERT_EQ("-voBBB", *begin);

  // -oBBB
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt());
  ASSERT_TRUE(ret.hasArg());
  ASSERT_EQ("BBB", ret.getValue().toString());
  ASSERT_EQ("AAA", *begin);

  // AAA
  ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isEnd());
  ASSERT_EQ("AAA", *begin);
}

TEST(OptParseTest, shortOpt2) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
      {Kind::B, 'v', "verbose", OptParseOp::NO_ARG, "show verbose message"},
      {Kind::C, 'c', "config", OptParseOp::HAS_ARG, "set configuration"},
      {Kind::D, 'D', "", OptParseOp::OPT_ARG, "set configuration"},
      {Kind::E, 'o', "output", OptParseOp::OPT_ARG, "set configuration"},
  };
  auto parser = createOptParser(options);

  //
  std::vector<std::string> args = {"-ccoBBB", "111", "222", "-c", "--"};
  auto begin = args.begin();
  auto end = args.end();

  // -c coBBB
  auto ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::C, ret.getOpt());
  ASSERT_TRUE(ret.hasArg());
  ASSERT_EQ("coBBB", ret.getValue().toString());
  ASSERT_EQ("111", *begin);

  // -c --
  ++begin; // 222
  ++begin; // -c
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::C, ret.getOpt());
  ASSERT_TRUE(ret.hasArg());
  ASSERT_EQ("--", ret.getValue().toString());
  ASSERT_TRUE(begin == end);
}

TEST(OptParseTest, longOpt1) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
      {Kind::B, 'v', "verbose", OptParseOp::NO_ARG, "show verbose message"},
      {Kind::C, 0, "config", OptParseOp::HAS_ARG, "set configuration"},
      {Kind::D, 'D', "", OptParseOp::OPT_ARG, "set configuration"},
      {Kind::E, 'o', "output", OptParseOp::OPT_ARG, "set configuration"},
  };
  auto parser = createOptParser(options);

  //
  std::vector<std::string> args = {"--config", "file", "--verbose", "--config=CCC",
                                   "--config", "--",   "--config="};
  auto begin = args.begin();
  auto end = args.end();

  // --config file
  auto ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::C, ret.getOpt());
  ASSERT_TRUE(ret.hasArg());
  ASSERT_EQ("file", ret.getValue().toString());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_EQ("--verbose", *begin);

  // --verbose
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::B, ret.getOpt());
  ASSERT_FALSE(ret.hasArg());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_EQ("--config=CCC", *begin);

  // --config=CCC
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::C, ret.getOpt());
  ASSERT_TRUE(ret.hasArg());
  ASSERT_EQ("CCC", ret.getValue().toString());
  ASSERT_EQ("", parser.getRemain().toString());

  // --config --
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::C, ret.getOpt());
  ASSERT_TRUE(ret.hasArg());
  ASSERT_EQ("--", ret.getValue().toString());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_EQ("--config=", *begin);

  // --config=
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::C, ret.getOpt());
  ASSERT_TRUE(ret.hasArg());
  ASSERT_EQ("", ret.getValue().toString());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_TRUE(begin == end);
}

TEST(OptParseTest, longOpt2) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
      {Kind::B, 'v', "verbose", OptParseOp::NO_ARG, "show verbose message"},
      {Kind::C, 0, "config", OptParseOp::HAS_ARG, "set configuration"},
      {Kind::D, 'D', "", OptParseOp::OPT_ARG, "set configuration"},
      {Kind::E, 'o', "output", OptParseOp::OPT_ARG, "set configuration"},
  };
  auto parser = createOptParser(options);

  //
  std::vector<std::string> args = {"--output=BBB", "--output=", "--output", "BBB", "--output"};
  auto begin = args.begin();
  auto end = args.end();

  // --output=BBB
  auto ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt());
  ASSERT_TRUE(ret.hasArg());
  ASSERT_EQ("BBB", ret.getValue().toString());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_EQ("--output=", *begin);

  // --output=
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt());
  ASSERT_TRUE(ret.hasArg());
  ASSERT_EQ("", ret.getValue().toString());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_EQ("--output", *begin);

  // --output
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt());
  ASSERT_FALSE(ret.hasArg());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_EQ("BBB", *begin);

  // BBB
  ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isEnd());
  ASSERT_EQ("BBB", *begin);
  ++begin;

  // --output
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt());
  ASSERT_FALSE(ret.hasArg());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_TRUE(begin == end);
}

TEST(OptParseTest, commonPrefix) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 0, "dump", OptParseOp::OPT_ARG, "show this help message"},
      {Kind::B, 0, "dump-state", OptParseOp::NO_ARG, "show this help message"},
  };
  auto parser = createOptParser(options);

  //
  std::vector<std::string> args = {"--dump-state"};
  auto begin = args.begin();
  auto end = args.end();

  auto ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::B, ret.getOpt());
  ASSERT_FALSE(ret.hasArg());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_TRUE(begin == end);
}

TEST(OptParseTest, multiArg) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
      {Kind::B, 'v', "verbose", OptParseOp::NO_ARG, "show verbose message"},
      {Kind::C, 0, "config", OptParseOp::HAS_ARG, "set configuration"},
      {Kind::D, 'D', "", OptParseOp::OPT_ARG, "set configuration"},
      {Kind::E, 'o', "output", OptParseOp::OPT_ARG, "set configuration"},
  };
  auto parser = createOptParser(options);

  //
  std::vector<std::string> args = {"-hvo", "123", "-ohv", "456"};
  auto begin = args.begin();
  auto end = args.end();

  // -h
  auto ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::A, ret.getOpt());
  ASSERT_FALSE(ret.hasArg());
  ASSERT_EQ("-hvo", *begin);
  ASSERT_EQ("vo", parser.getRemain().toString());

  // -v
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::B, ret.getOpt());
  ASSERT_FALSE(ret.hasArg());
  ASSERT_EQ("-hvo", *begin);
  ASSERT_EQ("o", parser.getRemain().toString());

  // -o
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt());
  ASSERT_FALSE(ret.hasArg());
  ASSERT_EQ("123", *begin);
  ASSERT_EQ("", parser.getRemain().toString());

  // 123
  ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ++begin;
  ASSERT_EQ("-ohv", *begin);

  // -o hv
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt());
  ASSERT_TRUE(ret.hasArg());
  ASSERT_EQ("hv", ret.getValue().toString());
  ASSERT_EQ("456", *begin);
  ASSERT_EQ("", parser.getRemain().toString());

  // 456
  ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isEnd());
  ASSERT_EQ("456", *begin);
}

TEST(OptParseTest, invalidShortOpt) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
      {Kind::B, 'v', "verbose", OptParseOp::NO_ARG, "show verbose message"},
      {Kind::C, 'c', "config", OptParseOp::HAS_ARG, "set configuration"},
      {Kind::D, 'D', "", OptParseOp::OPT_ARG, "set configuration"},
      {Kind::E, 'o', "output", OptParseOp::OPT_ARG, "set configuration"},
  };
  auto parser = createOptParser(options);

  //
  std::vector<std::string> args = {"-X", "-vA", "-Qvo"};

  auto begin = args.begin();
  auto end = args.end();

  // -X
  auto ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isError());
  ASSERT_EQ(OptParseResult<Kind>::Status::UNDEF, ret.getStatus());
  ASSERT_EQ("X", ret.getValue().toString());
  ASSERT_EQ("X", parser.getRemain().toString());
  ASSERT_EQ("-X", *begin);

  // -v
  parser.reset();
  ++begin;
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::B, ret.getOpt());
  ASSERT_FALSE(ret.hasArg());
  ASSERT_EQ("A", parser.getRemain().toString());
  ASSERT_EQ("-vA", *begin);

  // -A
  ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isError());
  ASSERT_EQ(OptParseResult<Kind>::Status::UNDEF, ret.getStatus());
  ASSERT_EQ("A", ret.getValue().toString());
  ASSERT_EQ("A", parser.getRemain().toString());
  ASSERT_EQ("-vA", *begin);

  // -Q
  parser.reset();
  ++begin;
  ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isError());
  ASSERT_EQ(OptParseResult<Kind>::Status::UNDEF, ret.getStatus());
  ASSERT_EQ("Q", ret.getValue().toString());
  ASSERT_EQ("Qvo", parser.getRemain().toString());
  ASSERT_EQ("-Qvo", *begin);
}

TEST(OptParseTest, invalidLongOpt) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
      {Kind::B, 'v', "verbose", OptParseOp::NO_ARG, "show verbose message"},
      {Kind::C, 'c', "config", OptParseOp::HAS_ARG, "set configuration"},
      {Kind::D, 'D', "", OptParseOp::OPT_ARG, "set configuration"},
      {Kind::E, 'o', "output", OptParseOp::OPT_ARG, "set configuration"},
  };
  auto parser = createOptParser(options);

  //
  std::vector<std::string> args = {"--in", "--AAA="};

  auto begin = args.begin();
  auto end = args.end();

  // --in
  auto ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isError());
  ASSERT_EQ(OptParseResult<Kind>::Status::UNDEF, ret.getStatus());
  ASSERT_EQ("in", ret.getValue().toString());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_EQ("--in", *begin);

  // --AAA=
  ++begin;
  parser.reset();
  ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isError());
  ASSERT_EQ(OptParseResult<Kind>::Status::UNDEF, ret.getStatus());
  ASSERT_EQ("AAA", ret.getValue().toString());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_EQ("--AAA=", *begin);
}

TEST(OptParseTest, needArgShortOpt1) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
      {Kind::B, 'v', "verbose", OptParseOp::NO_ARG, "show verbose message"},
      {Kind::C, 'c', "config", OptParseOp::HAS_ARG, "set configuration"},
      {Kind::D, 'D', "", OptParseOp::OPT_ARG, "set configuration"},
      {Kind::E, 'o', "output", OptParseOp::OPT_ARG, "set configuration"},
  };
  auto parser = createOptParser(options);

  //
  std::vector<std::string> args = {"-c", "-o", "-c"};

  auto begin = args.begin();
  auto end = args.end();

  // -c -o
  auto ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::C, ret.getOpt());
  ASSERT_TRUE(ret.hasArg());
  ASSERT_EQ("-o", ret.getValue().toString());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_EQ("-c", *begin);

  // -c
  ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isError());
  ASSERT_EQ(OptParseResult<Kind>::Status::NEED_ARG, ret.getStatus());
  ASSERT_EQ(Kind::C, ret.getOpt());
  ASSERT_EQ("c", ret.getValue().toString());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_TRUE(begin == end);
}

TEST(OptParseTest, needArgShortOpt2) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
      {Kind::B, 'v', "verbose", OptParseOp::NO_ARG, "show verbose message"},
      {Kind::C, 'c', "config", OptParseOp::HAS_ARG, "set configuration"},
      {Kind::D, 'D', "", OptParseOp::OPT_ARG, "set configuration"},
      {Kind::E, 'o', "output", OptParseOp::OPT_ARG, "set configuration"},
  };
  auto parser = createOptParser(options);

  //
  std::vector<std::string> args = {"-cc", "-o"};

  auto begin = args.begin();
  auto end = args.end();

  // -c c
  auto ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::C, ret.getOpt());
  ASSERT_TRUE(ret.hasArg());
  ASSERT_EQ("c", ret.getValue().toString());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_EQ("-o", *begin);

  // -o
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt());
  ASSERT_EQ("", ret.getValue().toString());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_TRUE(begin == end);
}

TEST(OptParseTest, needArgLongOpt) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
      {Kind::B, 'v', "verbose", OptParseOp::NO_ARG, "show verbose message"},
      {Kind::C, 'c', "config", OptParseOp::HAS_ARG, "set configuration"},
      {Kind::D, 'D', "", OptParseOp::OPT_ARG, "set configuration"},
      {Kind::E, 'o', "output", OptParseOp::OPT_ARG, "set configuration"},
  };
  auto parser = createOptParser(options);

  //
  std::vector<std::string> args = {"--config"};

  auto begin = args.begin();
  auto end = args.end();

  // --config
  auto ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isError());
  ASSERT_EQ(OptParseResult<Kind>::Status::NEED_ARG, ret.getStatus());
  ASSERT_EQ(Kind::C, ret.getOpt());
  ASSERT_EQ("config", ret.getValue().toString());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_TRUE(begin == end);
}

TEST(OptParseTest, options1) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
      {Kind::A, 'H', "", OptParseOp::NO_ARG, "show this help message"},
      {Kind::B, 'v', "verbose", OptParseOp::NO_ARG, "show verbose message"},
      {Kind::C, 'c', "config", OptParseOp::HAS_ARG, "set configuration"},
      {Kind::D, 'D', "", OptParseOp::OPT_ARG, "set configuration"},
      {Kind::E, 'o', "output", OptParseOp::OPT_ARG, "set configuration"},
  };
  auto parser = createOptParser(options);
  auto ret = parser.formatOptions();
  const char *expect = R"(Options:
  -h, --help               show this help message
  -H                       show this help message
  -v, --verbose            show verbose message
  -c, --config arg         set configuration
  -D[arg]                  set configuration
  -o[arg], --output[=arg]  set configuration)";
  ASSERT_EQ(expect, ret);
}

TEST(GetOptTest, base) {
  const char *argv[] = {
      "-a", "2", "hello", "-bcd", "-", "--", "hoge", "-f", "-e",
  };
  const char *optStr = "de:ba:c";
  opt::GetOptState optState(optStr);

  auto begin = std::begin(argv);
  auto end = std::end(argv);

  int opt = optState(begin, end);
  ASSERT_EQ('a', opt);
  ASSERT_EQ("", optState.nextChar.toString());
  ASSERT_EQ("2", optState.optArg.toString());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "hello");

  opt = optState(begin, end);
  ASSERT_EQ(-1, opt);
  ASSERT_EQ("", optState.nextChar.toString());
  ASSERT_EQ("", optState.optArg.toString());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "hello");

  ++begin;
  opt = optState(begin, end);
  ASSERT_EQ('b', opt);
  ASSERT_EQ("cd", optState.nextChar.toString());
  ASSERT_EQ("", optState.optArg.toString());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "-bcd");

  opt = optState(begin, end);
  ASSERT_EQ('c', opt);
  ASSERT_EQ("d", optState.nextChar.toString());
  ASSERT_EQ("", optState.optArg.toString());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "-bcd");

  opt = optState(begin, end);
  ASSERT_EQ('d', opt);
  ASSERT_EQ("", optState.nextChar.toString());
  ASSERT_EQ("", optState.optArg.toString());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "-");

  opt = optState(begin, end);
  ASSERT_EQ(-1, opt);
  ASSERT_EQ("", optState.nextChar.toString());
  ASSERT_EQ("", optState.optArg.toString());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "-");

  ++begin;
  opt = optState(begin, end);
  ASSERT_EQ(-1, opt);
  ASSERT_EQ("", optState.nextChar.toString());
  ASSERT_EQ("", optState.optArg.toString());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "hoge");

  ++begin;
  opt = optState(begin, end);
  ASSERT_EQ('?', opt);
  ASSERT_EQ("f", optState.nextChar.toString());
  ASSERT_EQ("", optState.optArg.toString());
  ASSERT_EQ('f', optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "-f");

  ++begin;
  optState.reset(optStr);
  opt = optState(begin, end);
  ASSERT_EQ('?', opt);
  ASSERT_EQ("", optState.nextChar.toString());
  ASSERT_EQ("", optState.optArg.toString());
  ASSERT_EQ('e', optState.optOpt);
  ASSERT_EQ(begin, end);
}

TEST(GetOptTest, opt) {
  const char *argv[] = {
      "-aba",
      "-a",
      "hoge",
      "-bAAA",
  };
  const char *optStr = ":a::b:";
  opt::GetOptState optState(optStr);

  auto begin = std::begin(argv);
  auto end = std::end(argv);

  int opt = optState(begin, end);
  ASSERT_EQ('a', opt);
  ASSERT_EQ("", optState.nextChar.toString());
  ASSERT_EQ("ba", optState.optArg.toString());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "-a");

  opt = optState(begin, end);
  ASSERT_EQ('a', opt);
  ASSERT_EQ("", optState.nextChar.toString());
  ASSERT_EQ("", optState.optArg.toString());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "hoge");

  ++begin;
  optState.reset(optStr);
  opt = optState(begin, end);
  ASSERT_EQ('b', opt);
  ASSERT_EQ("", optState.nextChar.toString());
  ASSERT_EQ("AAA", optState.optArg.toString());
  ASSERT_EQ(0, optState.optOpt);
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
  opt::GetOptState optState(optStr);
  auto begin = std::begin(argv);
  auto end = std::end(argv);

  int opt = optState(begin, end);
  ASSERT_EQ('a', opt);
  ASSERT_EQ("b", optState.nextChar.toString());
  ASSERT_EQ("", optState.optArg.toString());
  ASSERT_EQ(0, optState.optOpt);

  opt = optState(begin, end);
  ASSERT_EQ('b', opt);
  ASSERT_EQ("", optState.nextChar.toString());
  ASSERT_EQ("", optState.optArg.toString());
  ASSERT_EQ(0, optState.optOpt);

  // normally long options are unrecognized
  opt = optState(begin, end);
  ASSERT_EQ('?', opt);
  ASSERT_EQ("--help", optState.nextChar.toString());
  ASSERT_EQ("", optState.optArg.toString());
  ASSERT_EQ('-', optState.optOpt);
  ASSERT_TRUE(optState.foundLongOption);

  // if remapHelp is true, remap --help to -h
  optState.reset(optStr);
  optState.remapHelp = true;
  opt = optState(begin, end);
  ASSERT_EQ('h', opt);
  ASSERT_EQ("", optState.nextChar.toString());
  ASSERT_EQ("", optState.optArg.toString());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_FALSE(optState.foundLongOption);

  opt = optState(begin, end);
  ASSERT_EQ('c', opt);
  ASSERT_EQ("", optState.nextChar.toString());
  ASSERT_EQ("", optState.optArg.toString());
  ASSERT_EQ(0, optState.optOpt);

  // long options are still unrecognized except for --help
  opt = optState(begin, end);
  ASSERT_EQ('?', opt);
  ASSERT_EQ("--help1", optState.nextChar.toString());
  ASSERT_EQ("", optState.optArg.toString());
  ASSERT_EQ('-', optState.optOpt);
  ASSERT_TRUE(optState.foundLongOption);
}

TEST(GetOptTest, invalid) {
  const char *argv[] = {
      "-:",
  };
  opt::GetOptState optState(":a::b:");

  auto begin = std::begin(argv);
  auto end = std::end(argv);

  int opt = optState(begin, end);
  ASSERT_EQ('?', opt);
  ASSERT_EQ(":", optState.nextChar.toString());
  ASSERT_EQ("", optState.optArg.toString());
  ASSERT_EQ(':', optState.optOpt);
  ++begin;
  ASSERT_EQ(begin, end);
}

TEST(GetOptTest, empty) {
  const char *argv[] = {
      "-a",
      "",
  };
  opt::GetOptState optState("a:");

  auto begin = std::begin(argv);
  auto end = std::end(argv);

  int opt = optState(begin, end);
  ASSERT_EQ('a', opt);
  ASSERT_EQ("", optState.nextChar.toString());
  ASSERT_EQ("", optState.optArg.toString());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_EQ(begin, end);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
