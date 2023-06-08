#include <unistd.h>

#include "gtest/gtest.h"

#include "../test_common.h"

#include <keycode.h>
#include <line_renderer.h>
#include <object.h>
#include <rotate.h>
#include <type_pool.h>

using namespace ydsh;

struct Len {
  unsigned int byteSize;
  unsigned int colSize;
};

static Len getCharLen(StringRef line, const CharWidthProperties &ps, unsigned int initCols = 0) {
  Len len{0, 0};
  iterateGraphemeUntil(line, 1, [&](const GraphemeScanner::Result &grapheme) {
    LineRenderer renderer(ps, initCols);
    renderer.setLineNumLimit(0);
    renderer.renderLines(grapheme.ref);
    len.byteSize = grapheme.ref.size();
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
  ps.eaw = UnicodeUtil::HALF_WIDTH;
  ret = getCharLen(line, ps);
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(1, ret.colSize);

  ps.replaceInvalid = true;
  ps.eaw = UnicodeUtil::FULL_WIDTH;
  ret = getCharLen(line, ps);
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(2, ret.colSize);
}

struct Pipe {
  int fds[2];

  Pipe() {
    if (pipe(this->fds) < 0) {
      fatal_perror("pipe creation failed\n");
    }
  }

  ~Pipe() {
    for (auto &f : this->fds) {
      close(f);
    }
  }

  ssize_t write(StringRef ref) { return ::write(this->getWritePipe(), ref.data(), ref.size()); }

  int getReadPipe() const { return this->fds[0]; }

  int getWritePipe() const { return this->fds[1]; }
};

TEST(KeyCodeReaderTest, base) {
  Pipe pipe;
  pipe.write("1あ2\t\r");
  KeyCodeReader reader(pipe.getReadPipe());
  ASSERT_TRUE(reader.empty());
  ASSERT_EQ(1, reader.fetch());
  ASSERT_EQ("1", reader.get());

  ASSERT_EQ(3, reader.fetch());
  ASSERT_EQ("あ", reader.get());
  ASSERT_FALSE(reader.hasControlChar());

  ASSERT_EQ(1, reader.fetch());
  ASSERT_EQ("2", reader.get());

  ASSERT_EQ(1, reader.fetch());
  ASSERT_EQ("\t", reader.get());
  ASSERT_TRUE(reader.hasControlChar());

  ASSERT_EQ(1, reader.fetch());
  ASSERT_EQ("\r", reader.get());
  ASSERT_TRUE(reader.hasControlChar());
}

#define ESC_(s) "\x1b" s

TEST(KeyCodeReaderTest, escapeSeq) {
  Pipe pipe;
  pipe.write(ESC_("fあ") ESC_("OF") ESC_("[A") ESC_("[1~") ESC_("[200~1") ESC_("[1;3D")
                 ESC_("\x1b[A"));
  KeyCodeReader reader(pipe.getReadPipe());
  ASSERT_TRUE(reader.empty());

  ASSERT_EQ(2, reader.fetch());
  ASSERT_EQ(ESC_("f"), reader.get());
  ASSERT_TRUE(reader.hasControlChar());
  ASSERT_TRUE(reader.hasEscapeSeq());

  ASSERT_EQ(3, reader.fetch());
  ASSERT_EQ("あ", reader.get());
  ASSERT_FALSE(reader.hasControlChar());
  ASSERT_FALSE(reader.hasEscapeSeq());

  ASSERT_EQ(3, reader.fetch());
  ASSERT_EQ(ESC_("OF"), reader.get());
  ASSERT_TRUE(reader.hasControlChar());
  ASSERT_TRUE(reader.hasEscapeSeq());

  ASSERT_EQ(3, reader.fetch());
  ASSERT_EQ(ESC_("[A"), reader.get());
  ASSERT_TRUE(reader.hasControlChar());
  ASSERT_TRUE(reader.hasEscapeSeq());

  ASSERT_EQ(4, reader.fetch());
  ASSERT_EQ(ESC_("[1~"), reader.get());
  ASSERT_TRUE(reader.hasControlChar());
  ASSERT_TRUE(reader.hasEscapeSeq());

  ASSERT_EQ(6, reader.fetch());
  ASSERT_EQ(ESC_("[200~"), reader.get());
  ASSERT_TRUE(reader.hasControlChar());
  ASSERT_TRUE(reader.hasEscapeSeq());

  ASSERT_EQ(1, reader.fetch());
  ASSERT_EQ("1", reader.get());
  ASSERT_FALSE(reader.hasControlChar());
  ASSERT_FALSE(reader.hasEscapeSeq());

  ASSERT_EQ(6, reader.fetch());
  ASSERT_EQ(ESC_("[1;3D"), reader.get());
  ASSERT_TRUE(reader.hasControlChar());
  ASSERT_TRUE(reader.hasEscapeSeq());

  ASSERT_EQ(4, reader.fetch());
  ASSERT_EQ(ESC_("\x1b[A"), reader.get());
  ASSERT_TRUE(reader.hasControlChar());
  ASSERT_TRUE(reader.hasEscapeSeq());
}

TEST(KeyCodeReaderTest, timeout) {
  {
    Pipe pipe;
    pipe.write("\x1b[");
    KeyCodeReader reader(pipe.getReadPipe());
    ASSERT_TRUE(reader.empty());
    ASSERT_EQ(2, reader.fetch());
    ASSERT_EQ("\x1b[", reader.get());
  }

  {
    Pipe pipe;
    pipe.write("\x1b");
    KeyCodeReader reader(pipe.getReadPipe());
    ASSERT_TRUE(reader.empty());
    ASSERT_EQ(1, reader.fetch());
    ASSERT_EQ("\x1b", reader.get());
  }
}

TEST(KeyCodeReaderTest, invalid) {
  Pipe pipe;
  pipe.write("\xFF\xC2\xFF\xE0\x80\xFF\xF0\x80");
  KeyCodeReader reader(pipe.getReadPipe());
  ASSERT_TRUE(reader.empty());
  ASSERT_EQ(1, reader.fetch());
  ASSERT_EQ("\xFF", reader.get());

  ASSERT_EQ(2, reader.fetch());
  ASSERT_EQ("\xC2\xFF", reader.get());

  ASSERT_EQ(3, reader.fetch());
  ASSERT_EQ("\xE0\x80\xFF", reader.get());

  ASSERT_EQ(2, reader.fetch());
  ASSERT_EQ("\xF0\x80", reader.get());
}

struct KeyBindingTest : public ::testing::Test {
  static void checkCaret(StringRef caret, StringRef value) {
    auto v = KeyBindings::parseCaret(caret);
    ASSERT_EQ(value, v);
    ASSERT_EQ(caret, KeyBindings::toCaret(v));
  }
};

TEST_F(KeyBindingTest, caret1) {
  ASSERT_NO_FATAL_FAILURE(checkCaret("^@", StringRef("\0", 1)));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^A", "\x01"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^B", "\x02"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^C", "\x03"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^D", "\x04"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^E", "\x05"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^F", "\x06"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^G", "\x07"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^H", "\x08"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^I", "\x09"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^J", "\x0A"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^K", "\x0B"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^L", "\x0C"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^M", "\x0D"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^N", "\x0E"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^O", "\x0F"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^P", "\x10"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^Q", "\x11"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^R", "\x12"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^S", "\x13"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^T", "\x14"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^U", "\x15"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^V", "\x16"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^W", "\x17"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^X", "\x18"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^Y", "\x19"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^Z", "\x1A"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^[", "\x1B"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^\\", "\x1C"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^]", "\x1D"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^^", "\x1E"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^_", "\x1F"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^?", "\x7F"));
}

