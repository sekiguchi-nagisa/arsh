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

TEST(OptParseTest, base) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
  };
  auto parser = createOptParser(options);
  const char *args[] = {
      "-h", "--help", "-a", "-", "AAA",
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
  ASSERT_STREQ("-a", *begin);

  // -a (undefined option)
  ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isError());
  ASSERT_EQ(OptParseResult<Kind>::Status::UNDEF, ret.getStatus());
  ASSERT_EQ("a", ret.getValue().toString());
  ASSERT_STREQ("-a", *begin);

  // -
  ++begin;
  parser.reset();
  ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isEnd());
  ASSERT_STREQ("-", *begin);
}

TEST(OptParseTest, arg1) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
      {Kind::B, 'v', "verbose", OptParseOp::NO_ARG, "show verbose message"},
      {Kind::C, 0, "config", OptParseOp::HAS_ARG, "set configuration"},
      {Kind::D, 'D', nullptr, OptParseOp::OPT_ARG, "set configuration"},
      {Kind::E, 'o', "output", OptParseOp::OPT_ARG, "set configuration"},
  };
  auto parser = createOptParser(options);

  //
  std::vector<std::string> args = {"-v", "-o", "stdout", "AAA"};
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
  ASSERT_TRUE(ret.hasArg());
  ASSERT_EQ("stdout", ret.getValue().toString());
  ASSERT_EQ("AAA", *begin);

  // AAA
  ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isEnd());
  ASSERT_EQ("AAA", *begin);
}

TEST(OptParseTest, arg2) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
      {Kind::B, 'v', "verbose", OptParseOp::NO_ARG, "show verbose message"},
      {Kind::C, 0, "config", OptParseOp::HAS_ARG, "set configuration"},
      {Kind::D, 'D', nullptr, OptParseOp::OPT_ARG, "set configuration"},
      {Kind::E, 'o', "output", OptParseOp::OPT_ARG, "set configuration"},
  };
  auto parser = createOptParser(options);

  //
  std::vector<std::string> args = {"--config",     "file",      "--output", "-o", "-h",
                                   "--output=CCC", "--output=", "--output", "AAA"};
  auto begin = args.begin();
  auto end = args.end();

  // --config file
  auto ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::C, ret.getOpt());
  ASSERT_TRUE(ret.hasArg());
  ASSERT_EQ("file", ret.getValue().toString());
  ASSERT_EQ("--output", *begin);

  // --output
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt());
  ASSERT_FALSE(ret.hasArg()); // not accept '-' arg if specified OPT_ARG
  ASSERT_EQ("-o", *begin);

  // -o
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt());
  ASSERT_FALSE(ret.hasArg()); // not accept '-' arg if specified OPT_ARG
  ASSERT_EQ("-h", *begin);

  // -h
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::A, ret.getOpt());
  ASSERT_FALSE(ret.hasArg()); // not accept '-' arg if specified OPT_ARG
  ASSERT_EQ("--output=CCC", *begin);

  // --output=CCC
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt());
  ASSERT_TRUE(ret.hasArg());
  ASSERT_EQ("CCC", ret.getValue().toString());
  ASSERT_EQ("--output=", *begin);

  // --output=
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt()); // allow '--output=' if specified OPT_ARG
  ASSERT_FALSE(ret.hasArg());
  ASSERT_EQ("--output", *begin);

  // --output AAA
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt());
  ASSERT_TRUE(ret.hasArg());
  ASSERT_EQ("AAA", ret.getValue().toString());
  ASSERT_TRUE(begin == end);
}

TEST(OptParseTest, arg3) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
      {Kind::B, 'v', "verbose", OptParseOp::NO_ARG, "show verbose message"},
      {Kind::C, 0, "config", OptParseOp::HAS_ARG, "set configuration"},
      {Kind::D, 'D', nullptr, OptParseOp::OPT_ARG, "set configuration"},
      {Kind::E, 'o', "output", OptParseOp::OPT_ARG, "set configuration"},
  };
  auto parser = createOptParser(options);

  //
  std::vector<std::string> args = {"--output", "-", "-o", "-", "--output", "--", "-o", "--"};
  auto begin = args.begin();
  auto end = args.end();

  // --output -
  auto ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt());
  ASSERT_TRUE(ret.hasArg()); // accept '-' even if specified OPT_ARG
  ASSERT_EQ("-", ret.getValue().toString());
  ASSERT_EQ("-o", *begin);

  // -o -
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt());
  ASSERT_TRUE(ret.hasArg()); // accept '-' even if specified OPT_ARG
  ASSERT_EQ("-", ret.getValue().toString());
  ASSERT_EQ("--output", *begin);

  // --output --
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt());
  ASSERT_FALSE(ret.hasArg());
  ASSERT_EQ("--", *begin);

  // -o --
  ++begin;
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt());
  ASSERT_FALSE(ret.hasArg());
  ASSERT_EQ("--", *begin);
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
      {Kind::D, 'D', nullptr, OptParseOp::OPT_ARG, "set configuration"},
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

  // -o 123
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt());
  ASSERT_TRUE(ret.hasArg());
  ASSERT_EQ("123", ret.getValue().toString());
  ASSERT_EQ("-ohv", *begin);
  ASSERT_EQ("", parser.getRemain().toString());

  // -o
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt());
  ASSERT_FALSE(ret.hasArg()); // only last option accept arg
  ASSERT_EQ("-ohv", *begin);
  ASSERT_EQ("hv", parser.getRemain().toString());

  // -h
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::A, ret.getOpt());
  ASSERT_FALSE(ret.hasArg());
  ASSERT_EQ("-ohv", *begin);
  ASSERT_EQ("v", parser.getRemain().toString());

  // -v
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::B, ret.getOpt());
  ASSERT_FALSE(ret.hasArg()); // only last option accept arg
  ASSERT_EQ("456", *begin);
  ASSERT_EQ("", parser.getRemain().toString());

  // 456
  ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isEnd());
  ASSERT_EQ("456", *begin);
}

