#include "gtest/gtest.h"

#include "../test_common.h"
#include "factory.h"

#ifndef BIN_PATH
#define "require BIN_PATH"
#endif

#ifndef HIGHLIGHTER_PATH
#define "require HIGHLIGHTER_PATH"
#endif

using namespace ydsh;
using namespace highlighter;

class TokenCollector : public TokenEmitter {
private:
  std::vector<std::pair<HighlightTokenClass, std::string>> tokens;

public:
  explicit TokenCollector(StringRef ref) : TokenEmitter(ref) {}

  auto take() && { return std::move(this->tokens); }

private:
  void emit(HighlightTokenClass tokenClass, Token token) override {
    this->tokens.emplace_back(tokenClass, this->source.substr(token.pos, token.size).toString());
  }
};

static auto lex(StringRef ref) {
  std::string content = ref.toString();
  if (content.empty() || content.back() != '\n') {
    content += '\n';
  }
  TokenCollector collector(content);
  tokenizeAndEmit(collector, "dummy");
  return std::move(collector).take();
}

struct EmitterTest : public ::testing::Test {
  static void compare(HighlightTokenClass expectClass, const char *expect,
                      const std::pair<HighlightTokenClass, std::string> &pair) {
    ASSERT_EQ(expectClass, pair.first);
    ASSERT_EQ(expect, pair.second);
  }
};

TEST_F(EmitterTest, case1) {
  auto ret = lex("echo hello$@[0] 1>&2 # this is a comment");
  ASSERT_EQ(8, ret.size());
  compare(HighlightTokenClass::COMMAND, "echo", ret[0]);
  compare(HighlightTokenClass::COMMAND_ARG, "hello", ret[1]);
  compare(HighlightTokenClass::VARIABLE, "$@", ret[2]);
  compare(HighlightTokenClass::NONE, "[", ret[3]);
  compare(HighlightTokenClass::NUMBER, "0", ret[4]);
  compare(HighlightTokenClass::NONE, "]", ret[5]);
  compare(HighlightTokenClass::REDIRECT, "1>&2", ret[6]);
  compare(HighlightTokenClass::COMMENT, "# this is a comment", ret[7]);

  ret = lex("var a = 3.4");
  ASSERT_EQ(4, ret.size());
  compare(HighlightTokenClass::KEYWORD, "var", ret[0]);
  compare(HighlightTokenClass::NONE, "a", ret[1]);
  compare(HighlightTokenClass::OPERATOR, "=", ret[2]);
  compare(HighlightTokenClass::NUMBER, "3.4", ret[3]);

  ret = lex("assert $/a+/i =~ 'aAa'");
  ASSERT_EQ(4, ret.size());
  compare(HighlightTokenClass::KEYWORD, "assert", ret[0]);
  compare(HighlightTokenClass::REGEX, "$/a+/i", ret[1]);
  compare(HighlightTokenClass::OPERATOR, "=~", ret[2]);
  compare(HighlightTokenClass::STRING, "'aAa'", ret[3]);

  ret = lex("assert %'int' is Signal");
  ASSERT_EQ(4, ret.size());
  compare(HighlightTokenClass::KEYWORD, "assert", ret[0]);
  compare(HighlightTokenClass::SIGNAL, "%'int'", ret[1]);
  compare(HighlightTokenClass::OPERATOR, "is", ret[2]);
  compare(HighlightTokenClass::TYPE, "Signal", ret[3]);

  ret = lex("@($f(!$false))");
  ASSERT_EQ(7, ret.size());
  compare(HighlightTokenClass::NONE, "@(", ret[0]);
  compare(HighlightTokenClass::VARIABLE, "$f", ret[1]);
  compare(HighlightTokenClass::NONE, "(", ret[2]);
  compare(HighlightTokenClass::OPERATOR, "!", ret[3]);
  compare(HighlightTokenClass::VARIABLE, "$false", ret[4]);
  compare(HighlightTokenClass::NONE, ")", ret[5]);
  compare(HighlightTokenClass::NONE, ")", ret[6]);

  ret = lex("coproc ls *");
  ASSERT_EQ(3, ret.size());
  compare(HighlightTokenClass::KEYWORD, "coproc", ret[0]);
  compare(HighlightTokenClass::COMMAND, "ls", ret[1]);
  compare(HighlightTokenClass::COMMAND_ARG, "*", ret[2]);
}

