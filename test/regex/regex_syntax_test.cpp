
#include "../test_common.h"

#include <regex/dump.h>
#include <regex/flag.hpp>
#include <regex/parser.h>

using namespace arsh;

TEST(RegexFlag, base) {
  auto flag = regex::Flag::parse("", nullptr); // default
  ASSERT_TRUE(flag.hasValue());
  ASSERT_TRUE(flag.unwrap().is(regex::Mode::UNICODE));

  flag = regex::Flag::parse("uims", nullptr);
  ASSERT_TRUE(flag.hasValue());
  ASSERT_TRUE(flag.unwrap().is(regex::Mode::UNICODE));
  ASSERT_TRUE(flag.unwrap().has(regex::Modifier::IGNORE_CASE | regex::Modifier::DOT_ALL |
                                regex::Modifier::MULTILINE));

  flag = regex::Flag::parse("vimissvsssssmmm", nullptr);
  ASSERT_TRUE(flag.hasValue());
  ASSERT_TRUE(flag.unwrap().is(regex::Mode::UNICODE_SET));
  ASSERT_TRUE(flag.unwrap().has(regex::Modifier::IGNORE_CASE | regex::Modifier::DOT_ALL |
                                regex::Modifier::MULTILINE));

  std::string err;
  flag = regex::Flag::parse("ymq", &err);
  ASSERT_FALSE(flag.hasValue());
  ASSERT_EQ("invalid regex flag: `y'", err);

  err.clear();
  flag = regex::Flag::parse("mmmgu", &err);
  ASSERT_FALSE(flag.hasValue());
  ASSERT_EQ("invalid regex flag: `g'", err);

  err.clear();
  flag = regex::Flag::parse("mummv", &err);
  ASSERT_FALSE(flag.hasValue());
  ASSERT_EQ("cannot specify `v' flag, since `u' flag has already been specified", err);

  err.clear();
  flag = regex::Flag::parse("vvviu", &err);
  ASSERT_FALSE(flag.hasValue());
  ASSERT_EQ("cannot specify `u' flag, since `v' flag has already been specified", err);
}

struct SyntaxTreeTestEntry {
  const char *name;
  const char *pattern;
  const char *modifiers;

  struct Expect {
    Token token{};
    bool error{false};
    const char *str;

    constexpr Expect(const char *str) : str(str) {} // NOLINT

    constexpr Expect(Token token, const char *str) : token(token), error(true), str(str) {}

    explicit operator bool() const { return !this->error; }
  };
  Expect expect;
};

std::ostream &operator<<(std::ostream &stream, const SyntaxTreeTestEntry &entry) {
  return stream << entry.name << ": /" << entry.pattern << "/" << entry.modifiers;
}

static std::string trim(const char *str) {
  StringRef ref = str;
  while (ref.startsWith("\n")) {
    ref.removePrefix(1);
  }
  return ref.toString();
}

struct SyntaxTreeTest : public ::testing::TestWithParam<SyntaxTreeTestEntry> {
  static void doTest() {
    auto &p = GetParam();
    std::string err;
    auto flag = regex::Flag::parse(p.modifiers, regex::Mode::LEGACY, &err);
    SCOPED_TRACE(format("name: %s, pattern: %s, modifiers: %s", p.name, p.pattern, p.modifiers));
    ASSERT_EQ("", err);
    ASSERT_TRUE(flag.hasValue());
    regex::Parser parser;
    auto tree = parser(p.pattern, flag.unwrap());
    if (p.expect) {
      if (parser.hasError()) {
        auto &error = *parser.getError();
        fprintf(stderr, "[error] %s, at %s\n", error.message.c_str(), error.token.str().c_str());
        ASSERT_FALSE(parser.hasError());
      }
      regex::TreeDumper dumper;
      auto actual = dumper(tree);
      auto expect = trim(p.expect.str);
      ASSERT_EQ(expect, actual);
    } else {
      ASSERT_TRUE(parser.hasError());
      ASSERT_EQ(p.expect.token.str(), parser.getError()->token.str());
      ASSERT_EQ(p.expect.str, parser.getError()->message);
    }
  }
};

TEST_P(SyntaxTreeTest, base) { ASSERT_NO_FATAL_FAILURE(doTest()); }

