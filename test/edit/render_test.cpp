#include "gtest/gtest.h"

#include "../test_common.h"

#include <line_renderer.h>
#include <comp_candidates.h>
#include <object.h>
#include <type_pool.h>

using namespace arsh;

struct Len {
  unsigned int byteSize;
  unsigned int colSize;
};

static Len getCharLen(StringRef line, const CharWidthProperties &ps, unsigned int initCols = 0) {
  Len len{0, 0};
  iterateGraphemeUntil(line, 1, [&](const GraphemeCluster &grapheme) {
    LineRenderer renderer(ps, initCols);
    renderer.setLineNumLimit(0);
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

TEST_F(LineRendererTest, limit) {
  CharWidthProperties ps;
  std::string out;
  StringRef line = "111\r\n\r222\n333\n444\n555\n666";
  {
    LineRenderer renderer(ps, 0, out);
    renderer.setLineNumLimit(2);
    renderer.renderLines(line);
    ASSERT_EQ(2, renderer.getTotalRows());
    ASSERT_EQ(5, renderer.getTotalCols());
  }
  ASSERT_EQ("111^M\r\n^M222\r\n", out);

  out.clear();
  {
    LineRenderer renderer(ps, 0, out); // no limit
    renderer.renderLines(line);
    ASSERT_EQ(5, renderer.getTotalRows());
    ASSERT_EQ(3, renderer.getTotalCols());
  }
  ASSERT_EQ("111^M\r\n^M222\r\n333\r\n444\r\n555\r\n666", out);

  out.clear();
  line = "\x1b[40m111\n222\n33\x1b[40m3\n44\x1b[40m4\n555\n666";
  {
    LineRenderer renderer(ps, 0, out);
    renderer.setLineNumLimit(3);
    renderer.renderWithANSI(line);
    ASSERT_EQ(3, renderer.getTotalRows());
    ASSERT_EQ(3, renderer.getTotalCols());
  }
  ASSERT_EQ("\x1b[40m111\r\n222\r\n33\x1b[40m3\r\n", out);

  out.clear();
  line = "echo 111\necho 222\necho 333\necho 444";
  {
    ANSIEscapeSeqMap seqMap({
        {HighlightTokenClass::COMMAND, "\x1b[30m"},
        {HighlightTokenClass::COMMAND_ARG, "\x1b[40m"},
    });
    LineRenderer renderer(ps, 0, out, makeObserver(seqMap));
    renderer.setLineNumLimit(2);
    bool r = renderer.renderScript(line);
    ASSERT_TRUE(r);
    ASSERT_EQ(2, renderer.getTotalRows());
    ASSERT_EQ(8, renderer.getTotalCols());
  }
  ASSERT_EQ("\x1b[30mecho\x1b[0m \x1b[40m111\x1b[0m\r\n\x1b[30mecho\x1b[0m \x1b[40m222\x1b[0m\r\n",
            out);

  out.clear();
  line = "\necho 222\necho 333\necho 444";
  {
    ANSIEscapeSeqMap seqMap({
        {HighlightTokenClass::COMMAND, "\x1b[30m"},
        {HighlightTokenClass::COMMAND_ARG, "\x1b[40m"},
        {HighlightTokenClass::NONE, "\x1b[50m"},
    });
    LineRenderer renderer(ps, 0, out, makeObserver(seqMap));
    renderer.setLineNumLimit(2);
    bool r = renderer.renderScript(line);
    ASSERT_TRUE(r);
    ASSERT_EQ(2, renderer.getTotalRows());
    ASSERT_EQ(8, renderer.getTotalCols());
  }
  ASSERT_EQ("\x1b[50m\x1b[0m\r\n\x1b[50m\x1b[0m\x1b[30mecho\x1b[0m\x1b[50m "
            "\x1b[0m\x1b[40m222\x1b[0m\x1b[50m\x1b[0m\r\n",
            out);

  out.clear();
  line = "echo 111\necho 222\necho 333\necho 444";
  {
    LineRenderer renderer(ps, 2, out);
    renderer.setLineNumLimit(0);
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
    renderer.setMaxCols(5);
    renderer.renderLines(line);
    ASSERT_EQ(2, renderer.getTotalRows());
    ASSERT_EQ(4, renderer.getTotalCols());
  }
  ASSERT_EQ("  1\r\n23456\r\n7890", out);

  out = "";
  line = "\t1234567890あab\r\n@";
  {
    LineRenderer renderer(ps, 3, out);
    renderer.setMaxCols(5);
    renderer.renderLines(line);
    ASSERT_EQ(5, renderer.getTotalRows());
    ASSERT_EQ(4, renderer.getTotalCols());
  }
  ASSERT_EQ(" 1\r\n23456\r\n7890\r\nあab\r\n^M\r\n   @", out);

  out = "";
  line = "1234\t@ \r";
  {
    LineRenderer renderer(ps, 0, out);
    renderer.setMaxCols(4);
    renderer.renderLines(line);
    ASSERT_EQ(3, renderer.getTotalRows());
    ASSERT_EQ(0, renderer.getTotalCols());
  }
  ASSERT_EQ("1234\r\n    \r\n@ ^M\r\n", out);
}

static void append(ArrayObject &) {}

template <typename... T>
static void append(ArrayObject &obj, const char *first, T &&...remain) {
  obj.refValues().push_back(Value::createStr(first));
  append(obj, std::forward<T>(remain)...);
}

class PagerTest : public ExpectOutput {
protected:
  TypePool pool;
  CharWidthProperties ps;

public:
  PagerTest() { this->ps.replaceInvalid = true; }

  template <typename... T>
  ObjPtr<ArrayObject> create(T &&...args) const {
    auto v = Value::create<ArrayObject>(this->pool.get(TYPE::StringArray));
    auto &obj = typeAs<ArrayObject>(v);
    append(obj, std::forward<T>(args)...);
    return toObjPtr<ArrayObject>(v);
  }
};

TEST_F(PagerTest, small1) { // less than pager length
  auto array = this->create("AAA", "BBB", "CCC", "DDD", "EEE", "FFF");
  auto pager = CompletionPager::create(*array, this->ps, {.rows = 24, .cols = 10});
  ASSERT_EQ(2, pager.getPanes());
  ASSERT_TRUE(pager.getRows() < pager.getWinSize().rows);
  ASSERT_TRUE(pager.getRows() > array->size() / 2);
  ASSERT_EQ(0, pager.getCurRow());

  const char *expect = "\x1b[7mAAA \x1b[0mDDD \r\nBBB EEE \r\nCCC FFF \r\n";
  std::string out;
  pager.render(out);
  ASSERT_EQ(expect, out);

  // cursor up
  out = "";
  expect = "AAA DDD \r\nBBB EEE \r\nCCC \x1b[7mFFF \x1b[0m\r\n";
  pager.moveCursorToForward();
  ASSERT_EQ(2, pager.getCurRow());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // cursor up
  out = "";
  expect = "AAA DDD \r\nBBB \x1b[7mEEE \x1b[0m\r\nCCC FFF \r\n";
  pager.moveCursorToForward();
  ASSERT_EQ(1, pager.getCurRow());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // cursor up+up
  out = "";
  expect = "AAA DDD \r\nBBB EEE \r\n\x1b[7mCCC \x1b[0mFFF \r\n";
  pager.moveCursorToForward();
  pager.moveCursorToForward();
  ASSERT_EQ(2, pager.getCurRow());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // cursor down+down+down+down
  out = "";
  expect = "\x1b[7mAAA \x1b[0mDDD \r\nBBB EEE \r\nCCC FFF \r\n";
  pager.moveCursorToNext();
  pager.moveCursorToNext();
  pager.moveCursorToNext();
  pager.moveCursorToNext();
  ASSERT_EQ(0, pager.getCurRow());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // cursor down
  out = "";
  expect = "AAA DDD \r\n\x1b[7mBBB \x1b[0mEEE \r\nCCC FFF \r\n";
  pager.moveCursorToNext();
  ASSERT_EQ(1, pager.getCurRow());
  pager.render(out);
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, small2) { // less than pager length
  auto array = this->create("AAA", "BBB", "CCC", "DDD", "EEE", "FFF");
  auto pager = CompletionPager::create(*array, this->ps, {.rows = 24, .cols = 10});
  ASSERT_EQ(2, pager.getPanes());

  const char *expect = "\x1b[7mAAA \x1b[0mDDD \r\nBBB EEE \r\nCCC FFF \r\n";
  std::string out;
  pager.render(out);
  ASSERT_EQ(expect, out);

  // left
  out = "";
  expect = "AAA DDD \r\nBBB EEE \r\nCCC \x1b[7mFFF \x1b[0m\r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(2, pager.getCurRow());
  ASSERT_EQ(5, pager.getIndex());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // left
  out = "";
  expect = "AAA DDD \r\nBBB EEE \r\n\x1b[7mCCC \x1b[0mFFF \r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(2, pager.getCurRow());
  ASSERT_EQ(2, pager.getIndex());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // left
  out = "";
  expect = "AAA DDD \r\nBBB \x1b[7mEEE \x1b[0m\r\nCCC FFF \r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(1, pager.getCurRow());
  ASSERT_EQ(4, pager.getIndex());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // right
  out = "";
  expect = "AAA DDD \r\nBBB EEE \r\n\x1b[7mCCC \x1b[0mFFF \r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(2, pager.getCurRow());
  ASSERT_EQ(2, pager.getIndex());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // right
  out = "";
  expect = "AAA DDD \r\nBBB EEE \r\nCCC \x1b[7mFFF \x1b[0m\r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(2, pager.getCurRow());
  ASSERT_EQ(5, pager.getIndex());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // right
  out = "";
  expect = "\x1b[7mAAA \x1b[0mDDD \r\nBBB EEE \r\nCCC FFF \r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(0, pager.getCurRow());
  ASSERT_EQ(0, pager.getIndex());
  pager.render(out);
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, small3) { // less than pager length
  auto array = this->create("AAAAA", "BBBBB", "CCCCC", "DDDDD", "EEEEE");
  auto pager = CompletionPager::create(*array, this->ps, {.rows = 24, .cols = 20});
  ASSERT_EQ(2, pager.getPanes());

  /**
   * AAAAA   DDDDD
   * BBBBB   EEEEE
   * CCCCC
   */
  const char *expect = "\x1b[7mAAAAA   \x1b[0mDDDDD   \r\nBBBBB   EEEEE   \r\nCCCCC   \r\n";
  std::string out;
  pager.render(out);
  ASSERT_EQ(expect, out);

  // prev
  out = "";
  expect = "AAAAA   DDDDD   \r\nBBBBB   \x1b[7mEEEEE   \x1b[0m\r\nCCCCC   \r\n";
  pager.moveCursorToForward();
  ASSERT_EQ(1, pager.getCurRow());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // right
  out = "";
  expect = "AAAAA   DDDDD   \r\nBBBBB   EEEEE   \r\n\x1b[7mCCCCC   \x1b[0m\r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(2, pager.getCurRow());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // right
  out = "";
  expect = "\x1b[7mAAAAA   \x1b[0mDDDDD   \r\nBBBBB   EEEEE   \r\nCCCCC   \r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(0, pager.getCurRow());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // left
  out = "";
  expect = "AAAAA   DDDDD   \r\nBBBBB   EEEEE   \r\n\x1b[7mCCCCC   \x1b[0m\r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(2, pager.getCurRow());
  pager.render(out);
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, large1) { // larger than pager length
  auto array = this->create("AAA", "BBB", "CC\nC", "DDD", "EEE", "FFF", "GG\t", "HHH");
  auto pager = CompletionPager::create(*array, this->ps, {.rows = 5, .cols = 20});
  ASSERT_EQ(2, pager.getPanes());
  ASSERT_TRUE(pager.getRows() < array->size() / 2);
  ASSERT_EQ(0, pager.getCurRow());

  const char *expect = "\x1b[7mAAA     \x1b[0mEEE     \r\nBBB     FFF     \r\n"
                       "\x1b[7mrows 1-2/4\x1b[0m\r\n";
  std::string out;
  pager.render(out);
  ASSERT_EQ(expect, out);

  // up
  out = "";
  expect = "CCC     GG      \r\nDDD     \x1b[7mHHH     \x1b[0m\r\n"
           "\x1b[7mrows 3-4/4\x1b[0m\r\n";
  pager.moveCursorToForward();
  ASSERT_EQ(1, pager.getCurRow());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // up
  out = "";
  expect = "CCC     \x1b[7mGG      \x1b[0m\r\nDDD     HHH     \r\n"
           "\x1b[7mrows 3-4/4\x1b[0m\r\n";
  pager.moveCursorToForward();
  ASSERT_EQ(0, pager.getCurRow());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // up
  out = "";
  expect = "BBB     \x1b[7mFFF     \x1b[0m\r\nCCC     GG      \r\n"
           "\x1b[7mrows 2-3/4\x1b[0m\r\n";
  pager.moveCursorToForward();
  ASSERT_EQ(0, pager.getCurRow());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // down
  out = "";
  expect = "BBB     FFF     \r\nCCC     \x1b[7mGG      \x1b[0m\r\n"
           "\x1b[7mrows 2-3/4\x1b[0m\r\n";
  pager.moveCursorToNext();
  ASSERT_EQ(1, pager.getCurRow());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // down
  out = "";
  expect = "CCC     GG      \r\nDDD     \x1b[7mHHH     \x1b[0m\r\n"
           "\x1b[7mrows 3-4/4\x1b[0m\r\n";
  pager.moveCursorToNext();
  ASSERT_EQ(1, pager.getCurRow());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // down
  out = "";
  expect = "\x1b[7mAAA     \x1b[0mEEE     \r\nBBB     FFF     \r\n"
           "\x1b[7mrows 1-2/4\x1b[0m\r\n";
  pager.moveCursorToNext();
  ASSERT_EQ(0, pager.getCurRow());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // down+down+down
  out = "";
  expect = "CCC     GG      \r\n\x1b[7mDDD     \x1b[0mHHH     \r\n"
           "\x1b[7mrows 3-4/4\x1b[0m\r\n";
  pager.moveCursorToNext();
  pager.moveCursorToNext();
  pager.moveCursorToNext();
  ASSERT_EQ(1, pager.getCurRow());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // down
  out = "";
  expect = "AAA     \x1b[7mEEE     \x1b[0m\r\nBBB     FFF     \r\n"
           "\x1b[7mrows 1-2/4\x1b[0m\r\n";
  pager.moveCursorToNext();
  ASSERT_EQ(0, pager.getCurRow());
  pager.render(out);
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
  auto pager = CompletionPager::create(*array, this->ps, {.rows = 5, .cols = 10});
  ASSERT_EQ(2, pager.getPanes());

  const char *expect = "\x1b[7mAAA \x1b[0mEEE \r\nBBB FFF \r\n";
  std::string out;
  pager.render(out);
  ASSERT_EQ(expect, out);

  // left
  out = "";
  expect = "CCC GGG \r\nDDD \x1b[7mHHH \x1b[0m\r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(1, pager.getCurRow());
  ASSERT_EQ(7, pager.getIndex());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // left+left+left
  out = "";
  expect = "\x1b[7mCCC \x1b[0mGGG \r\nDDD HHH \r\n";
  pager.moveCursorToLeft();
  pager.moveCursorToLeft();
  pager.moveCursorToLeft();
  ASSERT_EQ(0, pager.getCurRow());
  ASSERT_EQ(2, pager.getIndex());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // left
  out = "";
  expect = "BBB \x1b[7mFFF \x1b[0m\r\nCCC GGG \r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(0, pager.getCurRow());
  ASSERT_EQ(5, pager.getIndex());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // right
  out = "";
  expect = "BBB FFF \r\n\x1b[7mCCC \x1b[0mGGG \r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(1, pager.getCurRow());
  ASSERT_EQ(2, pager.getIndex());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // right
  out = "";
  expect = "BBB FFF \r\nCCC \x1b[7mGGG \x1b[0m\r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(1, pager.getCurRow());
  ASSERT_EQ(6, pager.getIndex());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // right
  out = "";
  expect = "CCC GGG \r\n\x1b[7mDDD \x1b[0mHHH \r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(1, pager.getCurRow());
  ASSERT_EQ(3, pager.getIndex());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // right+right
  out = "";
  expect = "\x1b[7mAAA \x1b[0mEEE \r\nBBB FFF \r\n";
  pager.moveCursorToRight();
  pager.moveCursorToRight();
  ASSERT_EQ(0, pager.getCurRow());
  ASSERT_EQ(0, pager.getIndex());
  pager.render(out);
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
  auto pager = CompletionPager::create(*array, this->ps, {.rows = 100, .cols = 5});
  ASSERT_EQ(1, pager.getPanes());
  ASSERT_EQ(0, pager.getCurRow());
  ASSERT_EQ(0, pager.getIndex());

  const char *expect = "\x1b[7mAAA \x1b[0m\r\nBBB \r\nCCC \r\nDDD \r\n";
  std::string out;
  pager.render(out);
  ASSERT_EQ(expect, out);

  // next
  out = "";
  expect = "AAA \r\n\x1b[7mBBB \x1b[0m\r\nCCC \r\nDDD \r\n";
  pager.moveCursorToNext();
  ASSERT_EQ(1, pager.getCurRow());
  ASSERT_EQ(1, pager.getIndex());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // right+right
  out = "";
  expect = "AAA \r\nBBB \r\nCCC \r\n\x1b[7mDDD \x1b[0m\r\n";
  pager.moveCursorToRight();
  pager.moveCursorToRight();
  ASSERT_EQ(3, pager.getCurRow());
  ASSERT_EQ(3, pager.getIndex());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // right
  out = "";
  expect = "\x1b[7mAAA \x1b[0m\r\nBBB \r\nCCC \r\nDDD \r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(0, pager.getCurRow());
  ASSERT_EQ(0, pager.getIndex());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // left
  out = "";
  expect = "AAA \r\nBBB \r\nCCC \r\n\x1b[7mDDD \x1b[0m\r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(3, pager.getCurRow());
  ASSERT_EQ(3, pager.getIndex());
  pager.render(out);
  ASSERT_EQ(expect, out);

  // left
  out = "";
  expect = "AAA \r\nBBB \r\n\x1b[7mCCC \x1b[0m\r\nDDD \r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(2, pager.getCurRow());
  ASSERT_EQ(2, pager.getIndex());
  pager.render(out);
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, truncate) {
  this->ps.zwjSeqFallback = true;
  auto array = this->create("@@@", "ABCD123456", "ABCD987\r", "ABCDEああ", "123456\t\t",
                            "12345👩🏼‍🏭111");
  auto pager = CompletionPager::create(*array, this->ps, {.rows = 100, .cols = 10});
  ASSERT_EQ(1, pager.getPanes());
  ASSERT_EQ(8, pager.getPaneLen());
  pager.setShowCursor(false);

  std::string out;
  pager.render(out);
  const char *expect = "@@@     \r\nABCD1234\r\nABCD987.\r\nABCDEあ.\r\n123456  \r\n12345...\r\n";
  ASSERT_EQ(expect, out);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}