TEST_F(EmitterTest, case2) {
  TokenCollector collector("hello");
  collector(TokenKind::COMMAND, Token{.pos = 100, .size = 12});
  auto values = std::move(collector).take();
  ASSERT_TRUE(values.empty());
}

struct HighlightTest : public ::testing::Test {
  static void tokenize(FormatterFactory &factory, StringRef ref, std::ostream &output) {
    factory.setSource(ref);

    auto ret = factory.create(output);
    ASSERT_TRUE(ret);
    auto formatter = std::move(ret).take();
    ASSERT_TRUE(formatter);
    tokenizeAndEmit(*formatter, "<dummy>");
    formatter->finalize();
  }
};

TEST_F(HighlightTest, validate) {
  static_assert(ValidRule(""));
  static_assert(ValidRule("          "));
  static_assert(ValidRule("  bold  italic  underline bg:"));
  static_assert(ValidRule("#123456  nobold border:#000000 noitalic  nounderline  bg:#fbd"));
}

TEST_F(HighlightTest, color) {
  auto color = Color::parse("");
  ASSERT_FALSE(color);

  color = Color::parse("aaa");
  ASSERT_FALSE(color);
  ASSERT_EQ("<invalid>", color.toString());

  color = Color::parse("#abcd");
  ASSERT_FALSE(color);

  color = Color::parse("#fffffl");
  ASSERT_FALSE(color);

  color = Color::parse("#fffffff");
  ASSERT_FALSE(color);

  color = Color::parse("#abc");
  ASSERT_TRUE(color);
  ASSERT_EQ(0xaa, color.red);
  ASSERT_EQ(0xbb, color.green);
  ASSERT_EQ(0xcc, color.blue);
  ASSERT_EQ("#aabbcc", color.toString());

  color = Color::parse("#aabbcd");
  ASSERT_TRUE(color);
  ASSERT_EQ(0xaa, color.red);
  ASSERT_EQ(0xbb, color.green);
  ASSERT_EQ(0xcd, color.blue);
  ASSERT_EQ("#aabbcd", color.toString());

  color = Color::parse("#ffffff");
  ASSERT_TRUE(color);
  ASSERT_EQ(0xff, color.red);
  ASSERT_EQ(0xff, color.green);
  ASSERT_EQ(0xff, color.blue);
  ASSERT_EQ("#ffffff", color.toString());
}

TEST_F(HighlightTest, closestColor) {
  IndexedColorPalette256 colorPalette;
  for (unsigned int i = 0; i < 256; i++) {
    ASSERT_TRUE(colorPalette[i]);
  }
  ASSERT_EQ("#000000", colorPalette[0].toString());
  ASSERT_EQ("#d78700", colorPalette[172].toString());
  ASSERT_EQ("#0087d7", colorPalette[32].toString());

  auto color = Color::parse("#c0c0c0");
  ASSERT_TRUE(color);
  unsigned int index = colorPalette.findClosest(color);
  auto closest = colorPalette[index];
  ASSERT_TRUE(closest);
  ASSERT_EQ(color.toString(), closest.toString());

  color = Color::parse("#d78700");
  ASSERT_TRUE(color);
  index = colorPalette.findClosest(color);
  ASSERT_EQ(172, index);
  closest = colorPalette[index];
  ASSERT_TRUE(closest);
  ASSERT_EQ("#d78700", closest.toString());

  color = Color::parse("#e06c75");
  ASSERT_TRUE(color);
  index = colorPalette.findClosest(color);
  closest = colorPalette[index];
  ASSERT_TRUE(closest);
  ASSERT_EQ("#d75f87", closest.toString());
}