static constexpr SyntaxTreeTestEntry syntaxBaseCases[] = {
    {"any", ".", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Any
  token: (pos = 0, size = 1)
)"},
    {"any-escape", ".\\.", "mv", R"(
flag: (mode = v, modifier = m)
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 3)
  patterns:
    - kind: Any
      token: (pos = 0, size = 1)
    - kind: Char
      token: (pos = 1, size = 2)
      codePoint: U+002E, .
)"},
    {"empty", "", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Empty
  token: (pos = 0, size = 0)
)"},
    {"char1", "c", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 1)
  codePoint: U+0063, c
)"},
    {"char2", "あc", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 4)
  patterns:
    - kind: Char
      token: (pos = 0, size = 3)
      codePoint: U+3042, あ
    - kind: Char
      token: (pos = 3, size = 1)
      codePoint: U+0063, c
)"},

    // ^
    {"start1", "^", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Boundary
  token: (pos = 0, size = 1)
  boundary: START
)"},
    {"start2", "^", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Boundary
  token: (pos = 0, size = 1)
  boundary: START
)"},
    {"start3", "^", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Boundary
  token: (pos = 0, size = 1)
  boundary: START
)"},

    // $
    {"end1", "$", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Boundary
  token: (pos = 0, size = 1)
  boundary: END
)"},
    {"end2", "$", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Boundary
  token: (pos = 0, size = 1)
  boundary: END
)"},
    {"end3", "$", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Boundary
  token: (pos = 0, size = 1)
  boundary: END
)"},
};
INSTANTIATE_TEST_SUITE_P(base, SyntaxTreeTest, ::testing::ValuesIn(syntaxBaseCases));

static constexpr SyntaxTreeTestEntry syntaxAtomEscape1Cases[] = {
    /* \\ */
    {"last-bs1", "\\", "", {{0, 1}, "\\ at end of pattern"}},
    {"last-bs2", "2\\", "u", {{1, 1}, "\\ at end of pattern"}},
    {"last-bs3", "32\\", "v", {{2, 1}, "\\ at end of pattern"}},

    {"esc-bs1", "\\\\", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 2)
  codePoint: U+005C, \
)"},
    {"esc-bs2", "\\\\", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 2)
  codePoint: U+005C, \
)"},
    {"esc-bs3", "\\\\", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 2)
  codePoint: U+005C, \
)"},

    // \f
    {"esc-f1", "\\f", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 2)
  codePoint: U+000C, \x0C
)"},
    {"esc-f2", "\\f", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 2)
  codePoint: U+000C, \x0C
)"},
    {"esc-f3", "\\f", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 2)
  codePoint: U+000C, \x0C
)"},
    // \n
    {"esc-n1", "\\n", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 2)
  codePoint: U+000A, \x0A
)"},
    {"esc-n2", "\\n", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 2)
  codePoint: U+000A, \x0A
)"},
    {"esc-n3", "\\n", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 2)
  codePoint: U+000A, \x0A
)"},

    // \r
    {"esc-r1", "\\r", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 2)
  codePoint: U+000D, \x0D
)"},
    {"esc-r2", "\\r", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 2)
  codePoint: U+000D, \x0D
)"},
    {"esc-r3", "\\r", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 2)
  codePoint: U+000D, \x0D
)"},

    // \t
    {"esc-t1", "\\t", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 2)
  codePoint: U+0009, \x09
)"},
    {"esc-t2", "\\t", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 2)
  codePoint: U+0009, \x09
)"},
    {"esc-t3", "\\t", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 2)
  codePoint: U+0009, \x09
)"},

    // \v
    {"esc-v1", "\\v", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 2)
  codePoint: U+000B, \x0B
)"},
    {"esc-v2", "\\v", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 2)
  codePoint: U+000B, \x0B
)"},
    {"esc-v3", "\\v", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 2)
  codePoint: U+000B, \x0B
)"},
};
INSTANTIATE_TEST_SUITE_P(atomEscape1, SyntaxTreeTest, ::testing::ValuesIn(syntaxAtomEscape1Cases));

