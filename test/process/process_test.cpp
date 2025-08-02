#include "gtest/gtest.h"

#include "../../tools/platform/platform.h"
#include "../test_common.h"

#ifndef INSPECT_PATH
#error "require INSPECT_PATH"
#endif

class ProcTest : public ExpectOutput {};

template <typename Func>
static Output spawnAndWait(IOConfig config, Func func, bool remove = false) {
  return ProcBuilder::spawn(config, func).waitAndGetResult(remove);
}

TEST_F(ProcTest, base) {
  auto ret = ProcBuilder(INSPECT_PATH).execAndGetResult();
  ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(WaitStatus::EXITED, ret.status.kind));
  ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, ret.status.value));
  ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(ret.out.empty()));
  ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(ret.err.empty()));
}

TEST_F(ProcTest, status) {
  auto ret = spawnAndWait(IOConfig{}, [&] { return 100; });
  ASSERT_NO_FATAL_FAILURE(this->expect(ret, 100));

  IOConfig config;
  config.out = IOConfig::PIPE;
  ret = spawnAndWait(config, [&] { return 10; });
  ASSERT_NO_FATAL_FAILURE(this->expect(ret, 10));

  ret = spawnAndWait(IOConfig{}, [&]() -> int { abort(); });
  ASSERT_NO_FATAL_FAILURE(this->expect(ret, SIGABRT, WaitStatus::SIGNALED));
}

TEST_F(ProcTest, pipe) {
  IOConfig config;
  config.out = IOConfig::PIPE;
  auto ret = spawnAndWait(config, [&] { return 10; });
  ASSERT_NO_FATAL_FAILURE(this->expect(ret, 10));

  config.err = IOConfig::PIPE;
  ret = spawnAndWait(config, [&] {
    fprintf(stdout, "hello");
    fprintf(stderr, "world");
    return 10;
  });
  ASSERT_NO_FATAL_FAILURE(this->expect(ret, 10, WaitStatus::EXITED, "hello", "world"));
}

TEST_F(ProcTest, pty1) {
  IOConfig config;
  config.in = IOConfig::PTY;
  config.out = IOConfig::PTY;
  config.err = IOConfig::PIPE;

  auto ret = spawnAndWait(config, [&] {
    if (!isatty(STDIN_FILENO)) {
      return 10;
    }
    if (!isatty(STDOUT_FILENO)) {
      return 20;
    }
    if (isatty(STDERR_FILENO)) {
      return 30;
    }
    return 0;
  });
  ASSERT_NO_FATAL_FAILURE(this->expect(ret));
}

TEST_F(ProcTest, pty2) {
  if (arsh::platform::isCygwinOrMsys(arsh::platform::platform())) {
    return; // workaround for Cygwin
  }

  IOConfig config;
  config.in = IOConfig::PTY;
  config.out = IOConfig::PTY;
  config.err = IOConfig::PIPE;

  auto handle = ProcBuilder::spawn(config, [&] {
    char buf[64];
    auto size = read(STDIN_FILENO, buf, 64);
    if (size > 0) {
      buf[size] = '\0';
      printf("%s\n", buf);
      return 0;
    }
    return 1;
  });
  std::string str = "hello";
  auto r = write(handle.in(), str.c_str(), str.size());
  (void)r;
  auto ret2 = handle.waitAndGetResult(false);
  ASSERT_NO_FATAL_FAILURE(this->expect(ret2, 0, WaitStatus::EXITED, "hello\n"));
}

TEST_F(ProcTest, pty3) {
  if (arsh::platform::isCygwinOrMsys(arsh::platform::platform())) {
    return; // workaround for Cygwin
  }

  IOConfig config;
  config.in = IOConfig::PTY;
  config.out = IOConfig::PTY;

  auto handle = ProcBuilder::spawn(config, [&] {
    char buf[1];
    while (read(STDIN_FILENO, buf, 1) > 0) {
      if (buf[0] == 'p') {
        printf("print\n");
      } else if (buf[0] == 'b') {
        printf("break!!\n");
        break;
      } else {
        printf("ignore %c\n", buf[0]);
      }
      fflush(stdout);
    }
    return 0;
  });
  auto r = write(handle.in(), "p", 1);
  (void)r;
  auto output = handle.readAll(200);
  ASSERT_EQ("print\n", output.first);

  r = write(handle.in(), "x", 1);
  (void)r;
  output = handle.readAll(200);
  ASSERT_EQ("ignore x\n", output.first);

  r = write(handle.in(), "b", 1);
  (void)r;
  auto ret = handle.waitAndGetResult(false);
  ASSERT_NO_FATAL_FAILURE(this->expect(ret, 0, WaitStatus::EXITED, "break!!\n"));
}

