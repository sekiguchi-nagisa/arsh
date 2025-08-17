
#include "../test_common.h"
#include "keyname_lex.h"

#include "gtest/gtest.h"

#include <keycode.h>
#include <misc/fatal.h>

using namespace arsh;

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

  void write(StringRef ref) const {
    const auto r = ::write(this->getWritePipe(), ref.data(), ref.size());
    static_cast<void>(r);
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

TEST(KeyCodeReaderTest, bracketError) {
  {
    Pipe pipe;
    pipe.write("12345");
    KeyCodeReader reader(pipe.getReadPipe());
    ASSERT_FALSE(reader.intoBracketedPasteMode([](StringRef) { return true; }));
    ASSERT_EQ(ETIME, errno);
  }

  {
    Pipe pipe;
    pipe.write("12345\x1b[201");
    KeyCodeReader reader(pipe.getReadPipe());
    ASSERT_FALSE(reader.intoBracketedPasteMode([](StringRef) { return true; }));
    ASSERT_EQ(ETIME, errno);
  }

  {
    Pipe pipe;
    pipe.write("12345\x1b[201~");
    KeyCodeReader reader(pipe.getReadPipe());
    ASSERT_FALSE(reader.intoBracketedPasteMode([](StringRef) { return false; }));
    ASSERT_EQ(ENOMEM, errno);
  }
}

TEST(KeyCodeReaderTest, CSI) {
  {
    std::string seq = "\x1b[W";
    Pipe pipe;
    pipe.write(seq.c_str());
    KeyCodeReader reader(pipe.getReadPipe());
    ASSERT_TRUE(reader.empty());
    ASSERT_EQ(seq.size(), reader.fetch());
    ASSERT_EQ(seq, reader.get());
    ASSERT_FALSE(reader.getEvent().hasValue());
  }

  {
    std::string seq = "\x1b[>=W";
    Pipe pipe;
    pipe.write(seq.c_str());
    KeyCodeReader reader(pipe.getReadPipe());
    ASSERT_TRUE(reader.empty());
    ASSERT_EQ(seq.size(), reader.fetch());
    ASSERT_EQ(seq, reader.get());
    ASSERT_FALSE(reader.getEvent().hasValue());
  }

  {
    std::string seq = "\x1b[%%W";
    Pipe pipe;
    pipe.write(seq.c_str());
    KeyCodeReader reader(pipe.getReadPipe());
    ASSERT_TRUE(reader.empty());
    ASSERT_EQ(seq.size(), reader.fetch());
    ASSERT_EQ(seq, reader.get());
    ASSERT_FALSE(reader.getEvent().hasValue());
  }

  {
    std::string seq = "\x1b[019:;<=>?22222222222234344356 !\"#$%&'()*+,-./]";
    Pipe pipe;
    pipe.write(seq.c_str());
    KeyCodeReader reader(pipe.getReadPipe());
    ASSERT_TRUE(reader.empty());
    ASSERT_EQ(seq.size(), reader.fetch());
    ASSERT_EQ(seq, reader.get());
    ASSERT_FALSE(reader.getEvent().hasValue());
  }

  {
    std::string seq = "\x1b[019:;<=>?223434435678 !\"#$%&'()*+,-./^";
    Pipe pipe;
    pipe.write(seq.c_str());
    KeyCodeReader reader(pipe.getReadPipe());
    ASSERT_TRUE(reader.empty());
    ASSERT_EQ(seq.size(), reader.fetch());
    ASSERT_EQ(seq, reader.get());
    ASSERT_FALSE(reader.getEvent().hasValue());
  }

  {
    std::string seq = "\x1b[019:;<=>?223434435678 !\"#$%&'()*+,-./G";
    Pipe pipe;
    pipe.write(seq.c_str());
    KeyCodeReader reader(pipe.getReadPipe());
    ASSERT_TRUE(reader.empty());
    ASSERT_EQ(seq.size(), reader.fetch());
    ASSERT_EQ(seq, reader.get());
    ASSERT_FALSE(reader.getEvent().hasValue());
  }
}

TEST(KeyCodeReaderTest, SS3) {
  {
    std::string seq = "\x1bOR";
    Pipe pipe;
    pipe.write(seq.c_str());
    KeyCodeReader reader(pipe.getReadPipe());
    ASSERT_TRUE(reader.empty());
    ASSERT_EQ(seq.size(), reader.fetch());
    ASSERT_EQ(seq, reader.get());
    ASSERT_TRUE(reader.getEvent().hasValue());
  }

  {
    std::string seq = "\x1bO2R";
    Pipe pipe;
    pipe.write(seq.c_str());
    KeyCodeReader reader(pipe.getReadPipe());
    ASSERT_TRUE(reader.empty());
    ASSERT_EQ(seq.size(), reader.fetch());
    ASSERT_EQ(seq, reader.get());
  }

  {
    std::string seq = "\x1bO0123456789R";
    Pipe pipe;
    pipe.write(seq.c_str());
    KeyCodeReader reader(pipe.getReadPipe());
    ASSERT_TRUE(reader.empty());
    ASSERT_EQ(seq.size(), reader.fetch());
    ASSERT_EQ(seq, reader.get());
    ASSERT_FALSE(reader.getEvent().hasValue());
  }
}

struct CaretTest : public ::testing::Test {
  static void checkCaret(StringRef caret, StringRef value) {
    auto v = KeyEvent::parseCaret(caret);
    ASSERT_EQ(value, v);
    ASSERT_EQ(caret, KeyEvent::toCaret(v));
  }
};

TEST_F(CaretTest, caret1) {
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

TEST_F(CaretTest, caret2) {
  ASSERT_NO_FATAL_FAILURE(checkCaret("", ""));
  ASSERT_NO_FATAL_FAILURE(checkCaret("\xFF", "\xFF"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^[^[A^", "\x1B\x1B"
                                               "A^"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^1", "^1"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^", "^"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^^^", "\x1E^"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("12", "12"));
  ASSERT_NO_FATAL_FAILURE(checkCaret("^[^M", "\x1b\r"));
  ASSERT_EQ("\x1b\r", KeyEvent::parseCaret("^[\r"));
  ASSERT_EQ("^[^M", KeyEvent::toCaret("\x1b\r"));
}

struct KeyCodeTest : public ::testing::Test {
  static void checkCode(const StringRef seq, const Optional<KeyEvent> &event) {
    auto ret = KeyEvent::fromEscapeSeq(seq);
    ASSERT_EQ(event.hasValue(), ret.hasValue());
    if (ret.hasValue()) {
      auto &e = event.unwrap();
      ASSERT_EQ(e.toString(), ret.unwrap().toString());
    }
  }

  static void checkName(const StringRef keyName, KeyEvent event) {
    std::string err;
    auto ret = KeyEvent::fromKeyName(keyName, &err);
    ASSERT_EQ("", err);
    ASSERT_TRUE(ret.hasValue());
    ASSERT_EQ(event.toString(), ret.unwrap().toString());
  }

  static void checkName(const StringRef keyName, const std::string &err) {
    std::string actual;
    auto ret = KeyEvent::fromKeyName(keyName, &actual);
    ASSERT_FALSE(ret.hasValue());
    ASSERT_EQ(err, actual);
  }

  static void checkName(const StringRef keyName, const Union<KeyEvent, std::string> &eventOrErr) {
    ASSERT_TRUE(eventOrErr.hasValue());
    if (is<KeyEvent>(eventOrErr)) {
      checkName(keyName, get<KeyEvent>(eventOrErr));
    } else {
      checkName(keyName, get<std::string>(eventOrErr));
    }
  }
};

#define CSI_(S) ESC_("[" S)
#define SS3_(C) ESC_("O" C)

TEST_F(KeyCodeTest, invalid) {
  static const struct {
    StringRef seq;
    Optional<KeyEvent> event;
  } patterns[] = {
      {"12", {}},
      {CSI_("%"), {}},
      {CSI_("-12423~"), {}},
      {CSI_("123"), {}},
      {CSI_("aaaa"), {}},
      {SS3_("%"), {}},
      {SS3_("AA"), {}},
      {CSI_("1*A"), {}},
      {CSI_("1;122QA"), {}},
      {CSI_("-12"), {}},
      {CSI_("4294967295"), {}},
      {CSI_("2147483648"), {}},
      {CSI_("2147483:"), {}},
      {CSI_("214748::"), {}},
      {CSI_("214748::q"), {}},
      {CSI_("200:1~"), {}},
      {CSI_("200:40;2:1~"), {}},
  };
  for (unsigned int i = 0; i < std::size(patterns); i++) {
    auto &p = patterns[i];
    SCOPED_TRACE(format("\nindex:%d, seq:%s, event:%s", i, KeyEvent::toCaret(p.seq).c_str(),
                        p.event.hasValue() ? p.event.unwrap().toString().c_str() : ""));
    ASSERT_NO_FATAL_FAILURE(checkCode(p.seq, p.event));
  }
}

TEST_F(KeyCodeTest, controlChar) {
  static const struct {
    StringRef seq;
    Optional<KeyEvent> event;
  } patterns[] = {
      {{"\x00", 0}, {}},
      {{"\x00", 1}, KeyEvent('@', ModifierKey::CTRL)},
      {"\x01", KeyEvent('a', ModifierKey::CTRL)},
      {"\x02", KeyEvent('b', ModifierKey::CTRL)},
      {"\x03", KeyEvent('c', ModifierKey::CTRL)},
      {"\x04", KeyEvent('d', ModifierKey::CTRL)},
      {"\x05", KeyEvent('e', ModifierKey::CTRL)},
      {"\x06", KeyEvent('f', ModifierKey::CTRL)},
      {"\x07", KeyEvent('g', ModifierKey::CTRL)},
      {"\x08", KeyEvent('h', ModifierKey::CTRL)},
      {"\x09", KeyEvent(FunctionKey::TAB)},
      {"\x0A", KeyEvent('j', ModifierKey::CTRL)},
      {"\x0B", KeyEvent('k', ModifierKey::CTRL)},
      {"\x0C", KeyEvent('l', ModifierKey::CTRL)},
      {"\x0D", KeyEvent(FunctionKey::ENTER)},
      {"\x0E", KeyEvent('n', ModifierKey::CTRL)},
      {"\x0F", KeyEvent('o', ModifierKey::CTRL)},
      {"\x10", KeyEvent('p', ModifierKey::CTRL)},
      {"\x11", KeyEvent('q', ModifierKey::CTRL)},
      {"\x12", KeyEvent('r', ModifierKey::CTRL)},
      {"\x13", KeyEvent('s', ModifierKey::CTRL)},
      {"\x14", KeyEvent('t', ModifierKey::CTRL)},
      {"\x15", KeyEvent('u', ModifierKey::CTRL)},
      {"\x16", KeyEvent('v', ModifierKey::CTRL)},
      {"\x17", KeyEvent('w', ModifierKey::CTRL)},
      {"\x18", KeyEvent('x', ModifierKey::CTRL)},
      {"\x19", KeyEvent('y', ModifierKey::CTRL)},
      {"\x1A", KeyEvent('z', ModifierKey::CTRL)},
      {"\x1B", KeyEvent(FunctionKey::ESCAPE)},
      {"\x1C", KeyEvent('\\', ModifierKey::CTRL)},
      {"\x1D", KeyEvent(']', ModifierKey::CTRL)},
      {"\x1E", KeyEvent('^', ModifierKey::CTRL)},
      {"\x1F", KeyEvent('_', ModifierKey::CTRL)},
      {"\x7F", KeyEvent(FunctionKey::BACKSPACE)},
      {"\x2F", {}},
      {"\x7Fあ", {}},
  };
  for (unsigned int i = 0; i < std::size(patterns); i++) {
    auto &p = patterns[i];
    SCOPED_TRACE(format("\nindex:%d, seq:%s, event:%s", i, KeyEvent::toCaret(p.seq).c_str(),
                        p.event.hasValue() ? p.event.unwrap().toString().c_str() : ""));
    ASSERT_NO_FATAL_FAILURE(checkCode(p.seq, p.event));
  }
}

TEST_F(KeyCodeTest, alt1) {
  static const struct {
    StringRef seq;
    Optional<KeyEvent> event;
  } patterns[] = {
      {ESC_("a"), KeyEvent('a', ModifierKey::ALT)}, {ESC_("b"), KeyEvent('b', ModifierKey::ALT)},
      {ESC_("c"), KeyEvent('c', ModifierKey::ALT)}, {ESC_("d"), KeyEvent('d', ModifierKey::ALT)},
      {ESC_("e"), KeyEvent('e', ModifierKey::ALT)}, {ESC_("f"), KeyEvent('f', ModifierKey::ALT)},
      {ESC_("g"), KeyEvent('g', ModifierKey::ALT)}, {ESC_("h"), KeyEvent('h', ModifierKey::ALT)},
      {ESC_("i"), KeyEvent('i', ModifierKey::ALT)}, {ESC_("j"), KeyEvent('j', ModifierKey::ALT)},
      {ESC_("k"), KeyEvent('k', ModifierKey::ALT)}, {ESC_("l"), KeyEvent('l', ModifierKey::ALT)},
      {ESC_("m"), KeyEvent('m', ModifierKey::ALT)}, {ESC_("n"), KeyEvent('n', ModifierKey::ALT)},
      {ESC_("o"), KeyEvent('o', ModifierKey::ALT)}, {ESC_("p"), KeyEvent('p', ModifierKey::ALT)},
      {ESC_("q"), KeyEvent('q', ModifierKey::ALT)}, {ESC_("r"), KeyEvent('r', ModifierKey::ALT)},
      {ESC_("s"), KeyEvent('s', ModifierKey::ALT)}, {ESC_("t"), KeyEvent('t', ModifierKey::ALT)},
      {ESC_("u"), KeyEvent('u', ModifierKey::ALT)}, {ESC_("v"), KeyEvent('v', ModifierKey::ALT)},
      {ESC_("w"), KeyEvent('w', ModifierKey::ALT)}, {ESC_("x"), KeyEvent('x', ModifierKey::ALT)},
      {ESC_("y"), KeyEvent('y', ModifierKey::ALT)}, {ESC_("z"), KeyEvent('z', ModifierKey::ALT)},
  };
  for (unsigned int i = 0; i < std::size(patterns); i++) {
    auto &p = patterns[i];
    SCOPED_TRACE(format("\nindex:%d, seq:%s, event:%s", i, KeyEvent::toCaret(p.seq).c_str(),
                        p.event.hasValue() ? p.event.unwrap().toString().c_str() : ""));
    ASSERT_NO_FATAL_FAILURE(checkCode(p.seq, p.event));
  }
}

TEST_F(KeyCodeTest, alt2) {
  static const struct {
    StringRef seq;
    Optional<KeyEvent> event;
  } patterns[] = {
      {ESC_("A"), KeyEvent('a', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("B"), KeyEvent('b', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("C"), KeyEvent('c', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("D"), KeyEvent('d', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("E"), KeyEvent('e', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("F"), KeyEvent('f', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("G"), KeyEvent('g', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("H"), KeyEvent('h', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("I"), KeyEvent('i', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("J"), KeyEvent('j', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("K"), KeyEvent('k', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("L"), KeyEvent('l', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("M"), KeyEvent('m', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("N"), KeyEvent('n', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("O"), KeyEvent('o', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("P"), KeyEvent('p', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("Q"), KeyEvent('q', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("R"), KeyEvent('r', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("S"), KeyEvent('s', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("T"), KeyEvent('t', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("U"), KeyEvent('u', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("V"), KeyEvent('v', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("W"), KeyEvent('w', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("X"), KeyEvent('x', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("Y"), KeyEvent('y', ModifierKey::ALT | ModifierKey::SHIFT)},
      {ESC_("Z"), KeyEvent('z', ModifierKey::ALT | ModifierKey::SHIFT)},
  };
  for (unsigned int i = 0; i < std::size(patterns); i++) {
    auto &p = patterns[i];
    SCOPED_TRACE(format("\nindex:%d, seq:%s, event:%s", i, KeyEvent::toCaret(p.seq).c_str(),
                        p.event.hasValue() ? p.event.unwrap().toString().c_str() : ""));
    ASSERT_NO_FATAL_FAILURE(checkCode(p.seq, p.event));
  }
}

TEST_F(KeyCodeTest, alt3) {
  static const struct {
    StringRef seq;
    Optional<KeyEvent> event;
  } patterns[] = {
      {ESC_("0"), KeyEvent('0', ModifierKey::ALT)},   {ESC_("1"), KeyEvent('1', ModifierKey::ALT)},
      {ESC_("2"), KeyEvent('2', ModifierKey::ALT)},   {ESC_("3"), KeyEvent('3', ModifierKey::ALT)},
      {ESC_("4"), KeyEvent('4', ModifierKey::ALT)},   {ESC_("5"), KeyEvent('5', ModifierKey::ALT)},
      {ESC_("6"), KeyEvent('6', ModifierKey::ALT)},   {ESC_("7"), KeyEvent('7', ModifierKey::ALT)},
      {ESC_("8"), KeyEvent('8', ModifierKey::ALT)},   {ESC_("9"), KeyEvent('9', ModifierKey::ALT)},
      {ESC_(" "), KeyEvent(' ', ModifierKey::ALT)},   {ESC_("!"), KeyEvent('!', ModifierKey::ALT)},
      {ESC_("\""), KeyEvent('"', ModifierKey::ALT)},  {ESC_("#"), KeyEvent('#', ModifierKey::ALT)},
      {ESC_("$"), KeyEvent('$', ModifierKey::ALT)},   {ESC_("%"), KeyEvent('%', ModifierKey::ALT)},
      {ESC_("&"), KeyEvent('&', ModifierKey::ALT)},   {ESC_("'"), KeyEvent('\'', ModifierKey::ALT)},
      {ESC_("("), KeyEvent('(', ModifierKey::ALT)},   {ESC_(")"), KeyEvent(')', ModifierKey::ALT)},
      {ESC_("*"), KeyEvent('*', ModifierKey::ALT)},   {ESC_("+"), KeyEvent('+', ModifierKey::ALT)},
      {ESC_(","), KeyEvent(',', ModifierKey::ALT)},   {ESC_("-"), KeyEvent('-', ModifierKey::ALT)},
      {ESC_("."), KeyEvent('.', ModifierKey::ALT)},   {ESC_("/"), KeyEvent('/', ModifierKey::ALT)},
      {ESC_(":"), KeyEvent(':', ModifierKey::ALT)},   {ESC_(";"), KeyEvent(';', ModifierKey::ALT)},
      {ESC_("<"), KeyEvent('<', ModifierKey::ALT)},   {ESC_("="), KeyEvent('=', ModifierKey::ALT)},
      {ESC_(">"), KeyEvent('>', ModifierKey::ALT)},   {ESC_("?"), KeyEvent('?', ModifierKey::ALT)},
      {ESC_("@"), KeyEvent('@', ModifierKey::ALT)},   {ESC_("["), KeyEvent('[', ModifierKey::ALT)},
      {ESC_("\\"), KeyEvent('\\', ModifierKey::ALT)}, {ESC_("]"), KeyEvent(']', ModifierKey::ALT)},
      {ESC_("^"), KeyEvent('^', ModifierKey::ALT)},   {ESC_("_"), KeyEvent('_', ModifierKey::ALT)},
      {ESC_("`"), KeyEvent('`', ModifierKey::ALT)},   {ESC_("{"), KeyEvent('{', ModifierKey::ALT)},
      {ESC_("|"), KeyEvent('|', ModifierKey::ALT)},   {ESC_("}"), KeyEvent('}', ModifierKey::ALT)},
      {ESC_("~"), KeyEvent('~', ModifierKey::ALT)},
  };
  for (unsigned int i = 0; i < std::size(patterns); i++) {
    auto &p = patterns[i];
    SCOPED_TRACE(format("\nindex:%d, seq:%s, event:%s", i, KeyEvent::toCaret(p.seq).c_str(),
                        p.event.hasValue() ? p.event.unwrap().toString().c_str() : ""));
    ASSERT_NO_FATAL_FAILURE(checkCode(p.seq, p.event));
  }
}

TEST_F(KeyCodeTest, alt4) {
  static const struct {
    StringRef seq;
    Optional<KeyEvent> event;
  } patterns[] = {
      {{ESC_("\x00"), 2}, KeyEvent('@', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x01"), KeyEvent('a', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x02"), KeyEvent('b', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x03"), KeyEvent('c', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x04"), KeyEvent('d', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x05"), KeyEvent('e', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x06"), KeyEvent('f', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x07"), KeyEvent('g', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x08"), KeyEvent('h', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x09"), KeyEvent('i', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x0A"), KeyEvent('j', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x0B"), KeyEvent('k', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x0C"), KeyEvent('l', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x0D"), KeyEvent('m', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x0E"), KeyEvent('n', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x0F"), KeyEvent('o', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x10"), KeyEvent('p', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x11"), KeyEvent('q', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x12"), KeyEvent('r', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x13"), KeyEvent('s', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x14"), KeyEvent('t', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x15"), KeyEvent('u', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x16"), KeyEvent('v', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x17"), KeyEvent('w', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x18"), KeyEvent('x', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x19"), KeyEvent('y', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x1A"), KeyEvent('z', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x1B"), KeyEvent('[', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x1C"), KeyEvent('\\', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x1D"), KeyEvent(']', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x1E"), KeyEvent('^', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x1F"), KeyEvent('_', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x7F"), KeyEvent('?', ModifierKey::CTRL | ModifierKey::ALT)},
      {ESC_("\x80"), {}},
  };
  for (unsigned int i = 0; i < std::size(patterns); i++) {
    auto &p = patterns[i];
    SCOPED_TRACE(format("\nindex:%d, seq:%s, event:%s", i, KeyEvent::toCaret(p.seq).c_str(),
                        p.event.hasValue() ? p.event.unwrap().toString().c_str() : ""));
    ASSERT_NO_FATAL_FAILURE(checkCode(p.seq, p.event));
  }
}

TEST_F(KeyCodeTest, altArrow) {
  static const struct {
    StringRef seq;
    Optional<KeyEvent> event;
  } patterns[] = {
      {ESC_("\x1b[A"), KeyEvent(FunctionKey::UP, ModifierKey::ALT)},
      {ESC_("\x1b[B"), KeyEvent(FunctionKey::DOWN, ModifierKey::ALT)},
      {ESC_("\x1b[C"), KeyEvent(FunctionKey::RIGHT, ModifierKey::ALT)},
      {ESC_("\x1b[D"), KeyEvent(FunctionKey::LEFT, ModifierKey::ALT)},
      {ESC_("\x1b[H"), {}},
      {ESC_("\x1b[F"), {}},
      {CSI_("1;3A"), KeyEvent(FunctionKey::UP, ModifierKey::ALT)},
      {CSI_("1;3B"), KeyEvent(FunctionKey::DOWN, ModifierKey::ALT)},
      {CSI_("1;3C"), KeyEvent(FunctionKey::RIGHT, ModifierKey::ALT)},
      {CSI_("1;3D"), KeyEvent(FunctionKey::LEFT, ModifierKey::ALT)},
  };
  for (unsigned int i = 0; i < std::size(patterns); i++) {
    auto &p = patterns[i];
    SCOPED_TRACE(format("\nindex:%d, seq:%s, event:%s", i, KeyEvent::toCaret(p.seq).c_str(),
                        p.event.hasValue() ? p.event.unwrap().toString().c_str() : ""));
    ASSERT_NO_FATAL_FAILURE(checkCode(p.seq, p.event));
  }
}

TEST_F(KeyCodeTest, funcKey) {
  static const struct {
    StringRef seq;
    Optional<KeyEvent> event;
  } patterns[] = {
      {CSI_("27u"), KeyEvent(FunctionKey::ESCAPE)},
      {CSI_("13u"), KeyEvent(FunctionKey::ENTER)},
      {CSI_("9u"), KeyEvent(FunctionKey::TAB)},
      {CSI_("127u"), KeyEvent(FunctionKey::BACKSPACE)},

      {CSI_("1~"), KeyEvent(FunctionKey::HOME)},
      {CSI_("2~"), KeyEvent(FunctionKey::INSERT)},
      {CSI_("3~"), KeyEvent(FunctionKey::DELETE)},
      {CSI_("4~"), KeyEvent(FunctionKey::END)},
      {CSI_("5~"), KeyEvent(FunctionKey::PAGE_UP)},
      {CSI_("6~"), KeyEvent(FunctionKey::PAGE_DOWN)},

      {CSI_("A"), KeyEvent(FunctionKey::UP)},
      {CSI_("1A"), KeyEvent(FunctionKey::UP)},
      {SS3_("A"), KeyEvent(FunctionKey::UP)},
      {CSI_("0A"), {}},
      {CSI_("2A"), {}},

      {CSI_("B"), KeyEvent(FunctionKey::DOWN)},
      {CSI_("1B"), KeyEvent(FunctionKey::DOWN)},
      {SS3_("B"), KeyEvent(FunctionKey::DOWN)},
      {CSI_("0B"), {}},
      {CSI_("2B"), {}},

      {CSI_("C"), KeyEvent(FunctionKey::RIGHT)},
      {CSI_("1C"), KeyEvent(FunctionKey::RIGHT)},
      {SS3_("C"), KeyEvent(FunctionKey::RIGHT)},
      {CSI_("0C"), {}},
      {CSI_("2C"), {}},

      {CSI_("D"), KeyEvent(FunctionKey::LEFT)},
      {CSI_("1D"), KeyEvent(FunctionKey::LEFT)},
      {SS3_("D"), KeyEvent(FunctionKey::LEFT)},
      {CSI_("0D"), {}},
      {CSI_("2D"), {}},

      {CSI_("H"), KeyEvent(FunctionKey::HOME)},
      {CSI_("1H"), KeyEvent(FunctionKey::HOME)},
      {CSI_("7~"), KeyEvent(FunctionKey::HOME)},
      {SS3_("H"), KeyEvent(FunctionKey::HOME)},
      {CSI_("0H"), {}},
      {CSI_("2H"), {}},

      {CSI_("F"), KeyEvent(FunctionKey::END)},
      {CSI_("1F"), KeyEvent(FunctionKey::END)},
      {CSI_("8~"), KeyEvent(FunctionKey::END)},
      {SS3_("F"), KeyEvent(FunctionKey::END)},
      {CSI_("0F"), {}},
      {CSI_("2F"), {}},

      // {CSI_("57358u"), KeyEvent(FunctionKey::CAPS_LOCK)},
      // {CSI_("57359u"), KeyEvent(FunctionKey::SCROLL_LOCK)},
      // {CSI_("57360u"), KeyEvent(FunctionKey::NUM_LOCK)},
      {CSI_("57361u"), KeyEvent(FunctionKey::PRINT_SCREEN)},
      {CSI_("57362u"), KeyEvent(FunctionKey::PAUSE)},
      {CSI_("57363u"), KeyEvent(FunctionKey::MENU)},
      {CSI_("29~"), KeyEvent(FunctionKey::MENU)},

      {CSI_("1P"), KeyEvent(FunctionKey::F1)},
      {CSI_("P"), KeyEvent(FunctionKey::F1)},
      {CSI_("11~"), KeyEvent(FunctionKey::F1)},
      {SS3_("P"), KeyEvent(FunctionKey::F1)},
      {CSI_("0P"), {}},
      {CSI_("2P"), {}},

      {CSI_("1Q"), KeyEvent(FunctionKey::F2)},
      {CSI_("Q"), KeyEvent(FunctionKey::F2)},
      {CSI_("12~"), KeyEvent(FunctionKey::F2)},
      {SS3_("Q"), KeyEvent(FunctionKey::F2)},
      {CSI_("0Q"), {}},
      {CSI_("2Q"), {}},

      {CSI_("1R"), KeyEvent(FunctionKey::F3)},
      {CSI_("R"), KeyEvent(FunctionKey::F3)},
      {CSI_("13~"), KeyEvent(FunctionKey::F3)},
      {SS3_("R"), KeyEvent(FunctionKey::F3)},
      {CSI_("0R"), {}},
      {CSI_("2R"), {}},

      {CSI_("1S"), KeyEvent(FunctionKey::F4)},
      {CSI_("S"), KeyEvent(FunctionKey::F4)},
      {CSI_("14~"), KeyEvent(FunctionKey::F4)},
      {SS3_("S"), KeyEvent(FunctionKey::F4)},
      {CSI_("0S"), {}},
      {CSI_("2S"), {}},

      {CSI_("15~"), KeyEvent(FunctionKey::F5)},
      {CSI_("16~"), {}},
      {CSI_("17~"), KeyEvent(FunctionKey::F6)},
      {CSI_("18~"), KeyEvent(FunctionKey::F7)},
      {CSI_("19~"), KeyEvent(FunctionKey::F8)},
      {CSI_("20~"), KeyEvent(FunctionKey::F9)},
      {CSI_("21~"), KeyEvent(FunctionKey::F10)},
      {CSI_("22~"), {}},
      {CSI_("23~"), KeyEvent(FunctionKey::F11)},
      {CSI_("24~"), KeyEvent(FunctionKey::F12)},
      {CSI_("25~"), {}},
      {CSI_("200~"), KeyEvent(FunctionKey::BRACKET_START)},

      {CSI_("Z"), KeyEvent(FunctionKey::TAB, ModifierKey::SHIFT)},
      {CSI_("1Z"), KeyEvent(FunctionKey::TAB, ModifierKey::SHIFT)},
      {CSI_("0Z"), {}},
      {CSI_("2Z"), {}},

  };
  for (unsigned int i = 0; i < std::size(patterns); i++) {
    auto &p = patterns[i];
    SCOPED_TRACE(format("\nindex:%d, seq:%s, event:%s", i, KeyEvent::toCaret(p.seq).c_str(),
                        p.event.hasValue() ? p.event.unwrap().toString().c_str() : ""));
    ASSERT_NO_FATAL_FAILURE(checkCode(p.seq, p.event));
  }
}

TEST_F(KeyCodeTest, modifier) {
  static const struct {
    StringRef seq;
    Optional<KeyEvent> event;
  } patterns[] = {
      {CSI_("1A"), KeyEvent(FunctionKey::UP)},
      {CSI_("1;0A"), KeyEvent(FunctionKey::UP)},
      {CSI_("1;1A"), KeyEvent(FunctionKey::UP)},
      {CSI_("1;2A"), KeyEvent(FunctionKey::UP, ModifierKey::SHIFT)},
      {CSI_("1;3A"), KeyEvent(FunctionKey::UP, ModifierKey::ALT)},
      {CSI_("1;4A"), KeyEvent(FunctionKey::UP, ModifierKey::SHIFT | ModifierKey::ALT)},
      {CSI_("1;5A"), KeyEvent(FunctionKey::UP, ModifierKey::CTRL)},
      {CSI_("1;6A"), KeyEvent(FunctionKey::UP, ModifierKey::CTRL | ModifierKey::SHIFT)},
      {CSI_("1;7A"), KeyEvent(FunctionKey::UP, ModifierKey::CTRL | ModifierKey::ALT)},
      {CSI_("1;8A"),
       KeyEvent(FunctionKey::UP, ModifierKey::CTRL | ModifierKey::ALT | ModifierKey::SHIFT)},
      {CSI_("1;9A"), KeyEvent(FunctionKey::UP, ModifierKey::SUPER)},
      {CSI_("1;17A"), KeyEvent(FunctionKey::UP, ModifierKey::HYPER)},
      {CSI_("1;33A"), KeyEvent(FunctionKey::UP, ModifierKey::META)},
      {CSI_("1;65A"), KeyEvent(FunctionKey::UP)},
      {CSI_("1;67A"), KeyEvent(FunctionKey::UP, ModifierKey::ALT)},
      {CSI_("1;129A"), KeyEvent(FunctionKey::UP)},
      {CSI_("1;130A"), KeyEvent(FunctionKey::UP, ModifierKey::SHIFT)},
      {CSI_("1;254A"),
       KeyEvent(FunctionKey::UP, ModifierKey::SHIFT | ModifierKey::CTRL | ModifierKey::SUPER |
                                     ModifierKey::META | ModifierKey::HYPER)},
      {CSI_("1;255A"),
       KeyEvent(FunctionKey::UP, ModifierKey::ALT | ModifierKey::CTRL | ModifierKey::SUPER |
                                     ModifierKey::META | ModifierKey::HYPER)},
      {CSI_("1;256A"),
       KeyEvent(FunctionKey::UP, ModifierKey::ALT | ModifierKey::CTRL | ModifierKey::SUPER |
                                     ModifierKey::META | ModifierKey::HYPER | ModifierKey::SHIFT)},
      {CSI_("1;257A"), {}},
      {CSI_("1;4294967295A"), {}},
      {CSI_("1;4294967290A"), {}},
      {CSI_("1;23::::A"), {}},
      {CSI_("1;23A;:"), {}},
  };
  for (unsigned int i = 0; i < std::size(patterns); i++) {
    auto &p = patterns[i];
    SCOPED_TRACE(format("\nindex:%d, seq:%s, event:%s", i, KeyEvent::toCaret(p.seq).c_str(),
                        p.event.hasValue() ? p.event.unwrap().toString().c_str() : ""));
    ASSERT_NO_FATAL_FAILURE(checkCode(p.seq, p.event));
  }
}

TEST_F(KeyCodeTest, SS3WithModifier) { // some terminal put modifiers to SS3 seq
  static const struct {
    StringRef seq;
    Optional<KeyEvent> event;
  } patterns[] = {
      {SS3_("1H"), KeyEvent(FunctionKey::HOME)},
      {SS3_("0P"), KeyEvent(FunctionKey::F1)},
      {SS3_("2P"), KeyEvent(FunctionKey::F1, ModifierKey::SHIFT)},
      {SS3_("3Q"), KeyEvent(FunctionKey::F2, ModifierKey::ALT)},
      {SS3_("4R"), KeyEvent(FunctionKey::F3, ModifierKey::ALT | ModifierKey::SHIFT)},
      {SS3_("5S"), KeyEvent(FunctionKey::F4, ModifierKey::CTRL)},
      {SS3_("222S"), KeyEvent(FunctionKey::F4, ModifierKey::SHIFT | ModifierKey::CTRL |
                                                   ModifierKey::SUPER | ModifierKey::HYPER)},
      {SS3_("257S"), {}},
      {SS3_("5aS"), {}},
      {SS3_("222"), {}},
      {SS3_("SS"), {}},
  };
  for (unsigned int i = 0; i < std::size(patterns); i++) {
    auto &p = patterns[i];
    SCOPED_TRACE(format("\nindex:%d, seq:%s, event:%s", i, KeyEvent::toCaret(p.seq).c_str(),
                        p.event.hasValue() ? p.event.unwrap().toString().c_str() : ""));
    ASSERT_NO_FATAL_FAILURE(checkCode(p.seq, p.event));
  }
}

TEST_F(KeyCodeTest, numpad) {
  static const struct {
    StringRef seq;
    Optional<KeyEvent> event;
  } patterns[] = {
      {CSI_("57398u"), {}},
      {CSI_("57399;2u"), KeyEvent('0', ModifierKey::SHIFT)},
      {CSI_("57400u"), KeyEvent('1')},
      {CSI_("57401u"), KeyEvent('2')},
      {CSI_("57402u"), KeyEvent('3')},
      {CSI_("57403u"), KeyEvent('4')},
      {CSI_("57404u"), KeyEvent('5')},
      {CSI_("57405;4u"), KeyEvent('6', ModifierKey::SHIFT | ModifierKey::ALT)},
      {CSI_("57406u"), KeyEvent('7')},
      {CSI_("57407u"), KeyEvent('8')},
      {CSI_("57408u"), KeyEvent('9')},
      {CSI_("57409u"), KeyEvent('.')},
      {CSI_("57410u"), KeyEvent('/')},
      {CSI_("57411u"), KeyEvent('*')},
      {CSI_("57412;5u"), KeyEvent('-', ModifierKey::CTRL)},
      {CSI_("57413u"), KeyEvent('+')},
      {CSI_("57414;6u"), KeyEvent(FunctionKey::ENTER, ModifierKey::SHIFT | ModifierKey::CTRL)},
      {CSI_("57415u"), KeyEvent('=')},
      {CSI_("57416u"), {}},
      {CSI_("57417u"), KeyEvent(FunctionKey::LEFT)},
      {CSI_("57418u"), KeyEvent(FunctionKey::RIGHT)},
      {CSI_("57419u"), KeyEvent(FunctionKey::UP)},
      {CSI_("57420u"), KeyEvent(FunctionKey::DOWN)},
      {CSI_("57421u"), KeyEvent(FunctionKey::PAGE_UP)},
      {CSI_("57422u"), KeyEvent(FunctionKey::PAGE_DOWN)},
      {CSI_("57423u"), KeyEvent(FunctionKey::HOME)},
      {CSI_("57424u"), KeyEvent(FunctionKey::END)},
      {CSI_("57425u"), KeyEvent(FunctionKey::INSERT)},
      {CSI_("57426u"), KeyEvent(FunctionKey::DELETE)},
      {CSI_("57427u"), {}},
      {CSI_("57428u"), {}},
      // SS3 encoding
      {SS3_("o"), KeyEvent('/')},
      {SS3_("j"), KeyEvent('*')},
      {SS3_("m"), KeyEvent('-')},
      {SS3_("k"), KeyEvent('+')},
      {SS3_("M"), KeyEvent(FunctionKey::ENTER)},
      {SS3_("n"), KeyEvent('.')},
      {SS3_("p"), KeyEvent('0')},
      {SS3_("q"), KeyEvent('1')},
      {SS3_("r"), KeyEvent('2')},
      {SS3_("s"), KeyEvent('3')},
      {SS3_("t"), KeyEvent('4')},
      {SS3_("u"), KeyEvent('5')},
      {SS3_("v"), KeyEvent('6')},
      {SS3_("w"), KeyEvent('7')},
      {SS3_("x"), KeyEvent('8')},
      {SS3_("y"), KeyEvent('9')},
  };
  for (unsigned int i = 0; i < std::size(patterns); i++) {
    auto &p = patterns[i];
    SCOPED_TRACE(format("\nindex:%d, seq:%s, event:%s", i, KeyEvent::toCaret(p.seq).c_str(),
                        p.event.hasValue() ? p.event.unwrap().toString().c_str() : ""));
    ASSERT_NO_FATAL_FAILURE(checkCode(p.seq, p.event));
  }
}

TEST_F(KeyCodeTest, kittyProtocol) {
  static const struct {
    StringRef seq;
    Optional<KeyEvent> event;
  } patterns[] = {
      {CSI_("32u"), KeyEvent(' ')},
      {CSI_("32;2u"), KeyEvent(' ', ModifierKey::SHIFT)},
      {CSI_("32;4:1u"), KeyEvent(' ', ModifierKey::SHIFT | ModifierKey::ALT)},
      {CSI_("32::32;2u"), KeyEvent(' ', ModifierKey::SHIFT)}, // base-layout key
      {CSI_("121;2u"), KeyEvent('y', ModifierKey::SHIFT)},
      {CSI_("121:89;2u"), KeyEvent('y', ModifierKey::SHIFT)},
      {CSI_("120;6u"), KeyEvent('x', ModifierKey::SHIFT | ModifierKey::CTRL)},
      {CSI_("120:88;6u"), KeyEvent('x', ModifierKey::SHIFT | ModifierKey::CTRL)},
      {CSI_("120:88;6:1u"), KeyEvent('x', ModifierKey::SHIFT | ModifierKey::CTRL)},
      {CSI_("120:88;66:1u"), KeyEvent('x', ModifierKey::SHIFT)},
      {CSI_("120:88;666:1u"), {}},                         // unrecognized modifier
      {CSI_("88u"), KeyEvent('X')},                        // shifted-key, but no shift
      {CSI_("121:89u"), {}},                               // alternate code, but no shift
      {CSI_("121:89;3u"), {}},                             // alternate code, but no shift
      {CSI_("57:40;2:1u"), KeyEvent('(')},                 // use alternate code
      {CSI_("57:40;4u"), KeyEvent('(', ModifierKey::ALT)}, // use alternate code
      {CSI_("57;2u"), KeyEvent('9', ModifierKey::SHIFT)},  // with shift, but no alternate code
      {CSI_("57:40;2:3u"), {}}, // not support other events (only allow press)
      {CSI_("57343u"), KeyEvent(57343)},
      {CSI_("57344u"), {}}, // not allow Unicode Private Use Area
      {CSI_("57345u"), {}},
      {CSI_("63743u"), {}},
      {CSI_("63744u"), KeyEvent(63744)},
      {CSI_("0u"), KeyEvent(0)},                       // allow C0 control (normally not provided)
      {CSI_("1;2u"), KeyEvent(1, ModifierKey::SHIFT)}, // allow C0 control (normally not provided)
      {CSI_("32:<;2u"), {}},
      {CSI_("32:12;2:u"), {}},
      {CSI_("32:12;2;u"), {}},
      {CSI_("32:12;2:<u"), {}},
      {CSI_("32:12:&;2:1u"), {}},
  };
  for (unsigned int i = 0; i < std::size(patterns); i++) {
    auto &p = patterns[i];
    SCOPED_TRACE(format("\nindex:%d, seq:%s, event:%s", i, KeyEvent::toCaret(p.seq).c_str(),
                        p.event.hasValue() ? p.event.unwrap().toString().c_str() : ""));
    ASSERT_NO_FATAL_FAILURE(checkCode(p.seq, p.event));
  }
}

TEST_F(KeyCodeTest, modifyOtherKeys) {
  static const struct {
    StringRef seq;
    Optional<KeyEvent> event;
  } patterns[] = {
      {CSI_("27~"), {}},
      {CSI_("27;~"), {}},
      {CSI_("27;0~"), {}},
      {CSI_("27;1aa~"), {}},
      {CSI_("27;1;~"), {}},
      {CSI_("27;;~"), {}},
      {CSI_("27;2;0~"), {}},
      {CSI_("27;5;~"), {}},
      {CSI_("27;257;9~"), {}},
      {CSI_("27;1151;9aa~"), {}},
      {CSI_("27;1151;22222222222222222222222222222~"), {}},
      {CSI_("27;0;9~"), KeyEvent(FunctionKey::TAB)},
      {CSI_("27;;9~"), KeyEvent(FunctionKey::TAB)},
      {CSI_("27;1;9~"), KeyEvent(FunctionKey::TAB)},
      {CSI_("27;2;9~"), KeyEvent(FunctionKey::TAB, ModifierKey::SHIFT)}, // normally not generated
      {CSI_("27;5;9~"), KeyEvent(FunctionKey::TAB, ModifierKey::CTRL)},
      {CSI_("27;5;44~"), KeyEvent(',', ModifierKey::CTRL)},
      {CSI_("27;6;13~"), KeyEvent(FunctionKey::ENTER, ModifierKey::CTRL | ModifierKey::SHIFT)},
      {CSI_("27;68;13~"), KeyEvent(FunctionKey::ENTER, ModifierKey::ALT | ModifierKey::SHIFT)},
  };
  for (unsigned int i = 0; i < std::size(patterns); i++) {
    auto &p = patterns[i];
    SCOPED_TRACE(format("\nindex:%d, seq:%s, event:%s", i, KeyEvent::toCaret(p.seq).c_str(),
                        p.event.hasValue() ? p.event.unwrap().toString().c_str() : ""));
    ASSERT_NO_FATAL_FAILURE(checkCode(p.seq, p.event));
  }
}

TEST_F(KeyCodeTest, keyName1) {
  static const struct {
    std::string keyName;
    Union<KeyEvent, std::string> eventOrErr;
  } patterns[] = {

      {"!", KeyEvent('!')},     {"  \"", KeyEvent('"')}, {"#", KeyEvent('#')},
      {" $\t", KeyEvent('$')},  {"%", KeyEvent('%')},    {"& ", KeyEvent('&')},
      {"'", KeyEvent('\'')},    {"( ", KeyEvent('(')},   {" ) ", KeyEvent(')')},
      {"* ", KeyEvent('*')},    {"+", KeyEvent('+')},    {",", KeyEvent(',')},
      {" - ", KeyEvent('-')},   {".", KeyEvent('.')},    {"\t\n/ \r", KeyEvent('/')},
      {"0", KeyEvent('0')},     {"1", KeyEvent('1')},    {"2", KeyEvent('2')},
      {"3", KeyEvent('3')},     {"4", KeyEvent('4')},    {"5", KeyEvent('5')},
      {"6", KeyEvent('6')},     {"7 ", KeyEvent('7')},   {"8", KeyEvent('8')},
      {"\t9\t", KeyEvent('9')}, {":", KeyEvent(':')},    {" ; ", KeyEvent(';')},
      {"< ", KeyEvent('<')},    {"=", KeyEvent('=')},    {" > ", KeyEvent('>')},
      {" ? ", KeyEvent('?')},   {" @", KeyEvent('@')},   {"[", KeyEvent('[')},
      {" \\ ", KeyEvent('\\')}, {"] ", KeyEvent(']')},   {"^", KeyEvent('^')},
      {" _ ", KeyEvent('_')},   {" ` ", KeyEvent('`')},  {" a", KeyEvent('a')},
      {" b", KeyEvent('b')},    {" c", KeyEvent('c')},   {"d", KeyEvent('d')},
      {" e ", KeyEvent('e')},   {"f ", KeyEvent('f')},   {"g", KeyEvent('g')},
      {" h ", KeyEvent('h')},   {"i ", KeyEvent('i')},   {" j", KeyEvent('j')},
      {" k", KeyEvent('k')},    {"l ", KeyEvent('l')},

      {"m", KeyEvent('m')},     {" n ", KeyEvent('n')},  {"o ", KeyEvent('o')},
      {"p", KeyEvent('p')},     {" q ", KeyEvent('q')},  {"r ", KeyEvent('r')},
      {" s\t", KeyEvent('s')},  {"\nt", KeyEvent('t')},  {"\r\tu", KeyEvent('u')},
      {" v\t", KeyEvent('v')},  {"\nw", KeyEvent('w')},  {"\r\tx", KeyEvent('x')},
      {" y\t", KeyEvent('y')},  {"\nz", KeyEvent('z')},  {"\r\t{", KeyEvent('{')},
      {" |\t", KeyEvent('|')},  {"\n}", KeyEvent('}')},  {"\r\t~", KeyEvent('~')},
  };

  for (unsigned int i = 0; i < std::size(patterns); i++) {
    auto &p = patterns[i];
    SCOPED_TRACE(format("\nindex:%d, keyName:%s, eventOrErr:%s", i, p.keyName.c_str(),
                        is<KeyEvent>(p.eventOrErr) ? get<KeyEvent>(p.eventOrErr).toString().c_str()
                        : is<std::string>(p.eventOrErr) ? get<std::string>(p.eventOrErr).c_str()
                                                        : ""));
    ASSERT_NO_FATAL_FAILURE(checkName(p.keyName, p.eventOrErr));
  }
}

TEST_F(KeyCodeTest, keyName2) {
  static const struct {
    std::string keyName;
    Union<KeyEvent, std::string> eventOrErr;
  } patterns[] = {
      {"A", KeyEvent('a', ModifierKey::SHIFT)},     {"B", KeyEvent('b', ModifierKey::SHIFT)},
      {"C ", KeyEvent('c', ModifierKey::SHIFT)},    {"D", KeyEvent('d', ModifierKey::SHIFT)},
      {"E ", KeyEvent('e', ModifierKey::SHIFT)},    {"\nF", KeyEvent('f', ModifierKey::SHIFT)},
      {"G ", KeyEvent('g', ModifierKey::SHIFT)},    {"  H", KeyEvent('h', ModifierKey::SHIFT)},
      {"\tI\t", KeyEvent('i', ModifierKey::SHIFT)}, {"\tJ\t", KeyEvent('j', ModifierKey::SHIFT)},
      {"K ", KeyEvent('k', ModifierKey::SHIFT)},    {"  L", KeyEvent('l', ModifierKey::SHIFT)},
      {"\tM\t", KeyEvent('m', ModifierKey::SHIFT)}, {"\tN\t", KeyEvent('n', ModifierKey::SHIFT)},
      {"O ", KeyEvent('o', ModifierKey::SHIFT)},    {"  P", KeyEvent('p', ModifierKey::SHIFT)},
      {"\tQ\t", KeyEvent('q', ModifierKey::SHIFT)}, {"\tR\t", KeyEvent('r', ModifierKey::SHIFT)},
      {"S ", KeyEvent('s', ModifierKey::SHIFT)},    {"  T", KeyEvent('t', ModifierKey::SHIFT)},
      {"\tU\t", KeyEvent('u', ModifierKey::SHIFT)}, {"\tV\t", KeyEvent('v', ModifierKey::SHIFT)},
      {"W ", KeyEvent('w', ModifierKey::SHIFT)},    {"  X", KeyEvent('x', ModifierKey::SHIFT)},
      {"\tY\t", KeyEvent('y', ModifierKey::SHIFT)}, {"\tZ\t", KeyEvent('z', ModifierKey::SHIFT)},
  };

  for (unsigned int i = 0; i < std::size(patterns); i++) {
    auto &p = patterns[i];
    SCOPED_TRACE(format("\nindex:%d, keyName:%s, eventOrErr:%s", i, p.keyName.c_str(),
                        is<KeyEvent>(p.eventOrErr) ? get<KeyEvent>(p.eventOrErr).toString().c_str()
                        : is<std::string>(p.eventOrErr) ? get<std::string>(p.eventOrErr).c_str()
                                                        : ""));
    ASSERT_NO_FATAL_FAILURE(checkName(p.keyName, p.eventOrErr));
  }
}

TEST_F(KeyCodeTest, keyName3) {
  static const struct {
    std::string keyName;
    Union<KeyEvent, std::string> eventOrErr;
  } patterns[] = {
      {"  space", KeyEvent(' ')},
      {"__sPa_C__E___\n", KeyEvent(' ')},
      {"pLus", KeyEvent('+')},
      {"\n\tM_inus", KeyEvent('-')},
  };

  for (unsigned int i = 0; i < std::size(patterns); i++) {
    auto &p = patterns[i];
    SCOPED_TRACE(format("\nindex:%d, keyName:%s, eventOrErr:%s", i, p.keyName.c_str(),
                        is<KeyEvent>(p.eventOrErr) ? get<KeyEvent>(p.eventOrErr).toString().c_str()
                        : is<std::string>(p.eventOrErr) ? get<std::string>(p.eventOrErr).c_str()
                                                        : ""));
    ASSERT_NO_FATAL_FAILURE(checkName(p.keyName, p.eventOrErr));
  }
}

TEST_F(KeyCodeTest, funcKeyName1) {
  static const struct {
    std::string keyName;
    Union<KeyEvent, std::string> eventOrErr;
  } patterns[] = {
      {" esCap_e", KeyEvent(FunctionKey::ESCAPE)},
      {"ESC\n", KeyEvent(FunctionKey::ESCAPE)},
      {"enter ", KeyEvent(FunctionKey::ENTER)},
      {"\n\t_tA_B", KeyEvent(FunctionKey::TAB)},
      {"BackSpaCe", KeyEvent(FunctionKey::BACKSPACE)},
      {"bS ", KeyEvent(FunctionKey::BACKSPACE)},
      {"inserT_", KeyEvent(FunctionKey::INSERT)},
      {"ins ", KeyEvent(FunctionKey::INSERT)},
      {"DELETE", KeyEvent(FunctionKey::DELETE)},
      {"del", KeyEvent(FunctionKey::DELETE)},
      {"_l_EFT_", KeyEvent(FunctionKey::LEFT)},
      {"right\r", KeyEvent(FunctionKey::RIGHT)},
      {"UP", KeyEvent(FunctionKey::UP)},
      {"Down", KeyEvent(FunctionKey::DOWN)},
      {"pageup", KeyEvent(FunctionKey::PAGE_UP)},
      {"pAGED_own", KeyEvent(FunctionKey::PAGE_DOWN)},
      {"pGUP", KeyEvent(FunctionKey::PAGE_UP)},
      {"pgdN", KeyEvent(FunctionKey::PAGE_DOWN)},
      {"Home", KeyEvent(FunctionKey::HOME)},
      {"END_", KeyEvent(FunctionKey::END)},
      {"CTrl", "need '+' or '-' after modifier"},
      {"CAPS_LOCK", "unrecognized modifier or function key: CAPS_LOCK"},
      {"scrollLock", "unrecognized modifier or function key: scrollLock"},
      {"\t SCRLK_", "unrecognized modifier or function key: SCRLK_"},
      {"Num_LoCK", "unrecognized modifier or function key: Num_LoCK"},
      {"Print_Scr_Een", KeyEvent(FunctionKey::PRINT_SCREEN)},
      {"prtsc", KeyEvent(FunctionKey::PRINT_SCREEN)},
      {"break", KeyEvent(FunctionKey::PAUSE)},
      {"paU__Se", KeyEvent(FunctionKey::PAUSE)},
      {"Menu", KeyEvent(FunctionKey::MENU)},
      {"F0", "unrecognized modifier or function key: F0"},
      {"F13", "unrecognized modifier or function key: F13"},
      {"bracket_start", "unrecognized modifier or function key: bracket_start"},
  };

  for (unsigned int i = 0; i < std::size(patterns); i++) {
    auto &p = patterns[i];
    SCOPED_TRACE(format("\nindex:%d, keyName:%s, eventOrErr:%s", i, p.keyName.c_str(),
                        is<KeyEvent>(p.eventOrErr) ? get<KeyEvent>(p.eventOrErr).toString().c_str()
                        : is<std::string>(p.eventOrErr) ? get<std::string>(p.eventOrErr).c_str()
                                                        : ""));
    ASSERT_NO_FATAL_FAILURE(checkName(p.keyName, p.eventOrErr));
  }
}

TEST_F(KeyCodeTest, funcKeyName2) {
  static const struct {
    std::string keyName;
    Union<KeyEvent, std::string> eventOrErr;
  } patterns[] = {
      {"F1", KeyEvent(FunctionKey::F1)},     {"f2", KeyEvent(FunctionKey::F2)},
      {"f3", KeyEvent(FunctionKey::F3)},     {"F4", KeyEvent(FunctionKey::F4)},
      {"f5", KeyEvent(FunctionKey::F5)},     {"\nf6", KeyEvent(FunctionKey::F6)},
      {"F7", KeyEvent(FunctionKey::F7)},     {"\tf8", KeyEvent(FunctionKey::F8)},
      {"_F_9", KeyEvent(FunctionKey::F9)},   {"F10__", KeyEvent(FunctionKey::F10)},
      {"f1__1", KeyEvent(FunctionKey::F11)}, {"f1_2", KeyEvent(FunctionKey::F12)},
  };

  for (unsigned int i = 0; i < std::size(patterns); i++) {
    auto &p = patterns[i];
    SCOPED_TRACE(format("\nindex:%d, keyName:%s, eventOrErr:%s", i, p.keyName.c_str(),
                        is<KeyEvent>(p.eventOrErr) ? get<KeyEvent>(p.eventOrErr).toString().c_str()
                        : is<std::string>(p.eventOrErr) ? get<std::string>(p.eventOrErr).c_str()
                                                        : ""));
    ASSERT_NO_FATAL_FAILURE(checkName(p.keyName, p.eventOrErr));
  }
}

TEST_F(KeyCodeTest, nameAndModifier) {
  static const struct {
    std::string keyName;
    Union<KeyEvent, std::string> eventOrErr;
  } patterns[] = {
      {"ctrl+m", KeyEvent('m', ModifierKey::CTRL)},
      {"ctrl-M", KeyEvent('m', ModifierKey::CTRL | ModifierKey::SHIFT)},
      {"ALT  -alt++", KeyEvent('+', ModifierKey::ALT)},
      {"alt--", KeyEvent('-', ModifierKey::ALT)},
      {"super+ ___plus___", KeyEvent('+', ModifierKey::SUPER)},
      {"  m_Eta  + hY__P_eR-m_iNus", KeyEvent('-', ModifierKey::HYPER | ModifierKey::META)},
      {"  ctrl  +   m   ", KeyEvent('m', ModifierKey::CTRL)},
      {"shift - spA_ce", KeyEvent(' ', ModifierKey::SHIFT)},
      {"shift - s", KeyEvent('s', ModifierKey::SHIFT)},
      {"shift + alt - Tab", KeyEvent(FunctionKey::TAB, ModifierKey::SHIFT | ModifierKey::ALT)},
  };

  for (unsigned int i = 0; i < std::size(patterns); i++) {
    auto &p = patterns[i];
    SCOPED_TRACE(format("\nindex:%d, keyName:%s, eventOrErr:%s", i, p.keyName.c_str(),
                        is<KeyEvent>(p.eventOrErr) ? get<KeyEvent>(p.eventOrErr).toString().c_str()
                        : is<std::string>(p.eventOrErr) ? get<std::string>(p.eventOrErr).c_str()
                                                        : ""));
    ASSERT_NO_FATAL_FAILURE(checkName(p.keyName, p.eventOrErr));
  }
}

TEST_F(KeyCodeTest, InvalidkeyName) {
  static const struct {
    std::string keyName;
    Union<KeyEvent, std::string> eventOrErr;
  } patterns[] = {
      {" ", "need modifiers or keyname: "},
      {"shift", "need '+' or '-' after modifier"},
      {"shift &", "need '+' or '-' after modifier"},
      {" shift -", "need modifiers or keyname: "},
      {" shift +", "need modifiers or keyname: "},
      {"shift+M", "shift modifier is only allowed with lower letter or function key"},
      {"shift++", "shift modifier is only allowed with lower letter or function key"},
      {"shift- Minus", "shift modifier is only allowed with lower letter or function key"},
      {"shift+s 1234", "invalid token: 1"},
      {"s + shift", "invalid token: +"},
      {"shift+s qw", "invalid token: qw"},
  };

  for (unsigned int i = 0; i < std::size(patterns); i++) {
    auto &p = patterns[i];
    SCOPED_TRACE(format("\nindex:%d, keyName:%s, eventOrErr:%s", i, p.keyName.c_str(),
                        is<KeyEvent>(p.eventOrErr) ? get<KeyEvent>(p.eventOrErr).toString().c_str()
                        : is<std::string>(p.eventOrErr) ? get<std::string>(p.eventOrErr).c_str()
                                                        : ""));
    ASSERT_NO_FATAL_FAILURE(checkName(p.keyName, p.eventOrErr));
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}