static constexpr SyntaxTreeTestEntry syntaxAtomEscape2Cases[] = {
    // \b
    {"esc-b1", "\\b", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Boundary
  token: (pos = 0, size = 2)
  boundary: WORD
)"},
    {"esc-b2", "\\b", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Boundary
  token: (pos = 0, size = 2)
  boundary: WORD
)"},
    {"esc-b3", "\\b", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Boundary
  token: (pos = 0, size = 2)
  boundary: WORD
)"},

    // \B
    {"esc-B1", "\\B", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Boundary
  token: (pos = 0, size = 2)
  boundary: NOT_WORD
)"},
    {"esc-B2", "\\B", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Boundary
  token: (pos = 0, size = 2)
  boundary: NOT_WORD
)"},
    {"esc-B3", "\\B", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Boundary
  token: (pos = 0, size = 2)
  boundary: NOT_WORD
)"},
    // \d
    {"esc-d1", "\\d", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 2)
  property: DIGIT
  value: 0
  invert: false
)"},
    {"esc-d2", "\\d", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 2)
  property: DIGIT
  value: 0
  invert: false
)"},
    {"esc-d3", "\\d", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 2)
  property: DIGIT
  value: 0
  invert: false
)"},
    // \D
    {"esc-D1", "\\D", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 2)
  property: NOT_DIGIT
  value: 0
  invert: true
)"},
    {"esc-D2", "\\D", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 2)
  property: NOT_DIGIT
  value: 0
  invert: true
)"},
    {"esc-D3", "\\D", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 2)
  property: NOT_DIGIT
  value: 0
  invert: true
)"},

    // \s
    {"esc-s1", "\\s", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 2)
  property: SPACE
  value: 0
  invert: false
)"},
    {"esc-s2", "\\s", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 2)
  property: SPACE
  value: 0
  invert: false
)"},
    {"esc-s3", "\\s", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 2)
  property: SPACE
  value: 0
  invert: false
)"},

    // \S
    {"esc-S1", "\\S", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 2)
  property: NOT_SPACE
  value: 0
  invert: true
)"},
    {"esc-S2", "\\S", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 2)
  property: NOT_SPACE
  value: 0
  invert: true
)"},
    {"esc-S3", "\\S", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 2)
  property: NOT_SPACE
  value: 0
  invert: true
)"},

    // \w
    {"esc-w1", "\\w", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 2)
  property: WORD
  value: 0
  invert: false
)"},
    {"esc-w2", "\\w", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 2)
  property: WORD
  value: 0
  invert: false
)"},
    {"esc-w3", "\\w", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 2)
  property: WORD
  value: 0
  invert: false
)"},

    // \W
    {"esc-W1", "\\W", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 2)
  property: NOT_WORD
  value: 0
  invert: true
)"},
    {"esc-W2", "\\W", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 2)
  property: NOT_WORD
  value: 0
  invert: true
)"},
    {"esc-W3", "\\W", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 2)
  property: NOT_WORD
  value: 0
  invert: true
)"},
};
INSTANTIATE_TEST_SUITE_P(atomEscape2, SyntaxTreeTest, ::testing::ValuesIn(syntaxAtomEscape2Cases));