TEST_F(HighlightTest, rule) {
  constexpr auto base = ValidRule("bold italic underline bg:#fbd border:#ffffff");
  constexpr auto derived = ValidRule("nobold noitalic nounderline bg: #123456 border: ");

  auto ret = StyleRule().synthesize(base);
  ASSERT_FALSE(ret.text);
  ASSERT_TRUE(ret.bold);
  ASSERT_TRUE(ret.italic);
  ASSERT_TRUE(ret.underline);
  ASSERT_TRUE(ret.background);
  ASSERT_EQ(0xFF, ret.background.red);
  ASSERT_EQ(0xbb, ret.background.green);
  ASSERT_EQ(0xdd, ret.background.blue);
  ASSERT_TRUE(ret.border);
  ASSERT_EQ(0xFF, ret.border.red);
  ASSERT_EQ(0xFF, ret.border.green);
  ASSERT_EQ(0xFF, ret.border.blue);

  ret = ret.synthesize(derived);
  ASSERT_TRUE(ret.text);
  ASSERT_EQ(0x12, ret.text.red);
  ASSERT_EQ(0x34, ret.text.green);
  ASSERT_EQ(0x56, ret.text.blue);
  ASSERT_FALSE(ret.bold);
  ASSERT_FALSE(ret.italic);
  ASSERT_FALSE(ret.underline);
  ASSERT_FALSE(ret.background);
  ASSERT_FALSE(ret.border);
}

TEST_F(HighlightTest, style) {
  StyleMap styleMap;

  auto *style = styleMap.find("darcula");
  ASSERT_TRUE(style);
  ASSERT_STREQ("darcula", style->getName());
  ASSERT_TRUE(style->find(HighlightTokenClass::KEYWORD));

  style = styleMap.find("null");
  ASSERT_TRUE(style);
  ASSERT_STREQ("null", style->getName());
  ASSERT_FALSE(style->find(HighlightTokenClass::KEYWORD));

  style = styleMap.find("algol");
  ASSERT_TRUE(style);
  ASSERT_STREQ("algol", style->getName());
  ASSERT_TRUE(style->find(HighlightTokenClass::KEYWORD));

  style = styleMap.find("monokai");
  ASSERT_TRUE(style);
  ASSERT_STREQ("monokai", style->getName());
  ASSERT_TRUE(style->find(HighlightTokenClass::KEYWORD));

  style = styleMap.find("colorful");
  ASSERT_TRUE(style);
  ASSERT_STREQ("colorful", style->getName());
  ASSERT_TRUE(style->find(HighlightTokenClass::KEYWORD));

  style = styleMap.find("not found ");
  ASSERT_FALSE(style);
}

TEST_F(HighlightTest, factory) {
  StyleMap styleMap;
  FormatterFactory factory(styleMap);
  factory.setFormatName("fjriejfoie");
  auto ret = factory.create(std::cerr);
  ASSERT_FALSE(ret);
  ASSERT_EQ("unsupported formatter: fjriejfoie", ret.asErr());

  factory = FormatterFactory(styleMap);
  factory.setStyleName("freafref");
  ret = factory.create(std::cerr);
  ASSERT_FALSE(ret);
  ASSERT_EQ("unsupported style: freafref", ret.asErr());
}

TEST_F(HighlightTest, nullFormatter) {
  std::stringstream stream;
  std::string content = R"(#!/usr/bin/env ydsh
  function sum($a : Int) : Int for Int {
    return $this + $a
  }

  123456.sum($@.size() + $#)  # this is a comment

)";

  StyleMap styleMap;
  FormatterFactory factory(styleMap);
  factory.setFormatName("null");

  ASSERT_NO_FATAL_FAILURE(this->tokenize(factory, content, stream));

  ASSERT_EQ(content, stream.str());
}