TEST(OptParseTest, error1) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
      {Kind::B, 'v', "verbose", OptParseOp::NO_ARG, "show verbose message"},
      {Kind::C, 'c', "config", OptParseOp::HAS_ARG, "set configuration"},
      {Kind::D, 'D', nullptr, OptParseOp::OPT_ARG, "set configuration"},
      {Kind::E, 'o', "output", OptParseOp::OPT_ARG, "set configuration"},
  };
  auto parser = createOptParser(options);

  //
  std::vector<std::string> args = {"-vD", "-cvo", "--config", "-", "--config", "--", "--config"};
  auto begin = args.begin();
  auto end = args.end();

  // -v
  auto ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::B, ret.getOpt());
  ASSERT_FALSE(ret.hasArg());
  ASSERT_EQ("D", parser.getRemain().toString());
  ASSERT_EQ("-vD", *begin);

  // -D
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::D, ret.getOpt());
  ASSERT_FALSE(ret.hasArg());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_EQ("-cvo", *begin);

  // -c
  ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isError());
  ASSERT_EQ(Kind::C, ret.getOpt());
  ASSERT_EQ(OptParseResult<Kind>::Status::NEED_ARG, ret.getStatus());
  ASSERT_EQ("c", ret.getValue().toString());
  ASSERT_EQ("vo", parser.getRemain().toString());
  ASSERT_EQ("-cvo", *begin);

  // --config -
  ++begin;
  parser.reset();
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::C, ret.getOpt());
  ASSERT_TRUE(ret.hasArg());
  ASSERT_EQ("-", ret.getValue().toString()); // accept '-'
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_EQ("--config", *begin);

  // --config --
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::C, ret.getOpt());
  ASSERT_TRUE(ret.hasArg());
  ASSERT_EQ("--", ret.getValue().toString()); // accept '--'
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_EQ("--config", *begin);

  // --config
  ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isError());
  ASSERT_EQ(Kind::C, ret.getOpt());
  ASSERT_EQ(OptParseResult<Kind>::Status::NEED_ARG, ret.getStatus());
  ASSERT_EQ("config", ret.getValue().toString());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_TRUE(begin == end);
}

TEST(OptParseTest, error2) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
      {Kind::B, 'v', "verbose", OptParseOp::NO_ARG, "show verbose message"},
      {Kind::C, 'c', "config", OptParseOp::HAS_ARG, "set configuration"},
      {Kind::D, 'D', nullptr, OptParseOp::OPT_ARG, "set configuration"},
      {Kind::E, 'o', "output", OptParseOp::OPT_ARG, "set configuration"},
  };
  auto parser = createOptParser(options);

  //
  std::vector<std::string> args = {"--config="};
  auto begin = args.begin();
  auto end = args.end();

  auto ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isError());
  ASSERT_EQ(OptParseResult<Kind>::Status::NEED_ARG, ret.getStatus());
  ASSERT_EQ(Kind::C, ret.getOpt());
  ASSERT_EQ("config", ret.getValue().toString());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_TRUE(begin == end);
}