static constexpr SyntaxTreeTestEntry syntaxAtomEscape3Cases[] = {
    // \c
    {"escape-c1", "\\c", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 2)
  patterns:
    - kind: Char
      token: (pos = 0, size = 1)
      codePoint: U+005C, \
    - kind: Char
      token: (pos = 1, size = 1)
      codePoint: U+0063, c
)"},
    {"escape-c2", "\\c", "u", {{0, 2}, "invalid unicode escape: `\\c'"}},
    {"escape-c3", "\\c", "v", {{0, 2}, "invalid unicode escape: `\\c'"}},

    {"escape-c@1", " \\c@", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 4)
  patterns:
    - kind: Char
      token: (pos = 0, size = 1)
      codePoint: U+0020, \x20
    - kind: Char
      token: (pos = 1, size = 1)
      codePoint: U+005C, \
    - kind: Char
      token: (pos = 2, size = 1)
      codePoint: U+0063, c
    - kind: Char
      token: (pos = 3, size = 1)
      codePoint: U+0040, @
)"},
    {"escape-c@2", " \\c@", "u", {{1, 3}, "invalid unicode escape: `\\c@'"}},
    {"escape-c@3", " \\c@", "v", {{1, 3}, "invalid unicode escape: `\\c@'"}},

    {"escape-cあ1", "\\cあ", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 5)
  patterns:
    - kind: Char
      token: (pos = 0, size = 1)
      codePoint: U+005C, \
    - kind: Char
      token: (pos = 1, size = 1)
      codePoint: U+0063, c
    - kind: Char
      token: (pos = 2, size = 3)
      codePoint: U+3042, あ
)"},
    {"escape-cあ2", " \\cあ", "u", {{1, 5}, "invalid unicode escape: `\\cあ'"}},
    {"escape-cあ3", " \\cあ", "v", {{1, 5}, "invalid unicode escape: `\\cあ'"}},

    {"escape-cj1", "\\cj", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 3)
  codePoint: U+000A, \x0A
)"},
    {"escape-cj2", "\\cj", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 3)
  codePoint: U+000A, \x0A
)"},
    {"escape-cJ1", "\\cJ", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 3)
  codePoint: U+000A, \x0A
)"},
    {"escape-cJ2", "\\cJ", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 3)
  codePoint: U+000A, \x0A
)"},

    // \x
    {"escape-x1", "a\\x", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 3)
  patterns:
    - kind: Char
      token: (pos = 0, size = 1)
      codePoint: U+0061, a
    - kind: Char
      token: (pos = 1, size = 2)
      codePoint: U+0078, x
)"},
    {"escape-x2", "a\\x", "u", {{1, 2}, "invalid unicode escape: `\\x'"}},
    {"escape-x3", "a\\x", "v", {{1, 2}, "invalid unicode escape: `\\x'"}},
    {"escape-xZ1", "\\xZ", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 3)
  patterns:
    - kind: Char
      token: (pos = 0, size = 2)
      codePoint: U+0078, x
    - kind: Char
      token: (pos = 2, size = 1)
      codePoint: U+005A, Z
)"},
    {"escape-xZ2", "\\xZ", "u", {{0, 3}, "invalid unicode escape: `\\xZ'"}},
    {"escape-xZ3", "\\xZ", "v", {{0, 3}, "invalid unicode escape: `\\xZ'"}},

    {"escape-xAZ1", "\\xAZ", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 4)
  patterns:
    - kind: Char
      token: (pos = 0, size = 2)
      codePoint: U+0078, x
    - kind: Char
      token: (pos = 2, size = 1)
      codePoint: U+0041, A
    - kind: Char
      token: (pos = 3, size = 1)
      codePoint: U+005A, Z
)"},
    {"escape-xAZ2", "aa\\xAZ", "u", {{2, 4}, "invalid unicode escape: `\\xAZ'"}},
    {"escape-xAZ3", "aa\\xAZ", "v", {{2, 4}, "invalid unicode escape: `\\xAZ'"}},

    {"escape-xFFF1", "\\xFFF", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 5)
  patterns:
    - kind: Char
      token: (pos = 0, size = 4)
      codePoint: U+00FF, ÿ
    - kind: Char
      token: (pos = 4, size = 1)
      codePoint: U+0046, F
)"},
    {"escape-xFFF2", "\\xFFF", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 5)
  patterns:
    - kind: Char
      token: (pos = 0, size = 4)
      codePoint: U+00FF, ÿ
    - kind: Char
      token: (pos = 4, size = 1)
      codePoint: U+0046, F
)"},
    {"escape-xFFF3", "\\xFFF", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 5)
  patterns:
    - kind: Char
      token: (pos = 0, size = 4)
      codePoint: U+00FF, ÿ
    - kind: Char
      token: (pos = 4, size = 1)
      codePoint: U+0046, F
)"},
};
INSTANTIATE_TEST_SUITE_P(AtomEscape3, SyntaxTreeTest, ::testing::ValuesIn(syntaxAtomEscape3Cases));