TEST_F(ProcTest, pty4) {
  IOConfig config;
  config.in = IOConfig::PTY;
  config.out = IOConfig::PTY;
  config.err = IOConfig::PIPE;
  arsh::xcfmakesane(config.term);

  // start with raw mode
  auto handle = ProcBuilder::spawn(config, [&] {
    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(100));
    }
    return 0;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  std::string str = "\x03"; // CTRL-C
  auto r = write(handle.in(), str.c_str(), str.size());
  (void)r;
  fsync(handle.in());
  auto ret2 = handle.waitAndGetResult(false);
  if (arsh::platform::isFakeUnix(arsh::platform::platform())) {
    this->expect(ret2, SIGINT, WaitStatus::SIGNALED);
  } else {
    this->expect(ret2, SIGINT, WaitStatus::SIGNALED, "^C");
  }
}

TEST_F(ProcTest, ptyWinSize) {
  IOConfig config;
  config.in = IOConfig::PTY;
  config.out = IOConfig::PTY;
  config.err = IOConfig::PIPE;

  auto handle = ProcBuilder::spawn(config, [&] {
    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
  });

  // default winsize
  auto winsize = handle.getWinSize();
  ASSERT_EQ(80, winsize.cols);
  ASSERT_EQ(24, winsize.rows);

  // change winsize
  ASSERT_TRUE(handle.setWinSize({.rows = 50, .cols = 200}));
  winsize = handle.getWinSize();
  ASSERT_EQ(200, winsize.cols);
  ASSERT_EQ(50, winsize.rows);

  handle.kill(SIGINT);
  auto ret2 = handle.waitAndGetResult(false);
  ASSERT_NO_FATAL_FAILURE(this->expect(ret2, SIGINT, WaitStatus::SIGNALED));
}

TEST_F(ProcTest, timeout) {
  IOConfig config;
  auto handle = ProcBuilder::spawn(config, [] {
    FILE *fp = fopen("/dev/null", "w");
    if (!fp) {
      fatal_perror("open failed");
    }
    while (true) {
      fputs("do nothing", fp);
    }
    return 0;
  });
  handle.waitWithTimeout(2);
  ASSERT_TRUE(handle);
  handle.kill(SIGTERM);
  auto ret = handle.waitAndGetResult(false);
  ASSERT_NO_FATAL_FAILURE(this->expect(ret, SIGTERM, WaitStatus::SIGNALED));
}

TEST(ANSITest, base) {
  Screen screen;
  std::string line = "abc";
  screen.interpret(line.c_str(), line.size());
  ASSERT_EQ("abc", screen.toString());

  screen = Screen();
  line = "a\bbc";
  screen.interpret(line.c_str(), line.size());
  ASSERT_EQ("bc", screen.toString());

  screen = Screen();
  line = "ab\x1b[2Dc";
  screen.interpret(line.c_str(), line.size());
  ASSERT_EQ("cb", screen.toString());

  screen = Screen();
  line = "abcdef\x1b[3D\x1b[0K0";
  screen.interpret(line.c_str(), line.size());
  ASSERT_EQ("abc0", screen.toString());

  screen = Screen();
  line = "1\r\n2\r\n3\r\n\x1b[2A\x1b[0J";
  screen.interpret(line.c_str(), line.size());
  ASSERT_EQ("1\n2", screen.toString());

  screen = Screen();
  line = "abcdef\x1b[2J";
  screen.interpret(line.c_str(), line.size());
  ASSERT_EQ("", screen.toString());

  screen = Screen();
  line = "a\x1b[0123456789:;<=>? !\"#$%&'()*+,-./\\b"; // ignore unsupported CSI
  screen.interpret(line.c_str(), line.size());
  ASSERT_EQ("ab", screen.toString());

  unsigned int bellCount = 0;
  screen = Screen();
  screen.setBellCallback([&bellCount] { bellCount++; });
  line = "\a12\a3";
  screen.interpret(line.c_str(), line.size());
  ASSERT_EQ("123", screen.toString());
  ASSERT_EQ(2, bellCount);

  std::string rep;
  screen = Screen();
  screen.setReporter([&](std::string &&m) { rep = std::move(m); });
  line = "abcdef\x1b[2J\x1b[6n0";
  screen.interpret(line.c_str(), line.size());
  ASSERT_EQ("      0", screen.toString());
  ASSERT_EQ("\x1b[1;7R", rep);

  rep = "";
  screen = Screen();
  screen.setReporter([&](std::string &&m) { rep = std::move(m); });
  line = "abc\x1b[H\x1b[6nd";
  screen.interpret(line.c_str(), line.size());
  ASSERT_EQ("dbc", screen.toString());
  ASSERT_EQ("\x1b[1;1R", rep);

  rep = "";
  screen = Screen();
  screen.setReporter([&](std::string &&m) { rep = std::move(m); });
  line = "abc\x1b[H\x1b[1C\x1b[6nd";
  screen.interpret(line.c_str(), line.size());
  ASSERT_EQ("adc", screen.toString());
  ASSERT_EQ("\x1b[1;2R", rep);
}