TEST(OptParseTest, error3) {
  OptParser<Kind>::Option options[] = {
      {Kind::A, 'h', "help", OptParseOp::NO_ARG, "show this help message"},
      {Kind::B, 'v', "verbose", OptParseOp::NO_ARG, "show verbose message"},
      {Kind::C, 'c', "config", OptParseOp::HAS_ARG, "set configuration"},
      {Kind::D, 'D', nullptr, OptParseOp::OPT_ARG, "set configuration"},
      {Kind::E, 'o', "output", OptParseOp::OPT_ARG, "set configuration"},
  };
  auto parser = createOptParser(options);

  //
  std::vector<std::string> args = {"-A", "-onfig", "--hoge", "--dump=AAA"};
  auto begin = args.begin();
  auto end = args.end();

  // -A
  auto ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isError());
  ASSERT_EQ(OptParseResult<Kind>::Status::UNDEF, ret.getStatus());
  ASSERT_EQ("A", ret.getValue().toString());
  ASSERT_EQ("A", parser.getRemain().toString());
  ASSERT_EQ("-A", *begin);

  // -o
  ++begin;
  parser.reset();
  ret = parser(begin, end);
  ASSERT_TRUE(ret);
  ASSERT_EQ(Kind::E, ret.getOpt());
  ASSERT_FALSE(ret.hasArg());
  ASSERT_EQ("nfig", parser.getRemain().toString());
  ASSERT_EQ("-onfig", *begin);

  // -n
  ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isError());
  ASSERT_EQ(OptParseResult<Kind>::Status::UNDEF, ret.getStatus());
  ASSERT_EQ("n", ret.getValue().toString());
  ASSERT_EQ("nfig", parser.getRemain().toString());
  ASSERT_EQ("-onfig", *begin);

  // --hoge
  ++begin;
  parser.reset();
  ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isError());
  ASSERT_EQ(OptParseResult<Kind>::Status::UNDEF, ret.getStatus());
  ASSERT_EQ("hoge", ret.getValue().toString());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_EQ("--hoge", *begin);

  // --dump=AAA
  ++begin;
  parser.reset();
  ret = parser(begin, end);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(ret.isError());
  ASSERT_EQ(OptParseResult<Kind>::Status::UNDEF, ret.getStatus());
  ASSERT_EQ("dump", ret.getValue().toString());
  ASSERT_EQ("", parser.getRemain().toString());
  ASSERT_EQ("--dump=AAA", *begin);
}

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
  const char *optStr = "de:ba:c";
  opt::GetOptState optState(optStr);

  auto begin = std::begin(argv);
  auto end = std::end(argv);

  int opt = optState(begin, end);
  ASSERT_EQ('a', opt);
  ASSERT_EQ(nullptr, optState.nextChar.data());
  ASSERT_EQ("2", optState.optArg);
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "hello");

  opt = optState(begin, end);
  ASSERT_EQ(-1, opt);
  ASSERT_EQ(nullptr, optState.nextChar.data());
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "hello");

  ++begin;
  opt = optState(begin, end);
  ASSERT_EQ('b', opt);
  ASSERT_EQ("cd", optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "-bcd");

  opt = optState(begin, end);
  ASSERT_EQ('c', opt);
  ASSERT_EQ("d", optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "-bcd");

  opt = optState(begin, end);
  ASSERT_EQ('d', opt);
  ASSERT_EQ("", optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "-");

  opt = optState(begin, end);
  ASSERT_EQ(-1, opt);
  ASSERT_EQ(nullptr, optState.nextChar.data());
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "-");

  ++begin;
  opt = optState(begin, end);
  ASSERT_EQ(-1, opt);
  ASSERT_EQ(nullptr, optState.nextChar.data());
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "hoge");

  ++begin;
  opt = optState(begin, end);
  ASSERT_EQ('?', opt);
  ASSERT_EQ("f", optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ('f', optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "-f");

  ++begin;
  optState.reset(optStr);
  opt = optState(begin, end);
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
  const char *optStr = ":a::b:";
  opt::GetOptState optState(optStr);

  auto begin = std::begin(argv);
  auto end = std::end(argv);

  int opt = optState(begin, end);
  ASSERT_EQ('a', opt);
  ASSERT_EQ(nullptr, optState.nextChar.data());
  ASSERT_EQ("ba", optState.optArg);
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "-a");

  opt = optState(begin, end);
  ASSERT_EQ('a', opt);
  ASSERT_EQ(nullptr, optState.nextChar.data());
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_NE(begin, end);
  ASSERT_STREQ(*begin, "hoge");

  ++begin;
  optState.reset(optStr);
  opt = optState(begin, end);
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
  opt::GetOptState optState(optStr);
  auto begin = std::begin(argv);
  auto end = std::end(argv);

  int opt = optState(begin, end);
  ASSERT_EQ('a', opt);
  ASSERT_EQ("b", optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);

  opt = optState(begin, end);
  ASSERT_EQ('b', opt);
  ASSERT_EQ(nullptr, optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);

  // normally long options are unrecognized
  opt = optState(begin, end);
  ASSERT_EQ('?', opt);
  ASSERT_EQ("--help", optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ('-', optState.optOpt);
  ASSERT_TRUE(optState.foundLongOption);

  // if remapHelp is true, remap --help to -h
  optState.reset(optStr);
  optState.remapHelp = true;
  opt = optState(begin, end);
  ASSERT_EQ('h', opt);
  ASSERT_EQ(nullptr, optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);
  ASSERT_FALSE(optState.foundLongOption);

  opt = optState(begin, end);
  ASSERT_EQ('c', opt);
  ASSERT_EQ(nullptr, optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
  ASSERT_EQ(0, optState.optOpt);

  // long options are still unrecognized except for --help
  opt = optState(begin, end);
  ASSERT_EQ('?', opt);
  ASSERT_EQ("--help1", optState.nextChar);
  ASSERT_EQ(nullptr, optState.optArg.data());
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
  opt::GetOptState optState("a:");

  auto begin = std::begin(argv);
  auto end = std::end(argv);

  int opt = optState(begin, end);
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