static constexpr SyntaxTreeTestEntry syntaxUnicodeEscapeCases[] = {
    {"escape-u1", "aa\\u", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 4)
  patterns:
    - kind: Char
      token: (pos = 0, size = 1)
      codePoint: U+0061, a
    - kind: Char
      token: (pos = 1, size = 1)
      codePoint: U+0061, a
    - kind: Char
      token: (pos = 2, size = 2)
      codePoint: U+0075, u
)"},
    {"escape-u2", "aa\\u", "u", {{2, 2}, "invalid unicode escape: `\\u'"}},
    {"escape-u3", "aa\\u", "v", {{2, 2}, "invalid unicode escape: `\\u'"}},

    {"escape-u11", "aa\\u1", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 5)
  patterns:
    - kind: Char
      token: (pos = 0, size = 1)
      codePoint: U+0061, a
    - kind: Char
      token: (pos = 1, size = 1)
      codePoint: U+0061, a
    - kind: Char
      token: (pos = 2, size = 2)
      codePoint: U+0075, u
    - kind: Char
      token: (pos = 4, size = 1)
      codePoint: U+0031, 1
)"},
    {"escape-u12", "aa\\u1", "u", {{2, 3}, "invalid unicode escape: `\\u1'"}},
    {"escape-u13", "aa\\u1", "v", {{2, 3}, "invalid unicode escape: `\\u1'"}},

    {"escape-uFFF1", " \\uFFF", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 6)
  patterns:
    - kind: Char
      token: (pos = 0, size = 1)
      codePoint: U+0020, \x20
    - kind: Char
      token: (pos = 1, size = 2)
      codePoint: U+0075, u
    - kind: Char
      token: (pos = 3, size = 1)
      codePoint: U+0046, F
    - kind: Char
      token: (pos = 4, size = 1)
      codePoint: U+0046, F
    - kind: Char
      token: (pos = 5, size = 1)
      codePoint: U+0046, F
)"},
    {"escape-uFFF2", " \\uFFF", "u", {{1, 5}, "invalid unicode escape: `\\uFFF'"}},
    {"escape-uFFF3", " \\uFFF", "v", {{1, 5}, "invalid unicode escape: `\\uFFF'"}},

    {"escape-uFFFZ1", "\\uFFFZ", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 6)
  patterns:
    - kind: Char
      token: (pos = 0, size = 2)
      codePoint: U+0075, u
    - kind: Char
      token: (pos = 2, size = 1)
      codePoint: U+0046, F
    - kind: Char
      token: (pos = 3, size = 1)
      codePoint: U+0046, F
    - kind: Char
      token: (pos = 4, size = 1)
      codePoint: U+0046, F
    - kind: Char
      token: (pos = 5, size = 1)
      codePoint: U+005A, Z
)"},
    {"escape-uFFFZ2", "\\uFFFZ", "u", {{0, 6}, "invalid unicode escape: `\\uFFFZ'"}},
    {"escape-uFFFZ3", "\\uFFFZ", "v", {{0, 6}, "invalid unicode escape: `\\uFFFZ'"}},

    {"escape-u11111-1", "\\u11111", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 7)
  patterns:
    - kind: Char
      token: (pos = 0, size = 6)
      codePoint: U+1111, ᄑ
    - kind: Char
      token: (pos = 6, size = 1)
      codePoint: U+0031, 1
)"},
    {"escape-u11111-2", "\\u11111", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 7)
  patterns:
    - kind: Char
      token: (pos = 0, size = 6)
      codePoint: U+1111, ᄑ
    - kind: Char
      token: (pos = 6, size = 1)
      codePoint: U+0031, 1
)"},
    {"escape-u11111-3", "\\u11111", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 7)
  patterns:
    - kind: Char
      token: (pos = 0, size = 6)
      codePoint: U+1111, ᄑ
    - kind: Char
      token: (pos = 6, size = 1)
      codePoint: U+0031, 1
)"},

    {"escape-uD888-1",
     "\\uD888",
     "",
     {{0, 6}, "unicode escape generate invalid UTF-8 (surrogate): `U+D888'"}},
    {"escape-uD888-2",
     "\\uD888",
     "u",
     {{0, 6}, "unicode escape generate invalid UTF-8 (surrogate): `U+D888'"}},
    {"escape-uD888-3",
     "\\uD888",
     "v",
     {{0, 6}, "unicode escape generate invalid UTF-8 (surrogate): `U+D888'"}},
};
INSTANTIATE_TEST_SUITE_P(UnicodeEscape, SyntaxTreeTest,
                         ::testing::ValuesIn(syntaxUnicodeEscapeCases));

