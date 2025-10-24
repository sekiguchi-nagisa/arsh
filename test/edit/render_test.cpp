#include "gtest/gtest.h"

#include "../test_common.h"

#include <line_buffer.h>
#include <line_renderer.h>
#include <object.h>
#include <pager.h>
#include <renderer.h>
#include <vm.h>

using namespace arsh;

TEST(ColorEscapeTest, base) {
  ASSERT_TRUE(ANSIEscapeSeqMap::checkSGRSeq(""));
  ASSERT_TRUE(ANSIEscapeSeqMap::checkSGRSeq("\x1b[m"));
  ASSERT_TRUE(ANSIEscapeSeqMap::checkSGRSeq("\x1b[0m"));
  ASSERT_TRUE(ANSIEscapeSeqMap::checkSGRSeq("\x1b[000;0001234432;124123;234524300000m"));
  ASSERT_FALSE(ANSIEscapeSeqMap::checkSGRSeq(" "));
  ASSERT_FALSE(ANSIEscapeSeqMap::checkSGRSeq("abc"));
  ASSERT_FALSE(ANSIEscapeSeqMap::checkSGRSeq("\x1bqqqq"));
  ASSERT_FALSE(ANSIEscapeSeqMap::checkSGRSeq("\x1b["));
  ASSERT_FALSE(ANSIEscapeSeqMap::checkSGRSeq("\x1b[a"));
  ASSERT_FALSE(ANSIEscapeSeqMap::checkSGRSeq("\x1b[1"));
  ASSERT_FALSE(ANSIEscapeSeqMap::checkSGRSeq("\x1b[1a"));
  ASSERT_FALSE(ANSIEscapeSeqMap::checkSGRSeq("\x1b[1233"));
  ASSERT_FALSE(ANSIEscapeSeqMap::checkSGRSeq("\x1b[1324231a"));
  ASSERT_FALSE(ANSIEscapeSeqMap::checkSGRSeq("\x1b[1324231;"));
  ASSERT_FALSE(ANSIEscapeSeqMap::checkSGRSeq("\x1b[1324231;;"));
  ASSERT_FALSE(ANSIEscapeSeqMap::checkSGRSeq("\x1b[1324231;m"));
  ASSERT_FALSE(ANSIEscapeSeqMap::checkSGRSeq("\x1b[1324231;a"));
  ASSERT_FALSE(ANSIEscapeSeqMap::checkSGRSeq("\x1b[1324231;3"));
  ASSERT_FALSE(ANSIEscapeSeqMap::checkSGRSeq("\x1b[1324231;30"));
  ASSERT_FALSE(ANSIEscapeSeqMap::checkSGRSeq("\x1b[1324231;2342a"));
  ASSERT_FALSE(ANSIEscapeSeqMap::checkSGRSeq("\x1b[1324231;2324m1"));
}

TEST(ColorEscapeTest, map) {
  StringRef color =
      "lineno=\x1b[38;2;149;149;149m background=\x1b[38;2;212;212;212m "
      "foreground=\x1b[38;2;212;212;212m "
      "attribute=\x1b[38;2;212;212;212m none=\x1b[38;2;212;212;212m comment=\x1b[38;2;128;128;128m "
      "keyword=\x1b[38;2;204;120;50m\x1b[1m operator=\x1b[38;2;204;120;50m\x1b[1m "
      "number=\x1b[38;2;104;151;187m regex=\x1b[38;2;100;102;149m string=\x1b[38;2;106;135;89m "
      "command=\x1b[38;2;255;198;109m command_arg=\x1b[38;2;169;183;198m "
      "redirect=\x1b[38;2;169;183;198m variable=\x1b[38;2;152;118;170m type=\x1b[38;2;255;198;109m "
      "member=\x1b[38;2;169;183;198m";

  auto seqMap = ANSIEscapeSeqMap::fromString(color);
  auto &values = seqMap.getValues();
  ASSERT_EQ(16, values.size());
  ASSERT_TRUE(values.find(HighlightTokenClass::NONE_) == values.end()); // always ignore NONE
  ASSERT_EQ(values.find(HighlightTokenClass::COMMENT)->second, "\x1b[38;2;128;128;128m");
  ASSERT_EQ(values.find(HighlightTokenClass::KEYWORD)->second, "\x1b[38;2;204;120;50m\x1b[1m");
  ASSERT_EQ(values.find(HighlightTokenClass::OPERATOR)->second, "\x1b[38;2;204;120;50m\x1b[1m");
  ASSERT_EQ(values.find(HighlightTokenClass::NUMBER)->second, "\x1b[38;2;104;151;187m");
  ASSERT_EQ(values.find(HighlightTokenClass::REGEX)->second, "\x1b[38;2;100;102;149m");
  ASSERT_EQ(values.find(HighlightTokenClass::STRING)->second, "\x1b[38;2;106;135;89m");
  ASSERT_EQ(values.find(HighlightTokenClass::COMMAND)->second, "\x1b[38;2;255;198;109m");
  ASSERT_EQ(values.find(HighlightTokenClass::COMMAND_ARG)->second, "\x1b[38;2;169;183;198m");
  ASSERT_EQ(values.find(HighlightTokenClass::REDIRECT)->second, "\x1b[38;2;169;183;198m");
  ASSERT_EQ(values.find(HighlightTokenClass::VARIABLE)->second, "\x1b[38;2;152;118;170m");
  ASSERT_EQ(values.find(HighlightTokenClass::TYPE)->second, "\x1b[38;2;255;198;109m");
  ASSERT_EQ(values.find(HighlightTokenClass::MEMBER)->second, "\x1b[38;2;169;183;198m");
  ASSERT_EQ(values.find(HighlightTokenClass::ATTRIBUTE)->second, "\x1b[38;2;212;212;212m");
  ASSERT_EQ(values.find(HighlightTokenClass::FOREGROUND_)->second, "\x1b[38;2;212;212;212m");
  ASSERT_EQ(values.find(HighlightTokenClass::BACKGROUND_)->second, "\x1b[38;2;212;212;212m");
  ASSERT_EQ(values.find(HighlightTokenClass::LINENO_)->second, "\x1b[38;2;149;149;149m");
}

TEST(ColorEscapeTest, invalid) {
  StringRef color = "lineno=\x1b[38;2;149;149;149m hogea BBB=23 background=\x1b[38;2;212;212;212m "
                    "AAAAA=\x1b[38;2;212;212;212m";
  auto seqMap = ANSIEscapeSeqMap::fromString(color);
  auto &values = seqMap.getValues();
  ASSERT_EQ(2, values.size());
  ASSERT_EQ(values.find(HighlightTokenClass::LINENO_)->second, "\x1b[38;2;149;149;149m");
  ASSERT_EQ(values.find(HighlightTokenClass::BACKGROUND_)->second, "\x1b[38;2;212;212;212m");
}

struct Len {
  unsigned int byteSize;
  unsigned int colSize;
};

static Len getCharLen(StringRef line, const CharWidthProperties &ps, unsigned int initCols = 0) {
  Len len{0, 0};
  iterateGraphemeUntil(line, 1, [&](const GraphemeCluster &grapheme) {
    LineRenderer renderer(ps, initCols);
    renderer.setEmitNewline(false);
    renderer.renderLines(grapheme.getRef());
    len.byteSize = grapheme.getRef().size();
    len.colSize = renderer.getTotalCols() - initCols;
  });
  return len;
}