TEST_F(HighlightTest, ansiFormatter1) {
  std::stringstream stream;
  std::string content = R"(#!/usr/bin/env ydsh
# this is a comment
)";

  StyleMap styleMap;
  FormatterFactory factory(styleMap);
  factory.setFormatName("ansi");
  factory.setStyleName("darcula");

  ASSERT_NO_FATAL_FAILURE(tokenize(factory, content, stream));

  const char *expected = "\033[38;2;128;128;128m#!/usr/bin/env ydsh\033[0m\n"
                         "\033[38;2;128;128;128m# this is a comment\033[0m\n";
  ASSERT_EQ(expected, stream.str());
}

TEST_F(HighlightTest, ansiFormatter2) {
  std::stringstream stream;
  std::string content = R"(
#!/usr/bin/env ydsh
assert $OSTYPE == 'Linux'
)";

  StyleMap styleMap;
  FormatterFactory factory(styleMap);
  factory.setFormatName("ansi");
  factory.setStyleName("algol");

  ASSERT_NO_FATAL_FAILURE(tokenize(factory, content, stream));

  const char *expected =
      "\n"
      "\033[38;2;136;136;136m\033[3m#!/usr/bin/env ydsh\033[0m\n"
      "\033[1m\033[4massert\033[0m \033[38;2;102;102;102m\033[1m\033[3m$OSTYPE\033[0m == "
      "\033[38;2;102;102;102m\033[3m'Linux'\033[0m\n";
  ASSERT_EQ(expected, stream.str());
}

TEST_F(HighlightTest, ansiFormatter3) {
  std::stringstream stream;
  std::string content = R"(
'hello
world'

)";

  StyleMap styleMap;
  FormatterFactory factory(styleMap);
  factory.setFormatName("ansi");
  factory.setStyleName("colorful");

  ASSERT_NO_FATAL_FAILURE(tokenize(factory, content, stream));

  const char *expected = "\n"
                         "\033[38;2;187;187;187m\033[48;2;255;240;240m'hello\033[0m\n"
                         "\033[38;2;187;187;187m\033[48;2;255;240;240mworld'\033[0m\n\n";
  ASSERT_EQ(expected, stream.str());
}

TEST_F(HighlightTest, htmlFormatter1) {
  std::stringstream stream;
  std::string content = R"(
  'hello<>&
"world'

)";
  StyleMap styleMap;
  FormatterFactory factory(styleMap);
  factory.setStyleName("colorful");
  factory.setFormatName("html");

  ASSERT_NO_FATAL_FAILURE(tokenize(factory, content, stream));
  const char *expected = R"EOF(<pre style="tab-size:4">
<code>
  <span style="color:#bbbbbb;background-color:#fff0f0">&#39;hello&lt;&gt;&amp;</span>
<span style="color:#bbbbbb;background-color:#fff0f0">&quot;world&#39;</span>

</code></pre>)EOF";
  ASSERT_EQ(expected, stream.str());
}

TEST_F(HighlightTest, htmlFormatter2) {
  std::stringstream stream;
  std::string content = R"(
#!/usr/bin/env ydsh
assert $OSTYPE == 'Linux'
)";

  StyleMap styleMap;
  FormatterFactory factory(styleMap);
  factory.setFormatName("html");
  factory.setStyleName("algol");

  ASSERT_NO_FATAL_FAILURE(tokenize(factory, content, stream));

  const char *expected = R"EOF(<pre style="tab-size:4">
<code>
<span style="color:#888888;font-style:italic">#!/usr/bin/env ydsh</span>
<span style="font-weight:bold;text-decoration:underline">assert</span> <span style="color:#666666;font-weight:bold;font-style:italic">$OSTYPE</span> == <span style="color:#666666;font-style:italic">&#39;Linux&#39;</span>
</code></pre>)EOF";
  ASSERT_EQ(expected, stream.str());
}

class ColorizeTest : public ExpectOutput {
public:
  using ExpectOutput::expect;
  using ExpectOutput::expectRegex;

  template <typename... T>
  static ProcBuilder ds(T &&...args) {
    return ProcBuilder{BIN_PATH, std::forward<T>(args)...};
  }
};

