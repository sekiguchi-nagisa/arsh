#include <unistd.h>

#include "gtest/gtest.h"

#include <keycode.h>
#include <line_buffer.h>
#include <line_renderer.h>
#include <rotate.h>

using namespace ydsh;

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

  ssize_t write(StringRef ref) const {
    return ::write(this->getWritePipe(), ref.data(), ref.size());
  }

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

TEST(KillRingTest, pop) {
  KillRing killRing;
  killRing.setMaxSize(4);
  ASSERT_FALSE(killRing);
  killRing.add("AAA");
  killRing.add("BBB");
  killRing.add("CCC");
  killRing.add("DDD");
  ASSERT_TRUE(killRing);
  ASSERT_EQ(4, killRing.get()->size());
  killRing.reset();
  ASSERT_EQ("DDD", killRing.getCurrent().toString());
  killRing.rotate();
  ASSERT_EQ("CCC", killRing.getCurrent().toString());
  killRing.rotate();
  ASSERT_EQ("BBB", killRing.getCurrent().toString());
  killRing.rotate();
  ASSERT_EQ("AAA", killRing.getCurrent().toString());
  killRing.rotate();
  ASSERT_EQ("DDD", killRing.getCurrent().toString());

  killRing.get()->refValues().erase(killRing.get()->refValues().begin());
  killRing.get()->refValues().erase(killRing.get()->refValues().begin());

  killRing.rotate();
  ASSERT_EQ("DDD", killRing.getCurrent().toString());
  killRing.rotate();
  ASSERT_EQ("CCC", killRing.getCurrent().toString());
}