TEST(EncodingTest, charLen1) {
  // next char
  CharWidthProperties ps;
  StringRef line = "あaう";
  ASSERT_EQ("あaう", line);
  auto ret = getCharLen(line, ps); // あ
  ASSERT_EQ(3, ret.byteSize);
  ASSERT_EQ(2, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("aう", line);
  ret = getCharLen(line, ps); // a
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(1, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("う", line);
  ret = getCharLen(line, ps); // う
  ASSERT_EQ(3, ret.byteSize);
  ASSERT_EQ(2, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("", line);
  ret = getCharLen(line, ps); // end
  ASSERT_EQ(0, ret.byteSize);
  ASSERT_EQ(0, ret.colSize);
}

TEST(EncodingTest, charLen2) {
  // next char
  CharWidthProperties ps;
  StringRef line = "○a○🇦🇽b🇦🇽💁🏾‍♀️c💁🏾‍♀️🇦";
  auto ret = getCharLen(line, ps); // ○
  ASSERT_EQ(3, ret.byteSize);
  ASSERT_EQ(1, ret.colSize); // half width

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("a○🇦🇽b🇦🇽💁🏾‍♀️c💁🏾‍♀️🇦", line);
  ret = getCharLen(line, ps); // a
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(1, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("○🇦🇽b🇦🇽💁🏾‍♀️c💁🏾‍♀️🇦", line);
  ps.setProperty(CharWidthProperty::EAW, 2);
  ret = getCharLen(line, ps); // ○
  ASSERT_EQ(3, ret.byteSize);
  ASSERT_EQ(2, ret.colSize); // full width

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("🇦🇽b🇦🇽💁🏾‍♀️c💁🏾‍♀️🇦", line);
  ps = CharWidthProperties();
  ps.setProperty(CharWidthProperty::EMOJI_FLAG_SEQ, 2);
  ret = getCharLen(line, ps); // 🇦🇽
  ASSERT_EQ(8, ret.byteSize);
  ASSERT_EQ(2, ret.colSize); // FLAG_SEQ width is 2

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("b🇦🇽💁🏾‍♀️c💁🏾‍♀️🇦", line);
  ret = getCharLen(line, ps); // b
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(1, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("🇦🇽💁🏾‍♀️c💁🏾‍♀️🇦", line);
  ps = CharWidthProperties();
  ps.setProperty(CharWidthProperty::EMOJI_FLAG_SEQ, 4);
  ret = getCharLen(line, ps); // 🇦🇽
  ASSERT_EQ(8, ret.byteSize);
  ASSERT_EQ(4, ret.colSize); // FLAG_SEQ width is 4

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("💁🏾‍♀️c💁🏾‍♀️🇦", line);
  ps = CharWidthProperties();
  ret = getCharLen(line, ps); // 💁🏾‍♀️
  ASSERT_EQ(17, ret.byteSize);
  ASSERT_EQ(2, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("c💁🏾‍♀️🇦", line);
  ret = getCharLen(line, ps); // c
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(1, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("💁🏾‍♀️🇦", line);
  ps = CharWidthProperties();
  ps.setProperty(CharWidthProperty::EMOJI_ZWJ_SEQ, 3);
  ps.setProperty(CharWidthProperty::EAW, 2);
  ret = getCharLen(line, ps); // 💁🏾‍♀️
  ASSERT_EQ(17, ret.byteSize);
  ASSERT_EQ(6, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("🇦", line);
  ret = getCharLen(line, ps); //
  ASSERT_EQ(4, ret.byteSize);
  ASSERT_EQ(1, ret.colSize); // regional indicator is half

  ps.setProperty(CharWidthProperty::RGI, 2); // override width
  ASSERT_EQ("🇦", line);
  ret = getCharLen(line, ps); //
  ASSERT_EQ(4, ret.byteSize);
  ASSERT_EQ(2, ret.colSize);
}

TEST(EncodingTest, charLenControl) {
  CharWidthProperties ps;
  StringRef line = "\x1b\r\r\n\n"; // control char
  auto ret = getCharLen(line, ps); // \x1b
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(2, ret.colSize); // caret

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("\r\r\n\n", line);
  ret = getCharLen(line, ps); // \r
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(2, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("\r\n\n", line);
  ret = getCharLen(line, ps); // \r\n
  ASSERT_EQ(2, ret.byteSize);
  ASSERT_EQ(2, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("\n", line);
  ret = getCharLen(line, ps); // \n
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(0, ret.colSize);
}

TEST(EncodingTest, charTab) {
  CharWidthProperties ps;
  StringRef line = "\t\t\t\t";
  auto ret = getCharLen(line, ps, 0); // \t
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(4, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("\t\t\t", line);
  ret = getCharLen(line, ps, 1); // \t
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(3, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("\t\t", line);
  ret = getCharLen(line, ps, 3); // \t
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(1, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("\t", line);
  ret = getCharLen(line, ps, 4); // \t
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(4, ret.colSize);
}

TEST(EncodingTest, charInvalid) {
  CharWidthProperties ps;
  StringRef line = "\xFF\xFA";
  auto ret = getCharLen(line, ps);
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(0, ret.colSize);

  ps.replaceInvalid = true;
  ps.eaw = AmbiguousCharWidth::HALF;
  ret = getCharLen(line, ps);
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(1, ret.colSize);

  ps.replaceInvalid = true;
  ps.eaw = AmbiguousCharWidth::FULL;
  ret = getCharLen(line, ps);
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(2, ret.colSize);
}

class LineRendererTest : public ExpectOutput {
public:
  static bool isCompleteLine(StringRef source) {
    CharWidthProperties ps;
    std::string out;
    LineRenderer renderer(ps, 0, out);
    return renderer.renderScript(source);
  }

  static std::string renderPrompt(StringRef source, size_t offset = 0) {
    CharWidthProperties ps;
    ps.replaceInvalid = true;
    std::string out;
    LineRenderer renderer(ps, offset, out);
    renderer.renderWithANSI(source);
    return out;
  }

  static std::string renderLines(StringRef source, size_t offset = 0) {
    CharWidthProperties ps;
    ps.replaceInvalid = true;
    std::string out;
    LineRenderer renderer(ps, offset, out);
    renderer.renderLines(source);
    return out;
  }

  static std::string renderScript(StringRef source, size_t offset = 0,
                                  ObserverPtr<const ANSIEscapeSeqMap> seqMap = nullptr) {
    CharWidthProperties ps;
    ps.replaceInvalid = true;
    std::string out;
    LineRenderer renderer(ps, offset, out, seqMap);
    renderer.renderScript(source);
    return out;
  }
};

TEST_F(LineRendererTest, continuation) {
  ASSERT_TRUE(isCompleteLine("echo"));
  ASSERT_TRUE(isCompleteLine("{}}"));
  ASSERT_TRUE(isCompleteLine("$OSTYPE ++"));
  ASSERT_TRUE(isCompleteLine("$/frefrear\\/fer"));
  ASSERT_TRUE(isCompleteLine("echo >"));
  ASSERT_TRUE(isCompleteLine("{ echo >"));
  ASSERT_TRUE(isCompleteLine("if true"));
  ASSERT_TRUE(isCompleteLine("cat <<< EOF"));
  ASSERT_TRUE(isCompleteLine("cat << EOF\nthis is a pen\nEOF"));
  ASSERT_FALSE(isCompleteLine("echo\\"));
  ASSERT_FALSE(isCompleteLine("echo AAA\\"));
  ASSERT_FALSE(isCompleteLine("if (true"));
  ASSERT_FALSE(isCompleteLine("(echo >"));
  ASSERT_FALSE(isCompleteLine("{ echo hello"));
  ASSERT_FALSE(isCompleteLine("$(23456"));
  ASSERT_FALSE(isCompleteLine("23456."));
  ASSERT_FALSE(isCompleteLine(R"("ehochll$OSTYPE )"));
  ASSERT_FALSE(isCompleteLine("$OSTYPE + "));
  ASSERT_FALSE(isCompleteLine("$OSTYPE \\"));
  ASSERT_FALSE(isCompleteLine("echo hello  \\"));
  ASSERT_FALSE(isCompleteLine("echo hello 'frefera"));
  ASSERT_FALSE(isCompleteLine("34 + $'frefera"));
  ASSERT_FALSE(isCompleteLine("cat << EOF"));
  ASSERT_FALSE(isCompleteLine("cat 0<< 'EOF-_1d'"));
  ASSERT_FALSE(isCompleteLine("cat <<- EOF"));
  ASSERT_FALSE(isCompleteLine("cat << EOF\nthis is a pen"));
  ASSERT_FALSE(isCompleteLine("cat << EOF\n$OSTYPE"));
}

TEST_F(LineRendererTest, lines) {
  ASSERT_EQ("echo hello", renderLines("echo hello"));
  ASSERT_EQ("echo \r\n  hello\r\n  \r\n  ", renderLines("echo \nhello\n\n", 2));
  ASSERT_EQ("echo  1   ^H^G\r\n  @ ^[", renderLines("echo \t1\t\b\a\n@\t\x1b", 2));
  ASSERT_EQ("   @\r\n 1  @", renderLines("\t@\n1\t@", 1));
  ASSERT_EQ("  @\r\n  1 @", renderLines("\t@\n1\t@", 2));
  ASSERT_EQ(" @\r\n   1    @", renderLines("\t@\n1\t@", 3));
  ASSERT_EQ("    @\r\n    1   @", renderLines("\t@\n1\t@", 4));

  std::string expect = "echo ";
  expect += UnicodeUtil::REPLACEMENT_CHAR_UTF8;
  expect += "^M";
  expect += UnicodeUtil::REPLACEMENT_CHAR_UTF8;
  ASSERT_EQ(expect, this->renderLines("echo \xFF\r\xFF"));
}

TEST_F(LineRendererTest, prompt) {
  ASSERT_EQ("echo hello", renderPrompt("echo hello"));
  ASSERT_EQ("echo \r\n   hello", renderPrompt("echo \nhello", 3));
  ASSERT_EQ("\x1b[23mecho\x1b[0m ^[\r\n   hello",
            renderPrompt("\x1b[23mecho\x1b[0m \x1b\nhello", 3));
}

TEST_F(LineRendererTest, script) {
  ANSIEscapeSeqMap seqMap({
      {HighlightTokenClass::COMMAND, "\x1b[30m"},
      {HighlightTokenClass::COMMAND_ARG, "\x1b[40m"},
  });

  ASSERT_EQ("echo hello \\", renderScript("echo hello \\"));
  ASSERT_EQ("echo hello\r\n  ", renderScript("echo hello\n", 2));
  ASSERT_EQ("\x1b[30mecho\x1b[0m \x1b[40mhello\x1b[0m \\\r\n    \x1b[40m!!\x1b[0m",
            renderScript("echo hello \\\n  !!", 2, makeObserver(seqMap)));
}

TEST_F(LineRendererTest, ignoreNewline) {
  CharWidthProperties ps;
  std::string out;
  StringRef line = "echo 111\necho 222\necho 333\necho 444";
  {
    LineRenderer renderer(ps, 2, out);
    renderer.setEmitNewline(false);
    renderer.renderLines(line);
    ASSERT_EQ(0, renderer.getTotalRows());
    ASSERT_EQ(34, renderer.getTotalCols());
  }
  ASSERT_EQ("echo 111echo 222echo 333echo 444", out);
}

TEST_F(LineRendererTest, softwrap) {
  CharWidthProperties ps;
  std::string out;
  StringRef line = "\t1234567890";
  {
    LineRenderer renderer(ps, 2, out);
    renderer.setColLimit(5);
    renderer.renderLines(line);
    ASSERT_EQ(2, renderer.getTotalRows());
    ASSERT_EQ(4, renderer.getTotalCols());
  }
  ASSERT_EQ("  1\r\n23456\r\n7890", out);

  out = "";
  line = "\t1234567890あab\r\n@";
  {
    LineRenderer renderer(ps, 3, out);
    renderer.setColLimit(5);
    renderer.renderLines(line);
    ASSERT_EQ(5, renderer.getTotalRows());
    ASSERT_EQ(4, renderer.getTotalCols());
  }
  ASSERT_EQ(" 1\r\n23456\r\n7890\r\nあab\r\n^M\r\n   @", out);

  out = "";
  line = "1234\t@ \r";
  {
    LineRenderer renderer(ps, 0, out);
    renderer.setColLimit(4);
    renderer.renderLines(line);
    ASSERT_EQ(3, renderer.getTotalRows());
    ASSERT_EQ(0, renderer.getTotalCols());
  }
  ASSERT_EQ("1234\r\n    \r\n@ ^M\r\n", out);
}

TEST(RendererTest, semanticPrompt) {
  std::string str;
  str.resize(1024);
  RenderingContext ctx(str.data(), str.size(), "1>\n2>\n>> ", nullptr);
  ctx.semanticPrompt = true;
  ctx.buf.insertToCursor("1111\n2222\n3333");

  {
    auto ret = doRendering(ctx, nullptr, nullptr, 80);
    std::string expect = "1>\r\n"
                         "2>\r\n"
                         ">> \x1b]133;B\x1b\\1111\r\n"
                         "   2222\r\n"
                         "   3333";
    ASSERT_EQ(expect, ret.renderedLines);
  }

  {
    auto ret = doRendering(ctx, nullptr, nullptr, 80);
    std::string expect = "1>\r\n"
                         "2>\r\n"
                         ">> \x1b]133;B\x1b\\1111\r\n"
                         "   2222\r\n"
                         "   3333";
    ASSERT_EQ(expect, ret.renderedLines);
  }
}

class PagerTest : public ExpectOutput {
protected:
  ARState *state;
  CharWidthProperties ps;

public:
  PagerTest() {
    this->state = ARState_create();
    this->ps.replaceInvalid = true;
  }

  ~PagerTest() override { ARState_delete(&this->state); }

  void append(CandidatesObject &) {}

  template <typename... T>
  void append(CandidatesObject &wrapper, const char *first, T &&...remain) {
    wrapper.addNewCandidateFrom(*this->state, std::string(first), false);
    this->append(wrapper, std::forward<T>(remain)...);
  }

  template <typename... T>
  ObjPtr<CandidatesObject> create(T &&...args) {
    auto obj = createObject<CandidatesObject>();
    this->append(*obj, std::forward<T>(args)...);
    return obj;
  }

  ObjPtr<CandidatesObject> createWith(std::vector<std::pair<const char *, const char *>> &&args,
                                      const CandidateAttr attr = {CandidateAttr::Kind::NONE,
                                                                  false}) {
    auto obj = createObject<CandidatesObject>();
    for (auto &[can, sig] : args) {
      obj->addNewCandidateWith(*this->state, can, sig, attr);
    }
    return obj;
  }

  std::string render(const ArrayPager &pager) const {
    std::string out;
    LineRenderer renderer(this->ps, 0, out);
    pager.render(renderer);
    return out;
  }
};

TEST_F(PagerTest, small1) { // less than pager length
  auto array = this->create("AAA", "BBB", "CCC", "DDD", "EEE", "FFF");
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 24, .cols = 10});
  ASSERT_EQ(2, pager.getPanes());
  ASSERT_TRUE(pager.getRows() < pager.getWinSize().rows);
  ASSERT_TRUE(pager.getRows() > array->size() / 2);
  ASSERT_EQ(0, pager.getCurRow());

  const char *expect = "\x1b[7mAAA \x1b[0mDDD \r\nBBB EEE \r\nCCC FFF \r\n";
  std::string out = this->render(pager);
  ASSERT_EQ(expect, out);

  // cursor up
  expect = "AAA DDD \r\nBBB EEE \r\nCCC \x1b[7mFFF \x1b[0m\r\n";
  pager.moveCursorToForward();
  ASSERT_EQ(2, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // cursor up
  expect = "AAA DDD \r\nBBB \x1b[7mEEE \x1b[0m\r\nCCC FFF \r\n";
  pager.moveCursorToForward();
  ASSERT_EQ(1, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // cursor up+up
  expect = "AAA DDD \r\nBBB EEE \r\n\x1b[7mCCC \x1b[0mFFF \r\n";
  pager.moveCursorToForward();
  pager.moveCursorToForward();
  ASSERT_EQ(2, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // cursor down+down+down+down
  expect = "\x1b[7mAAA \x1b[0mDDD \r\nBBB EEE \r\nCCC FFF \r\n";
  pager.moveCursorToNext();
  pager.moveCursorToNext();
  pager.moveCursorToNext();
  pager.moveCursorToNext();
  ASSERT_EQ(0, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // cursor down
  expect = "AAA DDD \r\n\x1b[7mBBB \x1b[0mEEE \r\nCCC FFF \r\n";
  pager.moveCursorToNext();
  ASSERT_EQ(1, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, small2) { // less than pager length
  auto array = this->create("AAA", "BBB", "CCC", "DDD", "EEE", "FFF");
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 24, .cols = 10});
  ASSERT_EQ(2, pager.getPanes());

  const char *expect = "\x1b[7mAAA \x1b[0mDDD \r\nBBB EEE \r\nCCC FFF \r\n";
  std::string out = this->render(pager);
  ASSERT_EQ(expect, out);

  // left
  expect = "AAA DDD \r\nBBB EEE \r\nCCC \x1b[7mFFF \x1b[0m\r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(2, pager.getCurRow());
  ASSERT_EQ(5, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // left
  expect = "AAA DDD \r\nBBB EEE \r\n\x1b[7mCCC \x1b[0mFFF \r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(2, pager.getCurRow());
  ASSERT_EQ(2, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // left
  expect = "AAA DDD \r\nBBB \x1b[7mEEE \x1b[0m\r\nCCC FFF \r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(1, pager.getCurRow());
  ASSERT_EQ(4, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right
  expect = "AAA DDD \r\nBBB EEE \r\n\x1b[7mCCC \x1b[0mFFF \r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(2, pager.getCurRow());
  ASSERT_EQ(2, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right
  expect = "AAA DDD \r\nBBB EEE \r\nCCC \x1b[7mFFF \x1b[0m\r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(2, pager.getCurRow());
  ASSERT_EQ(5, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right
  expect = "\x1b[7mAAA \x1b[0mDDD \r\nBBB EEE \r\nCCC FFF \r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(0, pager.getCurRow());
  ASSERT_EQ(0, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, small3) { // less than pager length
  auto array = this->create("AAAAA", "BBBBB", "CCCCC", "DDDDD", "EEEEE");
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 24, .cols = 20});
  ASSERT_EQ(2, pager.getPanes());

  /**
   * AAAAA   DDDDD
   * BBBBB   EEEEE
   * CCCCC
   */
  const char *expect = "\x1b[7mAAAAA   \x1b[0mDDDDD   \r\nBBBBB   EEEEE   \r\nCCCCC   \r\n";
  std::string out = this->render(pager);
  ASSERT_EQ(expect, out);

  // prev
  expect = "AAAAA   DDDDD   \r\nBBBBB   \x1b[7mEEEEE   \x1b[0m\r\nCCCCC   \r\n";
  pager.moveCursorToForward();
  ASSERT_EQ(1, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right
  expect = "AAAAA   DDDDD   \r\nBBBBB   EEEEE   \r\n\x1b[7mCCCCC   \x1b[0m\r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(2, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right
  expect = "\x1b[7mAAAAA   \x1b[0mDDDDD   \r\nBBBBB   EEEEE   \r\nCCCCC   \r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(0, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // left
  expect = "AAAAA   DDDDD   \r\nBBBBB   EEEEE   \r\n\x1b[7mCCCCC   \x1b[0m\r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(2, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, large1) { // larger than pager length
  auto array = this->create("AAA", "BBB", "CC\nC", "DDD", "EEE", "FFF", "GG\t", "HHH");
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 5, .cols = 20});
  ASSERT_EQ(2, pager.getPanes());
  ASSERT_TRUE(pager.getRows() < array->size() / 2);
  ASSERT_EQ(0, pager.getCurRow());

  const char *expect = "\x1b[7mAAA     \x1b[0mEEE     \r\nBBB     FFF     \r\n"
                       "\x1b[7mrows 1-2/4\x1b[0m\r\n";
  std::string out;
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // up
  expect = "CCC     GG      \r\nDDD     \x1b[7mHHH     \x1b[0m\r\n"
           "\x1b[7mrows 3-4/4\x1b[0m\r\n";
  pager.moveCursorToForward();
  ASSERT_EQ(1, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // up
  expect = "CCC     \x1b[7mGG      \x1b[0m\r\nDDD     HHH     \r\n"
           "\x1b[7mrows 3-4/4\x1b[0m\r\n";
  pager.moveCursorToForward();
  ASSERT_EQ(0, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // up
  expect = "BBB     \x1b[7mFFF     \x1b[0m\r\nCCC     GG      \r\n"
           "\x1b[7mrows 2-3/4\x1b[0m\r\n";
  pager.moveCursorToForward();
  ASSERT_EQ(0, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // down
  expect = "BBB     FFF     \r\nCCC     \x1b[7mGG      \x1b[0m\r\n"
           "\x1b[7mrows 2-3/4\x1b[0m\r\n";
  pager.moveCursorToNext();
  ASSERT_EQ(1, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // down
  expect = "CCC     GG      \r\nDDD     \x1b[7mHHH     \x1b[0m\r\n"
           "\x1b[7mrows 3-4/4\x1b[0m\r\n";
  pager.moveCursorToNext();
  ASSERT_EQ(1, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // down
  expect = "\x1b[7mAAA     \x1b[0mEEE     \r\nBBB     FFF     \r\n"
           "\x1b[7mrows 1-2/4\x1b[0m\r\n";
  pager.moveCursorToNext();
  ASSERT_EQ(0, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // down+down+down
  expect = "CCC     GG      \r\n\x1b[7mDDD     \x1b[0mHHH     \r\n"
           "\x1b[7mrows 3-4/4\x1b[0m\r\n";
  pager.moveCursorToNext();
  pager.moveCursorToNext();
  pager.moveCursorToNext();
  ASSERT_EQ(1, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // down
  expect = "AAA     \x1b[7mEEE     \x1b[0m\r\nBBB     FFF     \r\n"
           "\x1b[7mrows 1-2/4\x1b[0m\r\n";
  pager.moveCursorToNext();
  ASSERT_EQ(0, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, large2) { // larger than pager length
  /**
   * AAA EEE
   * BBB FFF
   * CCC GGG
   * DDD HHH
   */
  auto array = this->create("AAA", "BBB", "CC\nC", "DDD", "EEE", "FFF", "GGG", "HHH");
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 5, .cols = 10});
  ASSERT_EQ(2, pager.getPanes());

  const char *expect = "\x1b[7mAAA \x1b[0mEEE \r\nBBB FFF \r\n";
  std::string out;
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // left
  expect = "CCC GGG \r\nDDD \x1b[7mHHH \x1b[0m\r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(1, pager.getCurRow());
  ASSERT_EQ(7, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // left+left+left
  expect = "\x1b[7mCCC \x1b[0mGGG \r\nDDD HHH \r\n";
  pager.moveCursorToLeft();
  pager.moveCursorToLeft();
  pager.moveCursorToLeft();
  ASSERT_EQ(0, pager.getCurRow());
  ASSERT_EQ(2, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // left
  expect = "BBB \x1b[7mFFF \x1b[0m\r\nCCC GGG \r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(0, pager.getCurRow());
  ASSERT_EQ(5, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right
  expect = "BBB FFF \r\n\x1b[7mCCC \x1b[0mGGG \r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(1, pager.getCurRow());
  ASSERT_EQ(2, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right
  expect = "BBB FFF \r\nCCC \x1b[7mGGG \x1b[0m\r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(1, pager.getCurRow());
  ASSERT_EQ(6, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right
  expect = "CCC GGG \r\n\x1b[7mDDD \x1b[0mHHH \r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(1, pager.getCurRow());
  ASSERT_EQ(3, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right+right
  expect = "\x1b[7mAAA \x1b[0mEEE \r\nBBB FFF \r\n";
  pager.moveCursorToRight();
  pager.moveCursorToRight();
  ASSERT_EQ(0, pager.getCurRow());
  ASSERT_EQ(0, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, single) { // larger than pager length
  /**
   * AAA
   * BBB
   * CCC
   * DDD
   */
  auto array = this->create("AAA", "BBB", "CCC", "DDD");
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 100, .cols = 5});
  ASSERT_EQ(1, pager.getPanes());
  ASSERT_EQ(0, pager.getCurRow());
  ASSERT_EQ(0, pager.getIndex());

  const char *expect = "\x1b[7mAAA \x1b[0m\r\nBBB \r\nCCC \r\nDDD \r\n";
  std::string out;
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // next
  expect = "AAA \r\n\x1b[7mBBB \x1b[0m\r\nCCC \r\nDDD \r\n";
  pager.moveCursorToNext();
  ASSERT_EQ(1, pager.getCurRow());
  ASSERT_EQ(1, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right+right
  expect = "AAA \r\nBBB \r\nCCC \r\n\x1b[7mDDD \x1b[0m\r\n";
  pager.moveCursorToRight();
  pager.moveCursorToRight();
  ASSERT_EQ(3, pager.getCurRow());
  ASSERT_EQ(3, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right
  expect = "\x1b[7mAAA \x1b[0m\r\nBBB \r\nCCC \r\nDDD \r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(0, pager.getCurRow());
  ASSERT_EQ(0, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // left
  expect = "AAA \r\nBBB \r\nCCC \r\n\x1b[7mDDD \x1b[0m\r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(3, pager.getCurRow());
  ASSERT_EQ(3, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // left
  expect = "AAA \r\nBBB \r\n\x1b[7mCCC \x1b[0m\r\nDDD \r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(2, pager.getCurRow());
  ASSERT_EQ(2, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, truncate) {
  this->ps.zwjSeqFallback = true;
  auto array = this->create("@@@", "ABCD123456", "ABCD987\r", "ABCDEああ", "123456\t\t",
                            "12345👩🏼‍🏭111");
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 100, .cols = 10});
  ASSERT_EQ(1, pager.getPanes());
  ASSERT_EQ(8, pager.getPaneLen());
  pager.setShowCursor(false);

  std::string out = this->render(pager);
  const char *expect = "@@@     \r\nABCD1234\r\nABCD987.\r\nABCDEあ.\r\n123456  \r\n12345...\r\n";
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, shrinkRow) {
  /**
   * AAA
   * BBB
   * CCC
   * DDD
   * EEE
   * FFF
   */
  auto array = this->create("AAA", "BBB", "CCC", "DDD", "EEE", "FFF");
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 10, .cols = 5});
  ASSERT_EQ(1, pager.getPanes());
  ASSERT_EQ(0, pager.getCurRow());
  ASSERT_EQ(0, pager.getIndex());

  const char *expect = "\x1b[7mAAA \x1b[0m\r\nBBB \r\nCCC \r\nDDD \r\n";
  std::string out;
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  pager.moveCursorToNext();
  pager.moveCursorToNext();
  pager.moveCursorToNext();
  expect = "AAA \r\nBBB \r\nCCC \r\n\x1b[7mDDD \x1b[0m\r\n";
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // shrink
  pager.updateWinSize({.rows = 9, .cols = 5});
  expect = "BBB \r\nCCC \r\n\x1b[7mDDD \x1b[0m\r\n";
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // shrink (no change)
  pager.updateWinSize({.rows = 8, .cols = 5});
  expect = "BBB \r\nCCC \r\n\x1b[7mDDD \x1b[0m\r\n";
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // shrink
  pager.updateWinSize({.rows = 7, .cols = 5});
  expect = "CCC \r\n\x1b[7mDDD \x1b[0m\r\n";
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  pager.moveCursorToNext();
  pager.updateWinSize({.rows = 7, .cols = 5});
  expect = "DDD \r\n\x1b[7mEEE \x1b[0m\r\n";
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // expand
  pager.updateWinSize({.rows = 9, .cols = 5});
  expect = "DDD \r\n\x1b[7mEEE \x1b[0m\r\nFFF \r\n";
  out = this->render(pager);
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, shrinkRatio) {
  const auto array = this->createWith({
      {"AAAAAA", "command"},
      {"BBBBBB", "command"},
      {"CCCCCC", "command"},
      {"DDDDDD", "command"},
      {"EEEEEE", "command"},
      {"FFFFFF", "command"},
  });
  {
    auto pager = ArrayPager::create(*array, this->ps, {.rows = 10, .cols = 30});
    ASSERT_EQ(1, pager.getPanes());
    ASSERT_EQ(0, pager.getCurRow());
    ASSERT_EQ(0, pager.getIndex());
    ASSERT_EQ(40, pager.getRowRatio());

    const char *expect = "\x1b[7mAAAAAA     (command)    \x1b[0m\r\nBBBBBB     (command)    "
                         "\r\nCCCCCC     (command)    \r\nDDDDDD     (command)    \r\n"
                         "\x1B[7mrows 1-4/6\x1B[0m\r\n";
    std::string out;
    out = this->render(pager);
    ASSERT_EQ(expect, out);
  }

  // change ratio (shrink)
  {
    auto pager = ArrayPager::create(*array, this->ps, {.rows = 10, .cols = 30}, 30);
    ASSERT_EQ(1, pager.getPanes());
    ASSERT_EQ(0, pager.getCurRow());
    ASSERT_EQ(0, pager.getIndex());
    ASSERT_EQ(30, pager.getRowRatio());

    const char *expect = "\x1b[7mAAAAAA     (command)    \x1b[0m\r\nBBBBBB     (command)    "
                         "\r\nCCCCCC     (command)    \r\n"
                         "\x1B[7mrows 1-3/6\x1B[0m\r\n";
    std::string out;
    out = this->render(pager);
    ASSERT_EQ(expect, out);
  }

  {
    auto pager = ArrayPager::create(*array, this->ps, {.rows = 10, .cols = 30}, 10);
    ASSERT_EQ(1, pager.getPanes());
    ASSERT_EQ(0, pager.getCurRow());
    ASSERT_EQ(0, pager.getIndex());
    ASSERT_EQ(10, pager.getRowRatio());

    const char *expect = "\x1b[7mAAAAAA     (command)    \x1b[0m\r\n"
                         "\x1B[7mrows 1-1/6\x1B[0m\r\n";
    std::string out;
    out = this->render(pager);
    ASSERT_EQ(expect, out);
  }

  {
    auto pager = ArrayPager::create(*array, this->ps, {.rows = 10, .cols = 30}, 0);
    ASSERT_EQ(1, pager.getPanes());
    ASSERT_EQ(0, pager.getCurRow());
    ASSERT_EQ(0, pager.getIndex());
    ASSERT_EQ(0, pager.getRowRatio());

    const char *expect = "";
    std::string out;
    out = this->render(pager);
    ASSERT_EQ(expect, out);
  }
}

TEST_F(PagerTest, expandRatio) {
  const auto array = this->createWith({
      {"AAAAAA", "command"},
      {"BBBBBB", "command"},
      {"CCCCCC", "command"},
      {"DDDDDD", "command"},
      {"EEEEEE", "command"},
      {"FFFFFF", "command"},
  });
  {
    auto pager = ArrayPager::create(*array, this->ps, {.rows = 10, .cols = 30});
    ASSERT_EQ(1, pager.getPanes());
    ASSERT_EQ(0, pager.getCurRow());
    ASSERT_EQ(0, pager.getIndex());
    ASSERT_EQ(40, pager.getRowRatio());

    const char *expect = "\x1b[7mAAAAAA     (command)    \x1b[0m\r\nBBBBBB     (command)    "
                         "\r\nCCCCCC     (command)    \r\nDDDDDD     (command)    \r\n"
                         "\x1B[7mrows 1-4/6\x1B[0m\r\n";
    std::string out;
    out = this->render(pager);
    ASSERT_EQ(expect, out);
  }

  // change ratio (expand)
  {
    auto pager = ArrayPager::create(*array, this->ps, {.rows = 10, .cols = 30}, 50);
    ASSERT_EQ(1, pager.getPanes());
    ASSERT_EQ(0, pager.getCurRow());
    ASSERT_EQ(0, pager.getIndex());
    ASSERT_EQ(50, pager.getRowRatio());

    const char *expect = "\x1b[7mAAAAAA     (command)    \x1b[0m\r\n"
                         "BBBBBB     (command)    \r\n"
                         "CCCCCC     (command)    \r\n"
                         "DDDDDD     (command)    \r\n"
                         "EEEEEE     (command)    \r\n"
                         "\x1B[7mrows 1-5/6\x1B[0m\r\n";
    std::string out;
    out = this->render(pager);
    ASSERT_EQ(expect, out);
  }

  {
    auto pager = ArrayPager::create(*array, this->ps, {.rows = 10, .cols = 30}, 100);
    ASSERT_EQ(1, pager.getPanes());
    ASSERT_EQ(0, pager.getCurRow());
    ASSERT_EQ(0, pager.getIndex());
    ASSERT_EQ(100, pager.getRowRatio());

    const char *expect = "\x1b[7mAAAAAA     (command)    \x1b[0m\r\nBBBBBB     (command)    "
                         "\r\nCCCCCC     (command)    \r\n"
                         "DDDDDD     (command)    \r\n"
                         "EEEEEE     (command)    \r\n"
                         "FFFFFF     (command)    \r\n";
    std::string out;
    out = this->render(pager);
    ASSERT_EQ(expect, out);
  }

  {
    auto pager = ArrayPager::create(*array, this->ps, {.rows = 9, .cols = 30}, 100);
    ASSERT_EQ(1, pager.getPanes());
    ASSERT_EQ(0, pager.getCurRow());
    ASSERT_EQ(0, pager.getIndex());
    ASSERT_EQ(100, pager.getRowRatio());

    const char *expect = "\x1b[7mAAAAAA     (command)    \x1b[0m\r\nBBBBBB     (command)    "
                         "\r\nCCCCCC     (command)    \r\n"
                         "DDDDDD     (command)    \r\n"
                         "EEEEEE     (command)    \r\n"
                         "FFFFFF     (command)    \r\n";
    std::string out;
    out = this->render(pager);
    ASSERT_EQ(expect, out);
  }

  {
    auto pager = ArrayPager::create(*array, this->ps, {.rows = 8, .cols = 30}, 100);
    ASSERT_EQ(1, pager.getPanes());
    ASSERT_EQ(0, pager.getCurRow());
    ASSERT_EQ(0, pager.getIndex());
    ASSERT_EQ(100, pager.getRowRatio());
    ASSERT_EQ(6, pager.getRenderedRows());

    const char *expect = "\x1b[7mAAAAAA     (command)    \x1b[0m\r\nBBBBBB     (command)    "
                         "\r\nCCCCCC     (command)    \r\n"
                         "DDDDDD     (command)    \r\n"
                         "EEEEEE     (command)    \r\n"
                         "\x1B[7mrows 1-5/6\x1B[0m\r\n";
    std::string out;
    out = this->render(pager);
    ASSERT_EQ(expect, out);
  }
}

TEST_F(PagerTest, desc1) {
  // single pane
  auto array = this->createWith({
      {"AAAAA", "regular file"},
      {"BBBBB", "executable"},
      {"CCCCC", "directory"},
      {"DDD", "named pipe"},
      {"EEEE", ""},
  });
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 24, .cols = 30});
  ASSERT_EQ(1, pager.getPanes());
  ASSERT_EQ(28, pager.getPaneLen());
  pager.setShowCursor(false);

  std::string out = this->render(pager);
  const char *expect = "AAAAA     (regular file)    \r\n"
                       "BBBBB       (executable)    \r\n"
                       "CCCCC        (directory)    \r\n"
                       "DDD         (named pipe)    \r\n"
                       "EEEE                        \r\n";
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, desc2) {
  // multi pane
  auto array = this->createWith({
      {"AAAAA", "regular file"},
      {"BBBBB", "executable"},
      {"CCCCC", "directory"},
      {"DDD", "named pipe"},
      {"EEEE", ""},
  });
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 24, .cols = 60});
  ASSERT_EQ(2, pager.getPanes());
  ASSERT_EQ(28, pager.getPaneLen());
  pager.setShowCursor(false);

  std::string out = this->render(pager);
  const char *expect = "AAAAA     (regular file)    DDD         (named pipe)    \r\n"
                       "BBBBB       (executable)    EEEE                        \r\n"
                       "CCCCC        (directory)    \r\n";
  ASSERT_EQ(expect, out);

  // shrink (hide description)
  pager.updateWinSize({.rows = 24, .cols = 9});
  ASSERT_EQ(1, pager.getPanes());
  ASSERT_EQ(8, pager.getPaneLen());
  out = this->render(pager);
  expect = "AAAAA   \r\n"
           "BBBBB   \r\n"
           "CCCCC   \r\n"
           "DDD     \r\n"
           "EEEE    \r\n";
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, sig) {
  auto array = this->createWith(
      {
          {"OSTYPE", ": String"},
          {"PID", ": Int"},
          {"COMP_HOOK", ": ((Module, [String], Int) -> Candidates?)?"},
      },
      {CandidateAttr::Kind::TYPE_SIGNATURE, false});
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 24, .cols = 80});
  ASSERT_EQ(1, pager.getPanes());
  ASSERT_EQ(56, pager.getPaneLen());
  pager.setShowCursor(false);

  std::string out = this->render(pager);
  const char *expect =
      "OSTYPE\x1b[90m : String                                         \x1b[0m\r\n"
      "PID\x1b[90m : Int                                               \x1b[0m\r\n"
      "COMP_HOOK\x1b[90m : ((Module, [String], Int) -> Candidates?)?   \x1b[0m\r\n";
  ASSERT_EQ(expect, out);

  // shrink (hide signature)
  pager.updateWinSize({.rows = 24, .cols = 14});
  ASSERT_EQ(1, pager.getPanes());
  ASSERT_EQ(12, pager.getPaneLen());
  out = this->render(pager);
  expect = "OSTYPE      \r\n"
           "PID         \r\n"
           "COMP_HOOK   \r\n";
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, candidate) {
  auto obj = this->createWith({});
  ASSERT_EQ(0, obj->size());
  obj->addNewCandidateWith(*this->state, "mkdir", "command",
                           {CandidateAttr::Kind::CMD_EXTERNAL, true});
  obj->addNewCandidateWith(*this->state, "mkdir", "dynamic", {CandidateAttr::Kind::CMD_DYNA, true});
  ASSERT_EQ(2, obj->size());
  obj->sortAndDedup(0);
  ASSERT_EQ(1, obj->size());
  ASSERT_EQ(CandidateAttr::Kind::CMD_DYNA, obj->getAttrAt(0).kind);
}

TEST(HistRotatorTest, base) {
  auto value =
      createObject<ArrayObject>(static_cast<unsigned int>(TYPE::StringArray), std::vector<Value>());
  auto &obj = *value;
  obj.append(Value::createStr("AAA"));
  obj.append(Value::createStr("BBB"));
  obj.append(Value::createStr("CCC"));
  obj.append(Value::createStr("DDD"));
  obj.append(Value::createStr("EEE"));

  HistRotator rotate(value);
  rotate.setMaxSize(4);
  ASSERT_EQ(4, rotate.getMaxSize());

  ASSERT_EQ(6, obj.size());
  ASSERT_EQ("AAA", obj[0].asStrRef().toString());
  ASSERT_EQ("BBB", obj[1].asStrRef().toString());
  ASSERT_EQ("CCC", obj[2].asStrRef().toString());
  ASSERT_EQ("DDD", obj[3].asStrRef().toString());
  ASSERT_EQ("EEE", obj[4].asStrRef().toString());
  ASSERT_EQ("", obj[5].asStrRef().toString()); // reserved for current editing buffer

  // rotate prev
  StringRef ref = "@@@"; // current editing content
  bool r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  ASSERT_EQ(4, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
  ASSERT_EQ("@@@", obj[3].asStrRef().toString());
  ASSERT_EQ("EEE", ref.toString());

  // rotate next
  r = rotate.rotate(ref, HistRotator::Op::NEXT);
  ASSERT_TRUE(r);
  ASSERT_EQ(4, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
  ASSERT_EQ("@@@", obj[3].asStrRef().toString());
  ASSERT_EQ("@@@", ref.toString());

  // rotate next
  r = rotate.rotate(ref, HistRotator::Op::NEXT);
  ASSERT_FALSE(r);
  ASSERT_EQ(4, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
  ASSERT_EQ("@@@", obj[3].asStrRef().toString());
  ASSERT_EQ("@@@", ref.toString());

  // rotate next
  r = rotate.rotate(ref, HistRotator::Op::NEXT);
  ASSERT_FALSE(r);
  ASSERT_EQ(4, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
  ASSERT_EQ("@@@", obj[3].asStrRef().toString());
  ASSERT_EQ("@@@", ref.toString());

  // rotate prev+prev
  ref = "$$$$";
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  ASSERT_EQ(4, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
  ASSERT_EQ("$$$$", obj[3].asStrRef().toString());
  ASSERT_EQ("DDD", ref.toString());

  // rotate prev
  ref = "&&&&";
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  ASSERT_EQ(4, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("&&&&", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
  ASSERT_EQ("$$$$", obj[3].asStrRef().toString());
  ASSERT_EQ("CCC", ref.toString());

  // rotate prev
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_FALSE(r);
  ASSERT_EQ(4, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("&&&&", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
  ASSERT_EQ("$$$$", obj[3].asStrRef().toString());
  ASSERT_EQ("CCC", ref.toString());

  // rotate prev
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_FALSE(r);
  ASSERT_EQ(4, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("&&&&", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
  ASSERT_EQ("$$$$", obj[3].asStrRef().toString());
  ASSERT_EQ("CCC", ref.toString());

  // revert
  rotate.revertAll();
  ASSERT_EQ(3, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
}

TEST(HistRotatorTest, broken1) {
  auto value =
      createObject<ArrayObject>(static_cast<unsigned int>(TYPE::StringArray), std::vector<Value>());
  auto &obj = *value;
  obj.append(Value::createStr("AAA"));
  obj.append(Value::createStr("BBB"));
  obj.append(Value::createStr("CCC"));
  obj.append(Value::createStr("DDD"));
  obj.append(Value::createStr("EEE"));

  HistRotator rotate(value);
  rotate.setMaxSize(4);
  ASSERT_EQ(4, rotate.getMaxSize());

  // rotate prev
  StringRef ref = "@@@"; // current editing content
  bool r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  ASSERT_EQ(4, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
  ASSERT_EQ("@@@", obj[3].asStrRef().toString());
  ASSERT_EQ("EEE", ref);

  obj.erase(obj.begin() + 1);
  ref = "&&&";
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  ASSERT_EQ(3, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("&&&", obj[1].asStrRef().toString());
  ASSERT_EQ("@@@", obj[2].asStrRef().toString());
  ASSERT_EQ("CCC", ref);

  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_FALSE(r);

  obj.clear();
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_FALSE(r);

  // revert
  rotate.revertAll();
  ASSERT_EQ(0, obj.size());
}

TEST(HistRotator, broken2) {
  auto value =
      createObject<ArrayObject>(static_cast<unsigned int>(TYPE::StringArray), std::vector<Value>());
  auto &obj = *value;
  obj.append(Value::createStr("AAA"));
  obj.append(Value::createStr("BBB"));
  obj.append(Value::createStr("CCC"));
  obj.append(Value::createStr("DDD"));
  obj.append(Value::createStr("EEE"));

  HistRotator rotate(value);
  rotate.setMaxSize(4);
  ASSERT_EQ(4, rotate.getMaxSize());

  // rotate prev
  StringRef ref = "@@@"; // current editing content
  bool r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  ASSERT_EQ(4, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
  ASSERT_EQ("@@@", obj[3].asStrRef().toString());
  ASSERT_EQ("EEE", ref);

  // remove history and rotate prev
  ref = "%%%";
  obj.erase(obj.begin(), obj.begin() + 2);
  ASSERT_EQ(2, obj.size());
  ASSERT_EQ("EEE", obj[0].asStrRef().toString());
  ASSERT_EQ("@@@", obj[1].asStrRef().toString());

  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_FALSE(r);
  ASSERT_EQ(2, obj.size());
  ASSERT_EQ("EEE", obj[0].asStrRef().toString());
  ASSERT_EQ("@@@", obj[1].asStrRef().toString());
  ASSERT_EQ("%%%", ref);

  rotate.revertAll();
  ASSERT_EQ(1, obj.size());
  ASSERT_EQ("EEE", obj[0].asStrRef().toString());
}

class ScrollTest : public PagerTest {
protected:
  WinSize winSize;
  std::string buf;
  RenderingContext ctx;

public:
  ScrollTest() : buf(4096, '\0'), ctx(this->buf.data(), this->buf.size(), "> ", nullptr) {}

  void setRows(unsigned short rows) { this->winSize.rows = rows; }

  void setCols(unsigned short cols) { this->winSize.cols = cols; }

  LineBuffer &getLineBuffer() { return this->ctx.buf; }

  RenderingResult render(const ObserverPtr<const ArrayPager> pager = nullptr) {
    this->ctx.buf.syncNewlinePosList();
    return doRendering(this->ctx, pager, nullptr, this->winSize.cols);
  }

  bool fit(RenderingResult &result, bool showPager = false) {
    const unsigned int actualCursorRows = result.cursorRows;
    const bool r = fitToWinSize(ctx, showPager, this->winSize.rows, result);
    this->ctx.scrolling = r;
    this->ctx.oldActualCursorRows = actualCursorRows;
    this->ctx.oldCursorRows = result.cursorRows;
    this->ctx.oldRenderedCols = result.renderedCols;
    return r;
  }
};

TEST_F(ScrollTest, base) {
  const char *lines = "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12";
  auto &lineBuf = getLineBuffer();
  lineBuf.insertToCursor(lines);
  this->setRows(5);

  {
    auto ret = render();
    ASSERT_EQ(12, ret.renderedRows);
    ASSERT_EQ(12, ret.cursorRows);
    ASSERT_GE(ret.renderedRows, this->winSize.rows);
    ASSERT_EQ(4, ret.renderedCols);

    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(5, ret.renderedRows);
    ASSERT_EQ(5, ret.cursorRows);
    ASSERT_EQ("  08\r\n  09\r\n  10\r\n  11\r\n  12", ret.renderedLines);
  }

  // move cursor (but not up/down)
  {
    lineBuf.moveCursorToStartOfLine();
    ASSERT_EQ(lineBuf.getUsedSize() - 2, lineBuf.getCursor());
    auto ret = render();
    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(5, ret.renderedRows);
    ASSERT_EQ(5, ret.cursorRows);
    ASSERT_EQ("  08\r\n  09\r\n  10\r\n  11\r\n  12", ret.renderedLines);
  }

  // move cursor up
  {
    lineBuf.moveCursorUpDown(true);
    auto ret = render();
    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(5, ret.renderedRows);
    ASSERT_EQ(4, ret.cursorRows);
    ASSERT_EQ("  08\r\n  09\r\n  10\r\n  11\r\n  12", ret.renderedLines);
  }

  // move cursor up * 3
  {
    lineBuf.moveCursorUpDown(true);
    lineBuf.moveCursorUpDown(true);
    lineBuf.moveCursorUpDown(true);
    auto ret = render();
    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(5, ret.renderedRows);
    ASSERT_EQ(1, ret.cursorRows);
    ASSERT_EQ("  08\r\n  09\r\n  10\r\n  11\r\n  12", ret.renderedLines);
  }

  // move cursor (but not up/down)
  {
    lineBuf.moveCursorToEndOfLine();
    auto ret = render();
    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(5, ret.renderedRows);
    ASSERT_EQ(1, ret.cursorRows);
    ASSERT_EQ("  08\r\n  09\r\n  10\r\n  11\r\n  12", ret.renderedLines);
  }

  // move cursor up (scroll up)
  {
    lineBuf.moveCursorUpDown(true);
    auto ret = render();
    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(5, ret.renderedRows);
    ASSERT_EQ(1, ret.cursorRows);
    ASSERT_EQ("  07\r\n  08\r\n  09\r\n  10\r\n  11", ret.renderedLines);
  }

  // move cursor up (scroll up)
  {
    lineBuf.moveCursorUpDown(true);
    auto ret = render();
    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(5, ret.renderedRows);
    ASSERT_EQ(1, ret.cursorRows);
    ASSERT_EQ("  06\r\n  07\r\n  08\r\n  09\r\n  10", ret.renderedLines);
  }

  // move cursor down (not scroll down)
  {
    lineBuf.moveCursorUpDown(false);
    auto ret = render();
    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(5, ret.renderedRows);
    ASSERT_EQ(2, ret.cursorRows);
    ASSERT_EQ("  06\r\n  07\r\n  08\r\n  09\r\n  10", ret.renderedLines);
  }

  // move cursor begin
  {
    lineBuf.moveCursorToStartOfBuf();
    ASSERT_EQ(0, lineBuf.getCursor());
    auto ret = render();
    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(5, ret.renderedRows);
    ASSERT_EQ(1, ret.cursorRows);
    ASSERT_EQ("> 01\r\n  02\r\n  03\r\n  04\r\n  05", ret.renderedLines);
  }

  // move cursor down * 6 (scroll down)
  {
    for (unsigned int i = 0; i < 6; i++) {
      lineBuf.moveCursorUpDown(false);
    }
    auto ret = render();
    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(5, ret.renderedRows);
    ASSERT_EQ(5, ret.cursorRows);
    ASSERT_EQ("  03\r\n  04\r\n  05\r\n  06\r\n  07", ret.renderedLines);
  }

  // move cursor end
  {
    lineBuf.moveCursorToEndOfBuf();
    ASSERT_EQ(lineBuf.getUsedSize(), lineBuf.getCursor());
    auto ret = render();
    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(5, ret.renderedRows);
    ASSERT_EQ(5, ret.cursorRows);
    ASSERT_EQ("  08\r\n  09\r\n  10\r\n  11\r\n  12", ret.renderedLines);
  }
}

TEST_F(ScrollTest, expandRows1) {
  const char *lines = "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12";
  auto &lineBuf = getLineBuffer();
  lineBuf.insertToCursor(lines);
  lineBuf.syncNewlinePosList();
  lineBuf.moveCursorUpDown(true);
  lineBuf.moveCursorUpDown(true);
  this->setRows(5);

  {
    auto ret = render();
    ASSERT_EQ(12, ret.renderedRows);
    ASSERT_EQ(10, ret.cursorRows);
    ASSERT_GE(ret.renderedRows, this->winSize.rows);

    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(5, ret.renderedRows);
    ASSERT_EQ(3, ret.cursorRows);
    ASSERT_EQ("  08\r\n  09\r\n  10\r\n  11\r\n  12", ret.renderedLines);
  }

  // expand rows
  {
    this->setRows(7);
    auto ret = render();
    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(7, ret.renderedRows);
    ASSERT_EQ(5, ret.cursorRows);
    ASSERT_EQ("  06\r\n  07\r\n  08\r\n  09\r\n  10\r\n  11\r\n  12", ret.renderedLines);
  }

  // remove line
  {
    lineBuf.deleteLineToCursor(true, nullptr);
    lineBuf.deletePrevChar(nullptr);
    auto ret = render();
    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(7, ret.renderedRows);
    ASSERT_EQ(5, ret.cursorRows);
    ASSERT_EQ("  05\r\n  06\r\n  07\r\n  08\r\n  09\r\n  11\r\n  12", ret.renderedLines);
  }

  // remove lines
  {
    for (unsigned int i = 0; i < 4; i++) {
      lineBuf.deleteLineToCursor(true, nullptr);
      lineBuf.deletePrevChar(nullptr);
    }
    auto ret = render();
    ASSERT_FALSE(this->fit(ret));
    ASSERT_EQ(7, ret.renderedRows);
    ASSERT_EQ(5, ret.cursorRows);
    ASSERT_EQ("> 01\r\n  02\r\n  03\r\n  04\r\n  05\r\n  11\r\n  12", ret.renderedLines);
  }

  // remove line (shrink scroll window)
  {
    lineBuf.deleteLineToCursor(true, nullptr);
    lineBuf.deletePrevChar(nullptr);
    auto ret = render();
    ASSERT_FALSE(this->fit(ret));
    ASSERT_EQ(6, ret.renderedRows);
    ASSERT_EQ(4, ret.cursorRows);
    ASSERT_EQ("> 01\r\n  02\r\n  03\r\n  04\r\n  11\r\n  12", ret.renderedLines);
  }
}

TEST_F(ScrollTest, expandRows2) {
  const char *lines = "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13";
  auto &lineBuf = getLineBuffer();
  lineBuf.insertToCursor(lines);
  lineBuf.syncNewlinePosList();
  for (unsigned int i = 0; i < 5; i++) {
    lineBuf.moveCursorUpDown(true);
  }
  this->setRows(4);

  {
    auto ret = render();
    ASSERT_EQ(13, ret.renderedRows);
    ASSERT_EQ(8, ret.cursorRows);
    ASSERT_GE(ret.renderedRows, this->winSize.rows);

    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(4, ret.renderedRows);
    ASSERT_EQ(4, ret.cursorRows);
    ASSERT_EQ("  05\r\n  06\r\n  07\r\n  08", ret.renderedLines);
  }

  {
    this->setRows(20);
    auto ret = render();
    ASSERT_FALSE(this->fit(ret));
    ASSERT_EQ(13, ret.renderedRows);
    ASSERT_EQ(8, ret.cursorRows);
    ASSERT_EQ("> 01\r\n  02\r\n  03\r\n  04\r\n  05\r\n  06\r\n"
              "  07\r\n  08\r\n  09\r\n  10\r\n  11\r\n  12\r\n  13",
              ret.renderedLines);
  }
}

TEST_F(ScrollTest, shrinkRows) {
  const char *lines = "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12";
  auto &lineBuf = getLineBuffer();
  lineBuf.insertToCursor(lines);
  lineBuf.syncNewlinePosList();
  lineBuf.moveCursorToStartOfBuf();
  lineBuf.moveCursorUpDown(false);
  this->setRows(5);

  {
    auto ret = render();
    ASSERT_EQ(12, ret.renderedRows);
    ASSERT_EQ(2, ret.cursorRows);
    ASSERT_GE(ret.renderedRows, this->winSize.rows);

    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(5, ret.renderedRows);
    ASSERT_EQ(2, ret.cursorRows);
    ASSERT_EQ("> 01\r\n  02\r\n  03\r\n  04\r\n  05", ret.renderedLines);
  }

  // shrink rows
  {
    this->setRows(3);
    auto ret = render();
    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(3, ret.renderedRows);
    ASSERT_EQ(2, ret.cursorRows);
    ASSERT_EQ("> 01\r\n  02\r\n  03", ret.renderedLines);
  }

  // down * 2
  {
    lineBuf.moveCursorUpDown(false);
    lineBuf.moveCursorUpDown(false);
    auto ret = render();
    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(3, ret.renderedRows);
    ASSERT_EQ(3, ret.cursorRows);
    ASSERT_EQ("  02\r\n  03\r\n  04", ret.renderedLines);
  }
}

TEST_F(ScrollTest, softwrap) {
  const char *lines = "011111\n022222\n033333\n044444\n055555\n"
                      "066666\n077777\n088888\n099999\n100000";
  auto &lineBuf = getLineBuffer();
  lineBuf.insertToCursor(lines);
  lineBuf.syncNewlinePosList();
  this->setRows(5);
  this->setCols(5);

  {
    auto ret = render();
    ASSERT_EQ(20, ret.renderedRows);
    ASSERT_EQ(20, ret.cursorRows);
    ASSERT_GE(ret.renderedRows, this->winSize.rows);
    ASSERT_EQ(5, ret.renderedCols);
    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(5, ret.renderedRows);
    ASSERT_EQ(5, ret.cursorRows);
    ASSERT_EQ("888\r\n  099\r\n999\r\n  100\r\n000", ret.renderedLines);
  }

  // move cursor left
  {
    for (unsigned int i = 0; i < 5; i++) {
      lineBuf.moveCursorToLeftByChar();
    }
    auto ret = render();
    ASSERT_EQ(20, ret.renderedRows);
    ASSERT_EQ(19, ret.cursorRows);
    ASSERT_GE(ret.renderedRows, this->winSize.rows);
    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(5, ret.renderedRows);
    ASSERT_EQ(4, ret.cursorRows);
    ASSERT_EQ("888\r\n  099\r\n999\r\n  100\r\n000", ret.renderedLines);
  }

  // move cursor up
  {
    lineBuf.moveCursorUpDown(true);
    auto ret = render();
    ASSERT_EQ(20, ret.renderedRows);
    ASSERT_EQ(17, ret.cursorRows);
    ASSERT_GE(ret.renderedRows, this->winSize.rows);
    ASSERT_TRUE(this->fit(ret));
    ASSERT_EQ(5, ret.renderedRows);
    ASSERT_EQ(2, ret.cursorRows);
    ASSERT_EQ("888\r\n  099\r\n999\r\n  100\r\n000", ret.renderedLines);
  }
}

TEST_F(ScrollTest, pager) {
  const char *lines = "011111\n022222\n033333\n044444\n055555\n"
                      "066666\n077777\n088888\n099999\n100000";
  auto &lineBuf = getLineBuffer();
  lineBuf.insertToCursor(lines);
  lineBuf.syncNewlinePosList();
  this->setRows(5);

  auto array = this->createWith({
      {"AAAAA", "regular file"},
      {"BBBBB", "executable"},
      {"CCCCC", "directory"},
      {"DDD", "named pipe"},
      {"EEEE", ""},
  });
  auto pager = ArrayPager::create(*array, this->ps,
                                  {.rows = this->winSize.rows, .cols = this->winSize.cols});
  ASSERT_EQ(2, pager.getPanes());
  ASSERT_EQ(28, pager.getPaneLen());
  ASSERT_EQ(3, pager.getRenderedRows());
  pager.setShowCursor(false);

  {
    auto ret = render(makeObserver(pager));
    ASSERT_EQ(14, ret.renderedRows);
    ASSERT_EQ(10, ret.cursorRows);
    ASSERT_GE(ret.renderedRows, this->winSize.rows);
    ASSERT_TRUE(this->fit(ret, true));
    ASSERT_EQ(5, ret.renderedRows);
    ASSERT_EQ(1, ret.cursorRows);
    ASSERT_EQ("  100000\r\n"
              "AAAAA     (regular file)    DDD         (named pipe)    \r\n"
              "BBBBB       (executable)    EEEE                        \r\n"
              "\x1b[7mrows 1-2/3\x1b[0m\r\n",
              ret.renderedLines);
  }

  // resize
  {
    this->setRows(10);
    auto ret = render(makeObserver(pager));
    ASSERT_EQ(14, ret.renderedRows);
    ASSERT_EQ(10, ret.cursorRows);
    ASSERT_GE(ret.renderedRows, this->winSize.rows);
    ASSERT_TRUE(this->fit(ret, true));
    ASSERT_EQ(10, ret.renderedRows);
    ASSERT_EQ(6, ret.cursorRows);
    ASSERT_EQ("  055555\r\n  066666\r\n  077777\r\n  088888\r\n  099999\r\n  100000\r\n"
              "AAAAA     (regular file)    DDD         (named pipe)    \r\n"
              "BBBBB       (executable)    EEEE                        \r\n"
              "\x1b[7mrows 1-2/3\x1b[0m\r\n",
              ret.renderedLines);
  }
}

TEST_F(ScrollTest, semanticPrompt1) {
  const char *lines = "011111\n022222\n033333\n044444\n055555\n"
                      "066666\n077777\n088888\n099999\n100000";
  auto &lineBuf = getLineBuffer();
  lineBuf.insertToCursor(lines);
  lineBuf.syncNewlinePosList();
  this->setRows(5);
  this->setCols(10);
  this->ctx.semanticPrompt = true;

  // remove lower rows
  {
    lineBuf.moveCursorToStartOfBuf();
    auto ret = render();
    this->fit(ret, false);
    ASSERT_EQ(1, ret.promptRows);
    ASSERT_EQ("> \x1b]133;B\x1b\\011111\r\n"
              "  022222\r\n"
              "  033333\r\n"
              "  044444\r\n"
              "  055555",
              ret.renderedLines);
  }

  // remove upper rows
  {
    lineBuf.moveCursorToEndOfBuf();
    auto ret = render();
    this->fit(ret, false);
    ASSERT_EQ(1, ret.promptRows);
    ASSERT_EQ("  066666\r\n"
              "  077777\r\n"
              "  088888\r\n"
              "  099999\r\n"
              "  100000",
              ret.renderedLines);
  }

  // remove upper and lower rows
  {
    for (unsigned int i = 0; i < 7; i++) {
      lineBuf.moveCursorUpDown(true);
    }
    auto ret = render();
    this->fit(ret, false);
    ASSERT_EQ(1, ret.promptRows);
    ASSERT_EQ("  033333\r\n"
              "  044444\r\n"
              "  055555\r\n"
              "  066666\r\n"
              "  077777",
              ret.renderedLines);
  }
}

TEST_F(ScrollTest, semanticPrompt2) {
  const char *lines = "011111\n022222\n033333\n044444\n055555\n"
                      "066666\n077777\n088888\n099999\n100000";
  auto &lineBuf = getLineBuffer();
  lineBuf.insertToCursor(lines);
  lineBuf.syncNewlinePosList();
  this->setRows(5);
  this->setCols(10);
  this->ctx.semanticPrompt = true;
  const_cast<StringRef &>(this->ctx.prompt) = "1>\n2>\n3>\n> ";

  // remove upper rows (trim prompt)
  {
    lineBuf.moveCursorToStartOfBuf();
    lineBuf.moveCursorUpDown(false);
    lineBuf.moveCursorUpDown(false);
    auto ret = render();
    ASSERT_EQ(4, ret.promptRows);
    ASSERT_EQ(6, ret.cursorRows);
    this->fit(ret, false);
    ASSERT_EQ(3, ret.promptRows);
    ASSERT_EQ("2>\r\n"
              "3>\r\n"
              "> \x1b]133;B\x1b\\011111\r\n"
              "  022222\r\n"
              "  033333",
              ret.renderedLines);
  }
}

struct Deleter {
  void operator()(ARState *state) const { ARState_delete(&state); }
};

using ARStateHandle = std::unique_ptr<ARState, Deleter>;

TEST(ErrorHighlightTest, path) {
  auto state = ARStateHandle(ARState_create());
  PathLikeChecker checker(*state);

  ASSERT_FALSE(checker(""));
  ASSERT_FALSE(checker("fjaeirfj fjaeirjfa127419234"));
  ASSERT_FALSE(checker("fjaeirfj fjaeirjfa127419234"));
  ASSERT_TRUE(checker("."));
  ASSERT_TRUE(checker(".."));
  ASSERT_TRUE(checker("./././././"));
  ASSERT_TRUE(checker("../../..///////"));
  ASSERT_FALSE(checker("..."));
  ASSERT_TRUE(checker("ps"));
  ASSERT_TRUE(checker("shctl"));
  ASSERT_TRUE(checker("/usr/bin/env"));
  ASSERT_FALSE(checker("/fjirfa/fjaeirj/23453"));
  ASSERT_FALSE(checker("/fjirfa/fjaeirj/23453")); // use cache
}

TEST(ErrorHighlightTest, base) {
  auto state = ARStateHandle(ARState_create());
  ANSIEscapeSeqMap seqMap({
      {HighlightTokenClass::COMMAND, "\x1b[30m"},
      {HighlightTokenClass::COMMAND_ARG, "\x1b[40m"},
      {HighlightTokenClass::ERROR_, "\x1b[50m"},
      {HighlightTokenClass::COMMENT, "\x1b[45m"},
  });

  std::string buf;
  buf.resize(32);
  PathLikeChecker checker(*state);
  RenderingContext ctx(buf.data(), buf.size(), "> ", std::ref(checker));

  {
    ctx.buf.insertToCursor("slsss -la");
    auto ret = doRendering(ctx, nullptr, makeObserver(seqMap), 100);
    ASSERT_EQ("> \x1b[50mslsss\x1b[0m \x1b[40m-la\x1b[0m", ret.renderedLines);
  }

  { // no highlight (user-defined definition)
    ctx.buf.deleteAll();
    ctx.buf.insertToCursor("slsss () {}");
    auto ret = doRendering(ctx, nullptr, makeObserver(seqMap), 100);
    ASSERT_EQ("> \x1b[30mslsss\x1b[0m () {}", ret.renderedLines);
  }

  { // no highlight (user-defined definition)
    ctx.buf.deleteAll();
    ctx.buf.insertToCursor("slsss \\\n \\\n () {}");
    auto ret = doRendering(ctx, nullptr, makeObserver(seqMap), 100);
    ASSERT_EQ("> \x1b[30mslsss\x1b[0m \\\r\n   \\\r\n   () {}", ret.renderedLines);
  }

  { // no highlight (user-defined definition)
    ctx.buf.deleteAll();
    ctx.buf.insertToCursor("(slsss \\\n \n () {})");
    auto ret = doRendering(ctx, nullptr, makeObserver(seqMap), 100);
    ASSERT_EQ("> (\x1b[30mslsss\x1b[0m \\\r\n   \r\n   () {})", ret.renderedLines);
  }

  { // no highlight (user-defined definition)
    ctx.buf.deleteAll();
    ctx.buf.insertToCursor("(slsss # this\n () {})");
    auto ret = doRendering(ctx, nullptr, makeObserver(seqMap), 100);
    ASSERT_EQ("> (\x1b[30mslsss\x1b[0m \x1b[45m# this\x1b[0m\r\n   () {})", ret.renderedLines);
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}