TEST_F(ColorizeTest, list) {
  const char *out = R"(Styles:
* algol
* colorful
* darcula
* monokai
* null

Formatters:
* empty nil null
  - output text without any formatting
* ansi console term terminal
  - format tokens with ANSI color codes (for true-color terminal)
* console256 term256 terminal256
  - format tokens with ANSI color codes (for 256-color terminal)
* html
  - format tokens as HTML codes
)";
  ProcBuilder builder = {HIGHLIGHTER_PATH, "-l"};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 0, out));
}

TEST_F(ColorizeTest, help) {
  auto out = format(R"(usage: %s [option ...] [source file]
Options:
    --html-full    generate self-contained html (for html formatter)
    -f arg         specify output formatter (default is `ansi' formatter)
    -h             show help message
    -l             show supported formatters/styles
    -o arg         specify output file (default is stdout)
    -s arg         specify highlighter color style (default is `darcula' style)
)",
                    HIGHLIGHTER_PATH);
  ProcBuilder builder = {HIGHLIGHTER_PATH, "-h"};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 0, out));
}

TEST_F(ColorizeTest, invalid1) {
  const char *out = R"(invalid option: -q
Options:
    --html-full    generate self-contained html (for html formatter)
    -f arg         specify output formatter (default is `ansi' formatter)
    -h             show help message
    -l             show supported formatters/styles
    -o arg         specify output file (default is stdout)
    -s arg         specify highlighter color style (default is `darcula' style)
)";
  ProcBuilder builder = {HIGHLIGHTER_PATH, "-q"};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 1, "", out));
}

TEST_F(ColorizeTest, invalid2) {
  const char *out = R"(need argument: -o
Options:
    --html-full    generate self-contained html (for html formatter)
    -f arg         specify output formatter (default is `ansi' formatter)
    -h             show help message
    -l             show supported formatters/styles
    -o arg         specify output file (default is stdout)
    -s arg         specify highlighter color style (default is `darcula' style)
)";
  ProcBuilder builder = {HIGHLIGHTER_PATH, "-o"};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 1, "", out));
}

TEST_F(ColorizeTest, cli1) {
  auto source = format(R"EOF(
  var colorize = @(%s)[0]

  assert "$(echo '1234' | exec $colorize)" == $'\033[38;2;104;151;187m1234\033[0m'
  assert "$(echo '1234' | exec $colorize -f console)" == $'\033[38;2;104;151;187m1234\033[0m'
  assert "$(echo '1234' | exec $colorize -s darcula)" == $'\033[38;2;104;151;187m1234\033[0m'
  assert "$(echo -n '1234' | exec $colorize -s null)" == $'1234'
  assert "$(echo '1234' | exec $colorize -s null
                                         -o /dev/stderr /dev/stdin 2>&1 > /dev/null)" == $'1234'
  assert "$(echo "'a'" | exec $colorize -s colorful -f term256)" ==
                                         $'\033[38;5;7m\033[48;5;253m\'a\'\033[0m'

  assert exec $colorize fhauerfhai 2>&1 | grep 'cannot open file' > /dev/null
  assert exec $colorize . 2>&1 | grep 'cannot read file' > /dev/null
  assert exec $colorize -o . 2>&1 | grep 'cannot open file' > /dev/null

  assert "$(echo 1234 | exec $colorize -s higjaior 2>&1)" == 'unsupported style: higjaior'
  assert "$(echo 1234 | exec $colorize -f higjaior 2>&1)" == 'unsupported formatter: higjaior'

  true
)EOF",
                       HIGHLIGHTER_PATH);
  ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", source.c_str()), 0));
}

TEST_F(ColorizeTest, cli2) {
  auto source = format(R"EOF(
  var colorize = @(%s)[0]

  assert echo 1234 | exec $colorize -f html | grep -v '<html>' > /dev/null
  assert echo 1234 | exec $colorize -f html | grep -v '</html>' > /dev/null
  assert echo 1234 | exec $colorize -f html --html-full | grep '<html>' > /dev/null
  assert echo 1234 | exec $colorize -f html --html-full | grep '</html>' > /dev/null

)EOF",
                       HIGHLIGHTER_PATH);
  ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", source.c_str()), 0));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}