static constexpr SyntaxTreeTestEntry syntaxUnicodeEscape2Cases[] = {
    {"escape-u{-1", "\\u{", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 3)
  patterns:
    - kind: Char
      token: (pos = 0, size = 2)
      codePoint: U+0075, u
    - kind: Char
      token: (pos = 2, size = 1)
      codePoint: U+007B, {
)"},
    {"escape-u{-2", "a\\u{", "u", {{1, 3}, "invalid unicode escape: `\\u{'"}},
    {"escape-u{-3", "a\\u{", "v", {{1, 3}, "invalid unicode escape: `\\u{'"}},

    {"escape-u{FF-1", "\\u{F", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 4)
  patterns:
    - kind: Char
      token: (pos = 0, size = 2)
      codePoint: U+0075, u
    - kind: Char
      token: (pos = 2, size = 1)
      codePoint: U+007B, {
    - kind: Char
      token: (pos = 3, size = 1)
      codePoint: U+0046, F
)"},
    {"escape-u{FF-2", "\\u{F", "u", {{0, 4}, "invalid unicode escape: `\\u{F'"}},
    {"escape-u{FF-3", "\\u{F", "v", {{0, 4}, "invalid unicode escape: `\\u{F'"}},

    {"escape-u{}-1", "\\u{}", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 4)
  patterns:
    - kind: Char
      token: (pos = 0, size = 2)
      codePoint: U+0075, u
    - kind: Char
      token: (pos = 2, size = 1)
      codePoint: U+007B, {
    - kind: Char
      token: (pos = 3, size = 1)
      codePoint: U+007D, }
)"},
    {"escape-u{}-2", "\\u{}", "u", {{0, 4}, "invalid unicode escape: `\\u{}'"}},
    {"escape-u{}-3", "\\u{}", "v", {{0, 4}, "invalid unicode escape: `\\u{}'"}},

    {"escape-u{1W}-1", "\\u{1W}", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 6)
  patterns:
    - kind: Char
      token: (pos = 0, size = 2)
      codePoint: U+0075, u
    - kind: Char
      token: (pos = 2, size = 1)
      codePoint: U+007B, {
    - kind: Char
      token: (pos = 3, size = 1)
      codePoint: U+0031, 1
    - kind: Char
      token: (pos = 4, size = 1)
      codePoint: U+0057, W
    - kind: Char
      token: (pos = 5, size = 1)
      codePoint: U+007D, }
)"},
    {"escape-u{1W}-2", "\\u{1W}", "u", {{0, 5}, "invalid unicode escape: `\\u{1W'"}},
    {"escape-u{1W}-3", "\\u{1W}", "v", {{0, 5}, "invalid unicode escape: `\\u{1W'"}},

    {"escape-u{7058}-1", "\\u{7058}", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 8)
  patterns:
    - kind: Char
      token: (pos = 0, size = 2)
      codePoint: U+0075, u
    - kind: Char
      token: (pos = 2, size = 1)
      codePoint: U+007B, {
    - kind: Char
      token: (pos = 3, size = 1)
      codePoint: U+0037, 7
    - kind: Char
      token: (pos = 4, size = 1)
      codePoint: U+0030, 0
    - kind: Char
      token: (pos = 5, size = 1)
      codePoint: U+0035, 5
    - kind: Char
      token: (pos = 6, size = 1)
      codePoint: U+0038, 8
    - kind: Char
      token: (pos = 7, size = 1)
      codePoint: U+007D, }
)"},
    {"escape-u{7058}-2", "\\u{7058}", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 8)
  codePoint: U+7058, 灘
)"},
    {"escape-u{7058}-3", "\\u{7058}", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 8)
  codePoint: U+7058, 灘
)"},

    {"esc-u{FFFFFFF}-1", "\\u{FFFFFFF}", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 11)
  patterns:
    - kind: Char
      token: (pos = 0, size = 2)
      codePoint: U+0075, u
    - kind: Char
      token: (pos = 2, size = 1)
      codePoint: U+007B, {
    - kind: Char
      token: (pos = 3, size = 1)
      codePoint: U+0046, F
    - kind: Char
      token: (pos = 4, size = 1)
      codePoint: U+0046, F
    - kind: Char
      token: (pos = 5, size = 1)
      codePoint: U+0046, F
    - kind: Char
      token: (pos = 6, size = 1)
      codePoint: U+0046, F
    - kind: Char
      token: (pos = 7, size = 1)
      codePoint: U+0046, F
    - kind: Char
      token: (pos = 8, size = 1)
      codePoint: U+0046, F
    - kind: Char
      token: (pos = 9, size = 1)
      codePoint: U+0046, F
    - kind: Char
      token: (pos = 10, size = 1)
      codePoint: U+007D, }
)"},
    {"esc-u{FFFFFFF}-2", " \\u{FFFFFFF}", "u", {{1, 11}, "invalid unicode escape: `\\u{FFFFFFF}'"}},
    {"esc-u{FFFFFFF}-3", " \\u{FFFFFFF}", "v", {{1, 11}, "invalid unicode escape: `\\u{FFFFFFF}'"}},

    {"esc-u{D888}-1", "\\u{D888}", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 8)
  patterns:
    - kind: Char
      token: (pos = 0, size = 2)
      codePoint: U+0075, u
    - kind: Char
      token: (pos = 2, size = 1)
      codePoint: U+007B, {
    - kind: Char
      token: (pos = 3, size = 1)
      codePoint: U+0044, D
    - kind: Char
      token: (pos = 4, size = 1)
      codePoint: U+0038, 8
    - kind: Char
      token: (pos = 5, size = 1)
      codePoint: U+0038, 8
    - kind: Char
      token: (pos = 6, size = 1)
      codePoint: U+0038, 8
    - kind: Char
      token: (pos = 7, size = 1)
      codePoint: U+007D, }
)"},
    {"esc-u{D888}-2",
     "\\u{D888}",
     "u",
     {{0, 8}, "unicode escape generate invalid UTF-8 (surrogate): `U+D888'"}},
    {"esc-u{D888}-3",
     "\\u{D888}",
     "v",
     {{0, 8}, "unicode escape generate invalid UTF-8 (surrogate): `U+D888'"}},
};
INSTANTIATE_TEST_SUITE_P(UnicodeEscape2, SyntaxTreeTest,
                         ::testing::ValuesIn(syntaxUnicodeEscape2Cases));