TEST(ANSITest, utf8) {
  Screen screen;
  std::string line = "あ13gｵ";
  screen.interpret(line.c_str(), line.size());
  ASSERT_EQ("あ13gｵ", screen.toString());
}

TEST(ANSITest, width) {
  Screen screen;
  std::string line = "1○\x1b[6n";
  std::string rep;
  screen.setReporter([&](std::string &&m) { rep = std::move(m); });
  screen.interpret(line.c_str(), line.size());
  ASSERT_EQ("1○", screen.toString());
  ASSERT_EQ("\x1b[1;3R", rep);

  screen = Screen();
  screen.setEAW(arsh::AmbiguousCharWidth::FULL);
  rep = "";
  screen.setReporter([&](std::string &&m) { rep = std::move(m); });
  screen.interpret(line.c_str(), line.size());
  ASSERT_EQ("1○", screen.toString());
  ASSERT_EQ("\x1b[1;4R", rep);
}

TEST(ANSITest, resize) {
  Screen screen;
  screen.setCursor({.row = 20, .col = 20});
  ASSERT_EQ(20, screen.getCursor().row);
  ASSERT_EQ(20, screen.getCursor().col);

  screen.resize({.row = 21, .col = 21});
  ASSERT_EQ(20, screen.getCursor().row);
  ASSERT_EQ(20, screen.getCursor().col);

  screen.resize({.row = 20, .col = 20});
  ASSERT_EQ(20, screen.getCursor().row);
  ASSERT_EQ(20, screen.getCursor().col);

  screen.resize({.row = 19, .col = 19});
  ASSERT_EQ(19, screen.getCursor().row);
  ASSERT_EQ(19, screen.getCursor().col);
}

TEST(ANSITest, listener) {
  // CSI
  {
    Screen screen;
    std::string seq;
    screen.setCSIListener([&seq](arsh::StringRef ref) { seq = ref.toString(); });
    arsh::StringRef line = "\x1b[27~11";
    screen.interpret(line.data(), line.size());
    ASSERT_EQ("11", screen.toString());
    ASSERT_EQ("\x1b[27~", seq);
  }

  // OSC
  {
    Screen screen;
    std::string seq;
    screen.setOSCListener([&seq](arsh::StringRef ref) { seq = ref.toString(); });
    arsh::StringRef line = "\x1b]133;A\x1b\\@@";
    screen.interpret(line.data(), line.size());
    ASSERT_EQ("@@", screen.toString());
    ASSERT_EQ("\x1b]133;A", seq);
  }

  {
    Screen screen;
    std::string seq;
    screen.setOSCListener([&seq](arsh::StringRef ref) { seq = ref.toString(); });
    arsh::StringRef line = "\x1b]133;D;12\a12@";
    screen.interpret(line.data(), line.size());
    ASSERT_EQ("12@", screen.toString());
    ASSERT_EQ("\x1b]133;D;12", seq);
  }

  // FTCS
  {
    Screen screen;
    std::string seq;
    std::string raw;
    Screen::FTCS ftcs = Screen::FTCS::UNRECOGNIZED;
    std::string param;
    screen.setOSCListener([&seq](arsh::StringRef ref) { seq = ref.toString(); });
    screen.setFTCSListener(
        [&raw, &ftcs, &param](arsh::StringRef raw0, Screen::FTCS ftcs0, arsh::StringRef param0) {
          raw = raw0.toString();
          ftcs = ftcs0;
          param = param0.toString();
        });
    arsh::StringRef line = "\x1b]133;D;12\a12@";
    screen.interpret(line.data(), line.size());
    ASSERT_EQ("12@", screen.toString());
    ASSERT_EQ("", seq);
    ASSERT_EQ("\x1b]133;D;12", raw);
    ASSERT_EQ(";12", param);
    ASSERT_EQ(Screen::FTCS::COMMAND_FINISHED, ftcs);

    // unrecognized
    line = "\x1b]133;Q;12\a12@";
    screen.interpret(line.data(), line.size());
    ASSERT_EQ("", seq);
    ASSERT_EQ("\x1b]133;Q;12", raw);
    ASSERT_EQ(";12", param);
    ASSERT_EQ(Screen::FTCS::UNRECOGNIZED, ftcs);
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}