TEST_F(KeyBindingTest, caret2) {
  ASSERT_NO_FATAL_FAILURE(checkCaret("", ""));
  ASSERT_NO_FATAL_FAILURE(checkCaret("\xFF", "\xFF"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^[^[A^", "\x1B\x1B"
                                               "A^"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^1", "^1"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^", "^"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^^^", "\x1E^"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("12", "12"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^[^M", "\x1b\r"));
  ASSERT_EQ("\x1b\r", KeyBindings::parseCaret("^[\r"));
  ASSERT_EQ("^[^M", KeyBindings::toCaret("\x1b\r"));
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
  obj.refValues().push_back(DSValue::createStr(first));
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
    auto v = DSValue::create<ArrayObject>(this->pool.get(TYPE::StringArray));
    auto &obj = typeAs<ArrayObject>(v);
    append(obj, std::forward<T>(args)...);
    return toObjPtr<ArrayObject>(v);
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
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 24, .cols = 10});
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
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 24, .cols = 20});
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
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 5, .cols = 20});
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
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 5, .cols = 10});
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
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 100, .cols = 5});
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
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 100, .cols = 10});
  ASSERT_EQ(1, pager.getPanes());
  ASSERT_EQ(8, pager.getPaneLen());
  pager.setShowCursor(false);

  std::string out;
  pager.render(out);
  const char *expect = "@@@     \r\nABCD1234\r\nABCD987.\r\nABCDEあ.\r\n123456  \r\n12345...\r\n";
  ASSERT_EQ(expect, out);
}

TEST(KillRingTest, base) {
  KillRing killRing;
  killRing.setMaxSize(4);
  ASSERT_FALSE(killRing);
  killRing.add("AAA");
  ASSERT_TRUE(killRing);
  ASSERT_EQ(1, killRing.get()->size());
  ASSERT_EQ("AAA", killRing.get()->getValues()[0].asStrRef());

  // ignore empty string
  killRing.add("");
  ASSERT_EQ(1, killRing.get()->size());
  ASSERT_EQ("AAA", killRing.get()->getValues()[0].asStrRef());

  killRing.add("BBB");
  ASSERT_EQ(2, killRing.get()->size());
  ASSERT_EQ("AAA", killRing.get()->getValues()[0].asStrRef());
  ASSERT_EQ("BBB", killRing.get()->getValues()[1].asStrRef());
  killRing.add("CCC");
  killRing.add("DDD");
  ASSERT_EQ(4, killRing.get()->size());
  ASSERT_EQ("AAA", killRing.get()->getValues()[0].asStrRef());
  ASSERT_EQ("BBB", killRing.get()->getValues()[1].asStrRef());
  ASSERT_EQ("CCC", killRing.get()->getValues()[2].asStrRef());
  ASSERT_EQ("DDD", killRing.get()->getValues()[3].asStrRef());

  // truncate old item
  killRing.add("EEE");
  ASSERT_EQ(4, killRing.get()->size());
  ASSERT_EQ("BBB", killRing.get()->getValues()[0].asStrRef());
  ASSERT_EQ("CCC", killRing.get()->getValues()[1].asStrRef());
  ASSERT_EQ("DDD", killRing.get()->getValues()[2].asStrRef());
  ASSERT_EQ("EEE", killRing.get()->getValues()[3].asStrRef());

  killRing.add("FFF");
  ASSERT_EQ(4, killRing.get()->size());
  ASSERT_EQ("CCC", killRing.get()->getValues()[0].asStrRef());
  ASSERT_EQ("DDD", killRing.get()->getValues()[1].asStrRef());
  ASSERT_EQ("EEE", killRing.get()->getValues()[2].asStrRef());
  ASSERT_EQ("FFF", killRing.get()->getValues()[3].asStrRef());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}