static constexpr SyntaxTreeTestEntry syntaxUnicodePropertyCases[] = {
    {"esc-p-1", "\\p", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 2)
  codePoint: U+0070, p
)"},
    {"esc-p-2", "\\p", "u", {{0, 2}, "invalid unicode property escape: `\\p'"}},
    {"esc-p-3", "\\p", "v", {{0, 2}, "invalid unicode property escape: `\\p'"}},

    {"esc-P{-1", "\\P{", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 3)
  patterns:
    - kind: Char
      token: (pos = 0, size = 2)
      codePoint: U+0050, P
    - kind: Char
      token: (pos = 2, size = 1)
      codePoint: U+007B, {
)"},
    {"esc-P{-2", "\\P{", "u", {{0, 3}, "invalid unicode property escape: `\\P{'"}},
    {"esc-P{-3", "\\P{", "v", {{0, 3}, "invalid unicode property escape: `\\P{'"}},

    {"esc-p{C-1", "\\p{C", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 4)
  patterns:
    - kind: Char
      token: (pos = 0, size = 2)
      codePoint: U+0070, p
    - kind: Char
      token: (pos = 2, size = 1)
      codePoint: U+007B, {
    - kind: Char
      token: (pos = 3, size = 1)
      codePoint: U+0043, C
)"},
    {"esc-p{C-2", "\\p{C", "u", {{0, 4}, "invalid unicode property escape: `\\p{C'"}},
    {"esc-p{C-3", "\\p{C", "v", {{0, 4}, "invalid unicode property escape: `\\p{C'"}},

    {"esc-p{あ}-1", "\\p{あ}", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 7)
  patterns:
    - kind: Char
      token: (pos = 0, size = 2)
      codePoint: U+0070, p
    - kind: Char
      token: (pos = 2, size = 1)
      codePoint: U+007B, {
    - kind: Char
      token: (pos = 3, size = 3)
      codePoint: U+3042, あ
    - kind: Char
      token: (pos = 6, size = 1)
      codePoint: U+007D, }
)"},
    {"esc-p{あ}-2", "\\p{あ}", "u", {{0, 7}, "unrecognized property value: あ"}},
    {"esc-p{あ}-3", "\\p{あ}", "v", {{0, 7}, "unrecognized property value: あ"}},

    {"esc-p{Co}-1", "\\p{Co}", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 6)
  patterns:
    - kind: Char
      token: (pos = 0, size = 2)
      codePoint: U+0070, p
    - kind: Char
      token: (pos = 2, size = 1)
      codePoint: U+007B, {
    - kind: Char
      token: (pos = 3, size = 1)
      codePoint: U+0043, C
    - kind: Char
      token: (pos = 4, size = 1)
      codePoint: U+006F, o
    - kind: Char
      token: (pos = 5, size = 1)
      codePoint: U+007D, }
)"},
    {"esc-p{Co}-2", "\\p{Co}", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 6)
  property: UNICODE
  value: gc=Private_Use
  invert: false
)"},
    {"esc-p{Co}-3", "\\p{Co}", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 6)
  property: UNICODE
  value: gc=Private_Use
  invert: false
)"},

    {"esc-p{GC=C}-1", "\\p{GC=C}", "u", {{0, 8}, "unrecognized property name: GC"}},
    {"esc-p{GC=C}-2", "\\p{GC=C}", "v", {{0, 8}, "unrecognized property name: GC"}},

    {"esc-p{Arab}-1", " \\p{Arab}", "u", {{1, 8}, "unrecognized property value: Arab"}},
    {"esc-p{Arab}-2", " \\p{Arab}", "v", {{1, 8}, "unrecognized property value: Arab"}},

    {"esc-p{Script=Arab}-1", "\\p{Script=Arab}", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 15)
  property: UNICODE
  value: sc=Arabic
  invert: false
)"},
    {"esc-p{Script=Arab}-2", "\\p{Script=Arab}", "vi", R"(
flag: (mode = v, modifier = i)
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 15)
  property: UNICODE
  value: sc=Arabic
  invert: false
)"},

    {"esc-P{Term}-1", "\\P{Term}", "u", R"(
flag: (mode = u, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 8)
  property: NOT_UNICODE
  value: Terminal_Punctuation
  invert: true
)"},
    {"esc-P{Term}-2", "\\P{Term}", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 8)
  property: NOT_UNICODE
  value: Terminal_Punctuation
  invert: true
)"},

    {"esc-p{RGI_Emoji}-1",
     "\\p{RGI_Emoji}",
     "u",
     {{0, 13}, "unrecognized property value: RGI_Emoji"}},
    {"esc-p{RGI_Emoji}-2", "\\p{RGI_Emoji}", "v", R"(
flag: (mode = v, modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Property
  token: (pos = 0, size = 13)
  property: EMOJI
  value: RGI_Emoji
  invert: false
)"},
};

INSTANTIATE_TEST_SUITE_P(UnicodeProperty, SyntaxTreeTest,
                         ::testing::ValuesIn(syntaxUnicodePropertyCases));