TEST(HistRotatorTest, base) {
  auto value = DSValue::create<ArrayObject>(static_cast<unsigned int>(TYPE::StringArray),
                                            std::vector<DSValue>());
  auto obj = toObjPtr<ArrayObject>(value);
  obj->append(DSValue::createStr("AAA"));
  obj->append(DSValue::createStr("BBB"));
  obj->append(DSValue::createStr("CCC"));
  obj->append(DSValue::createStr("DDD"));
  obj->append(DSValue::createStr("EEE"));

  HistRotator rotate(obj);
  rotate.setMaxSize(4);
  ASSERT_EQ(4, rotate.getMaxSize());

  ASSERT_EQ(6, obj->getValues().size());
  ASSERT_EQ("AAA", obj->getValues()[0].asStrRef().toString());
  ASSERT_EQ("BBB", obj->getValues()[1].asStrRef().toString());
  ASSERT_EQ("CCC", obj->getValues()[2].asStrRef().toString());
  ASSERT_EQ("DDD", obj->getValues()[3].asStrRef().toString());
  ASSERT_EQ("EEE", obj->getValues()[4].asStrRef().toString());
  ASSERT_EQ("", obj->getValues()[5].asStrRef().toString()); // reserved for current editing buffer

  // rotate prev
  StringRef ref = "@@@"; // current editing content
  bool r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  ASSERT_EQ(4, obj->size());
  ASSERT_EQ("CCC", obj->getValues()[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj->getValues()[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj->getValues()[2].asStrRef().toString());
  ASSERT_EQ("@@@", obj->getValues()[3].asStrRef().toString());
  ASSERT_EQ("EEE", ref.toString());

  // rotate next
  r = rotate.rotate(ref, HistRotator::Op::NEXT);
  ASSERT_TRUE(r);
  ASSERT_EQ(4, obj->size());
  ASSERT_EQ("CCC", obj->getValues()[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj->getValues()[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj->getValues()[2].asStrRef().toString());
  ASSERT_EQ("@@@", obj->getValues()[3].asStrRef().toString());
  ASSERT_EQ("@@@", ref.toString());

  // rotate next
  r = rotate.rotate(ref, HistRotator::Op::NEXT);
  ASSERT_FALSE(r);
  ASSERT_EQ(4, obj->size());
  ASSERT_EQ("CCC", obj->getValues()[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj->getValues()[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj->getValues()[2].asStrRef().toString());
  ASSERT_EQ("@@@", obj->getValues()[3].asStrRef().toString());
  ASSERT_EQ("@@@", ref.toString());

  // rotate next
  r = rotate.rotate(ref, HistRotator::Op::NEXT);
  ASSERT_FALSE(r);
  ASSERT_EQ(4, obj->size());
  ASSERT_EQ("CCC", obj->getValues()[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj->getValues()[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj->getValues()[2].asStrRef().toString());
  ASSERT_EQ("@@@", obj->getValues()[3].asStrRef().toString());
  ASSERT_EQ("@@@", ref.toString());

  // rotate prev+prev
  ref = "$$$$";
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  ASSERT_EQ(4, obj->size());
  ASSERT_EQ("CCC", obj->getValues()[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj->getValues()[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj->getValues()[2].asStrRef().toString());
  ASSERT_EQ("$$$$", obj->getValues()[3].asStrRef().toString());
  ASSERT_EQ("DDD", ref.toString());

  // rotate prev
  ref = "&&&&";
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  ASSERT_EQ(4, obj->size());
  ASSERT_EQ("CCC", obj->getValues()[0].asStrRef().toString());
  ASSERT_EQ("&&&&", obj->getValues()[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj->getValues()[2].asStrRef().toString());
  ASSERT_EQ("$$$$", obj->getValues()[3].asStrRef().toString());
  ASSERT_EQ("CCC", ref.toString());

  // rotate prev
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_FALSE(r);
  ASSERT_EQ(4, obj->size());
  ASSERT_EQ("CCC", obj->getValues()[0].asStrRef().toString());
  ASSERT_EQ("&&&&", obj->getValues()[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj->getValues()[2].asStrRef().toString());
  ASSERT_EQ("$$$$", obj->getValues()[3].asStrRef().toString());
  ASSERT_EQ("CCC", ref.toString());

  // rotate prev
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_FALSE(r);
  ASSERT_EQ(4, obj->size());
  ASSERT_EQ("CCC", obj->getValues()[0].asStrRef().toString());
  ASSERT_EQ("&&&&", obj->getValues()[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj->getValues()[2].asStrRef().toString());
  ASSERT_EQ("$$$$", obj->getValues()[3].asStrRef().toString());
  ASSERT_EQ("CCC", ref.toString());

  // revert
  rotate.revertAll();
  ASSERT_EQ(3, obj->size());
  ASSERT_EQ("CCC", obj->getValues()[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj->getValues()[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj->getValues()[2].asStrRef().toString());
}

TEST(HistRotatorTest, broken1) {
  auto value = DSValue::create<ArrayObject>(static_cast<unsigned int>(TYPE::StringArray),
                                            std::vector<DSValue>());
  auto obj = toObjPtr<ArrayObject>(value);
  obj->append(DSValue::createStr("AAA"));
  obj->append(DSValue::createStr("BBB"));
  obj->append(DSValue::createStr("CCC"));
  obj->append(DSValue::createStr("DDD"));
  obj->append(DSValue::createStr("EEE"));

  HistRotator rotate(obj);
  rotate.setMaxSize(4);
  ASSERT_EQ(4, rotate.getMaxSize());

  // rotate prev
  StringRef ref = "@@@"; // current editing content
  bool r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  ASSERT_EQ(4, obj->size());
  ASSERT_EQ("CCC", obj->getValues()[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj->getValues()[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj->getValues()[2].asStrRef().toString());
  ASSERT_EQ("@@@", obj->getValues()[3].asStrRef().toString());
  ASSERT_EQ("EEE", ref);

  obj->refValues().erase(obj->refValues().begin() + 1);
  ref = "&&&";
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  ASSERT_EQ(3, obj->size());
  ASSERT_EQ("CCC", obj->getValues()[0].asStrRef().toString());
  ASSERT_EQ("&&&", obj->getValues()[1].asStrRef().toString());
  ASSERT_EQ("@@@", obj->getValues()[2].asStrRef().toString());
  ASSERT_EQ("CCC", ref);

  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_FALSE(r);

  obj->refValues().clear();
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_FALSE(r);

  // revert
  rotate.revertAll();
  ASSERT_EQ(0, obj->size());
}

TEST(HistRotator, broken2) {
  auto value = DSValue::create<ArrayObject>(static_cast<unsigned int>(TYPE::StringArray),
                                            std::vector<DSValue>());
  auto obj = toObjPtr<ArrayObject>(value);
  obj->append(DSValue::createStr("AAA"));
  obj->append(DSValue::createStr("BBB"));
  obj->append(DSValue::createStr("CCC"));
  obj->append(DSValue::createStr("DDD"));
  obj->append(DSValue::createStr("EEE"));

  HistRotator rotate(obj);
  rotate.setMaxSize(4);
  ASSERT_EQ(4, rotate.getMaxSize());

  // rotate prev
  StringRef ref = "@@@"; // current editing content
  bool r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  ASSERT_EQ(4, obj->size());
  ASSERT_EQ("CCC", obj->getValues()[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj->getValues()[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj->getValues()[2].asStrRef().toString());
  ASSERT_EQ("@@@", obj->getValues()[3].asStrRef().toString());
  ASSERT_EQ("EEE", ref);

  // remove history and rotate prev
  ref = "%%%";
  obj->refValues().erase(obj->refValues().begin(), obj->refValues().begin() + 2);
  ASSERT_EQ(2, obj->size());
  ASSERT_EQ("EEE", obj->getValues()[0].asStrRef().toString());
  ASSERT_EQ("@@@", obj->getValues()[1].asStrRef().toString());

  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_FALSE(r);
  ASSERT_EQ(2, obj->size());
  ASSERT_EQ("EEE", obj->getValues()[0].asStrRef().toString());
  ASSERT_EQ("@@@", obj->getValues()[1].asStrRef().toString());
  ASSERT_EQ("%%%", ref);

  rotate.revertAll();
  ASSERT_EQ(1, obj->size());
  ASSERT_EQ("EEE", obj->getValues()[0].asStrRef().toString());
}

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
  ASSERT_EQ(17, values.size());
  ASSERT_EQ(values.find(HighlightTokenClass::NONE)->second, "\x1b[38;2;212;212;212m");
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

struct LineBufferTest : public ::testing::Test {
  struct LineIntervalSet {
    unsigned int cursor;
    unsigned int expectIndex;
    std::string line;
    std::string wholeLine;
  };

  static void checkLineInterval(const std::vector<LineIntervalSet> &testSets, LineBuffer &buffer) {
    for (auto &e : testSets) {
      SCOPED_TRACE("cursor: " + std::to_string(e.cursor));
      buffer.setCursor(e.cursor);
      unsigned int index = buffer.findCurNewlineIndex();
      ASSERT_EQ(e.expectIndex, index);

      auto actualLine = buffer.getCurLine(false).toString();
      ASSERT_EQ(e.line, actualLine);
      actualLine = buffer.getCurLine(true).toString();
      ASSERT_EQ(e.wholeLine, actualLine);
    }
  }
};

TEST_F(LineBufferTest, base) {
  std::string storage;
  storage.resize(16, '@');
  LineBuffer buffer(storage.data(), storage.size());
  ASSERT_EQ(0, buffer.getUsedSize());
  ASSERT_EQ(0, buffer.getCursor());
  ASSERT_EQ("", buffer.get().toString());

  // insert
  ASSERT_TRUE(buffer.insertToCursor("1234"));
  ASSERT_EQ(4, buffer.getUsedSize());
  ASSERT_EQ(4, buffer.getCursor());
  ASSERT_EQ("1234", buffer.get().toString());
  ASSERT_EQ("1234", buffer.getToCursor().toString());
  ASSERT_EQ("", buffer.getFromCursor().toString());

  // insert large data
  ASSERT_FALSE(buffer.insertToCursor("QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQqqqq"));
  ASSERT_EQ(4, buffer.getUsedSize());
  ASSERT_EQ(4, buffer.getCursor());

  // move cursor
  buffer.setCursor(1);
  ASSERT_EQ(1, buffer.getCursor());
  ASSERT_EQ("1234", buffer.get().toString());
  ASSERT_EQ("1", buffer.getToCursor().toString());
  ASSERT_EQ("234", buffer.getFromCursor().toString());

  // delete right
  ASSERT_TRUE(buffer.deleteFromCursor(1));
  ASSERT_EQ(3, buffer.getUsedSize());
  ASSERT_EQ(1, buffer.getCursor());
  ASSERT_EQ("134", buffer.get().toString());
  ASSERT_EQ("1", buffer.getToCursor().toString());
  ASSERT_EQ("34", buffer.getFromCursor().toString());

  // delete left
  ASSERT_TRUE(buffer.deleteToCursor(1));
  ASSERT_EQ(2, buffer.getUsedSize());
  ASSERT_EQ(0, buffer.getCursor());
  ASSERT_EQ("34", buffer.get().toString());
  ASSERT_EQ("", buffer.getToCursor().toString());
  ASSERT_EQ("34", buffer.getFromCursor().toString());

  // delete fail
  ASSERT_FALSE(buffer.deleteToCursor(0));
  ASSERT_FALSE(buffer.deleteToCursor(4));
  ASSERT_FALSE(buffer.deleteFromCursor(0));
  ASSERT_FALSE(buffer.deleteFromCursor(4));
}

TEST_F(LineBufferTest, charOp) {
  std::string storage;
  storage.resize(16, '@');
  LineBuffer buffer(storage.data(), storage.size());

  ASSERT_TRUE(buffer.insertToCursor("あいう"));
  ASSERT_EQ(9, buffer.getUsedSize());
  ASSERT_EQ(9, buffer.getCursor());

  // char op
  auto retSize = buffer.nextCharBytes();
  ASSERT_EQ(0, retSize);
  retSize = buffer.prevCharBytes();
  ASSERT_EQ(3, retSize);
  buffer.decCursor(retSize);
  ASSERT_EQ("あい", buffer.getToCursor().toString());
  ASSERT_EQ("う", buffer.getFromCursor().toString());
  retSize = buffer.nextCharBytes();
  ASSERT_EQ(3, retSize);
  buffer.setCursor(0);
  ASSERT_EQ(0, buffer.prevCharBytes());

  buffer.deleteAll();
  ASSERT_EQ(0, buffer.getUsedSize());
  ASSERT_EQ(0, buffer.getCursor());
  ASSERT_TRUE(buffer.insertToCursor("あ111い"));
  ASSERT_EQ(9, buffer.getCursor());

  // word op
  retSize = buffer.nextWordBytes();
  ASSERT_EQ(0, retSize);
  retSize = buffer.prevWordBytes();
  ASSERT_EQ(3, retSize);
  buffer.decCursor(retSize);
  ASSERT_EQ("あ111", buffer.getToCursor().toString());
  ASSERT_EQ("い", buffer.getFromCursor().toString());
  retSize = buffer.prevWordBytes();
  ASSERT_EQ(3, retSize);
  retSize = buffer.nextWordBytes();
  ASSERT_EQ(3, retSize);
  buffer.setCursor(0);
  ASSERT_EQ(0, buffer.prevWordBytes());
}

TEST_F(LineBufferTest, deleteOut) {
  std::string storage;
  storage.resize(16, '@');
  LineBuffer buffer(storage.data(), storage.size());
  ASSERT_TRUE(buffer.insertToCursor("あいう"));
  buffer.setCursor(3);
  ASSERT_EQ("あ", buffer.getToCursor().toString());
  ASSERT_EQ("いう", buffer.getFromCursor().toString());

  std::string out;
  ASSERT_TRUE(buffer.deleteToCursor(3, &out));
  ASSERT_EQ("あ", out);
  ASSERT_EQ("いう", buffer.getFromCursor().toString());
  ASSERT_TRUE(buffer.deleteFromCursor(3, &out));
  ASSERT_EQ("い", out);
  ASSERT_EQ("う", buffer.getFromCursor().toString());
}

TEST_F(LineBufferTest, newline1) {
  std::string storage;
  storage.resize(32, '@');
  LineBuffer buffer(storage.data(), storage.size());
  ASSERT_TRUE(buffer.insertToCursor("123")); // not end with newline
  ASSERT_EQ(3, buffer.getCursor());
  ASSERT_EQ(3, buffer.getUsedSize());
  buffer.syncNewlinePosList();
  ASSERT_TRUE(buffer.isSingleLine());
  auto &newlinePosList = buffer.getNewlinePosList();
  ASSERT_EQ(0, newlinePosList.size());

  unsigned int r = buffer.findCurNewlineIndex();
  ASSERT_EQ(0, r);

  std::vector<LineIntervalSet> table = {
      {0, 0, "", "123"},
      {1, 0, "1", "123"},
      {2, 0, "12", "123"},
      {3, 0, "123", "123"},
  };
  ASSERT_NO_FATAL_FAILURE(checkLineInterval(table, buffer));
}

TEST_F(LineBufferTest, newline2) {
  std::string storage;
  storage.resize(32, '@');
  LineBuffer buffer(storage.data(), storage.size());
  ASSERT_TRUE(buffer.insertToCursor("123\n456\n789")); // not end with newline
  ASSERT_EQ(11, buffer.getCursor());
  ASSERT_EQ(11, buffer.getUsedSize());
  buffer.syncNewlinePosList();
  ASSERT_FALSE(buffer.isSingleLine());
  auto &newlinePosList = buffer.getNewlinePosList();
  ASSERT_EQ(2, newlinePosList.size());
  ASSERT_EQ(3, newlinePosList[0]);
  ASSERT_EQ(7, newlinePosList[1]);

  unsigned int r = buffer.findCurNewlineIndex();
  ASSERT_EQ(2, r);

  std::vector<LineIntervalSet> table = {
      {0, 0, "", "123"}, {1, 0, "1", "123"}, {2, 0, "12", "123"},  {3, 0, "123", "123"},
      {4, 1, "", "456"}, {5, 1, "4", "456"}, {6, 1, "45", "456"},  {7, 1, "456", "456"},
      {8, 2, "", "789"}, {9, 2, "7", "789"}, {10, 2, "78", "789"}, {11, 2, "789", "789"},
  };
  ASSERT_NO_FATAL_FAILURE(checkLineInterval(table, buffer));
}

TEST_F(LineBufferTest, newline3) {
  std::string storage;
  storage.resize(32, '@');
  LineBuffer buffer(storage.data(), storage.size());
  ASSERT_TRUE(buffer.insertToCursor("123\n456\n789\n")); // end with newline
  ASSERT_EQ(12, buffer.getCursor());
  ASSERT_EQ(12, buffer.getUsedSize());
  buffer.syncNewlinePosList();
  ASSERT_FALSE(buffer.isSingleLine());
  auto &newlinePosList = buffer.getNewlinePosList();
  ASSERT_EQ(3, newlinePosList.size());
  ASSERT_EQ(3, newlinePosList[0]);
  ASSERT_EQ(7, newlinePosList[1]);
  ASSERT_EQ(11, newlinePosList[2]);

  unsigned int r = buffer.findCurNewlineIndex();
  ASSERT_EQ(3, r);

  std::vector<LineIntervalSet> table = {
      {0, 0, "", "123"}, {1, 0, "1", "123"}, {2, 0, "12", "123"},  {3, 0, "123", "123"},
      {4, 1, "", "456"}, {5, 1, "4", "456"}, {6, 1, "45", "456"},  {7, 1, "456", "456"},
      {8, 2, "", "789"}, {9, 2, "7", "789"}, {10, 2, "78", "789"}, {11, 2, "789", "789"},
      {12, 3, "", ""},
  };
  ASSERT_NO_FATAL_FAILURE(checkLineInterval(table, buffer));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}