static constexpr SyntaxTreeTestEntry syntaxNamedBackRefCases[] = {
    {"ref1", "\\k", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Char
  token: (pos = 0, size = 2)
  codePoint: U+006B, k
)"},
    {"ref2", "\\k", "u", {{0, 2}, "\\k is not followed by <"}},
    {"ref3", "\\k", "v", {{0, 2}, "\\k is not followed by <"}},
    {"ref-<1", "\\k<", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 3)
  patterns:
    - kind: Char
      token: (pos = 0, size = 2)
      codePoint: U+006B, k
    - kind: Char
      token: (pos = 2, size = 1)
      codePoint: U+003C, <
)"},
    {"ref-<2", "\\k<", "u", {{2, 1}, "capture group name must contain valid identifier: `<'"}},
    {"ref-<2-1", "\\k<2", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 4)
  patterns:
    - kind: Char
      token: (pos = 0, size = 2)
      codePoint: U+006B, k
    - kind: Char
      token: (pos = 2, size = 1)
      codePoint: U+003C, <
    - kind: Char
      token: (pos = 3, size = 1)
      codePoint: U+0032, 2
)"},
    {"ref-<2-3", "\\k<2", "v", {{2, 2}, "capture group name must contain valid identifier: `<2'"}},
    {"ref-<あ", "\\k<あ", "v", {{2, 4}, "unclosed capture group name: `<あ'"}},
    {"ref-<あ@",
     "\\k<あ@",
     "v",
     {{2, 5}, "capture group name must contain valid identifier: `<あ@'"}},
    {"ref-<あ1", "\\k<あ1", "v", {{2, 5}, "unclosed capture group name: `<あ1'"}},
    {"ref-esc", "\\k<\\", "u", {{3, 1}, "invalid unicode escape: `\\'"}},
    {"ref-esc-u", "\\k<\\u", "u", {{3, 2}, "invalid unicode escape: `\\u'"}},
    {"ref-esc-u{", "\\k<\\u{", "u", {{3, 3}, "invalid unicode escape: `\\u{'"}},
    {"ref-esc-u",
     "\\k<\\uEEEE",
     "u",
     {{2, 7}, "capture group name must contain valid identifier: `<\xEE\xBB\xAE'"}},

    {"ref-あ1-1", "\\k<あ1>", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 8)
  patterns:
    - kind: Char
      token: (pos = 0, size = 2)
      codePoint: U+006B, k
    - kind: Char
      token: (pos = 2, size = 1)
      codePoint: U+003C, <
    - kind: Char
      token: (pos = 3, size = 3)
      codePoint: U+3042, あ
    - kind: Char
      token: (pos = 6, size = 1)
      codePoint: U+0031, 1
    - kind: Char
      token: (pos = 7, size = 1)
      codePoint: U+003E, >
)"},
    {"ref-あ1-2", "\\k<あ1>", "u", {{0, 8}, "undefined capture group name: `<あ1>'"}},
    {"ref-あ1-3", "\\k<あ1>", "v", {{0, 8}, "undefined capture group name: `<あ1>'"}},

    {"ref-u3042-1", "\\k<\\u30421>", "", R"(
flag: (mode = , modifier = )
captureGroupCount: 0
namedCaptureGroups: []
pattern:
  kind: Seq
  token: (pos = 0, size = 8)
  patterns:
    - kind: Char
      token: (pos = 0, size = 2)
      codePoint: U+006B, k
    - kind: Char
      token: (pos = 2, size = 1)
      codePoint: U+003C, <
    - kind: Char
      token: (pos = 3, size = 3)
      codePoint: U+3042, あ
    - kind: Char
      token: (pos = 6, size = 1)
      codePoint: U+0031, 1
    - kind: Char
      token: (pos = 7, size = 1)
      codePoint: U+003E, >
)"},
    {"ref-u30421-2", "\\k<\\u30421>", "u", {{0, 11}, "undefined capture group name: `<あ1>'"}},
    {"ref-u30421-3", "\\k<\\u30421>", "v", {{0, 11}, "undefined capture group name: `<あ1>'"}},

    //     {"ref-u{3042}1-1", "\\k<\\u{3042}1>", "", R"(
    // )"}, //TODO: fix repeat
    {"ref-u{3042}1-2", "\\k<\\u{3042}1>", "v", {{0, 13}, "undefined capture group name: `<あ1>'"}},
};
INSTANTIATE_TEST_SUITE_P(NamedBackRef, SyntaxTreeTest,
                         ::testing::ValuesIn(syntaxNamedBackRefCases));

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}