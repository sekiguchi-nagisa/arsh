#include "gtest/gtest.h"

#include "../test_common.h"
#include "ansi.h"

class ProcTest : public ExpectOutput {};

template <typename Func>
static Output spawnAndWait(IOConfig config, Func func, bool remove = false) {
    return ProcBuilder::spawn(config, func).waitAndGetResult(remove);
}

TEST_F(ProcTest, status) {
    auto ret = spawnAndWait(IOConfig{}, [&]{
        return 100;
    });
    ASSERT_NO_FATAL_FAILURE(this->expect(ret, 100));

    IOConfig config;
    config.out = IOConfig::PIPE;
    ret = spawnAndWait(config, [&]{
        return 10;
    });
    ASSERT_NO_FATAL_FAILURE(this->expect(ret, 10));

    ret = spawnAndWait(IOConfig{}, [&]()-> int {
        abort();
    });
    ASSERT_NO_FATAL_FAILURE(this->expect(ret, SIGABRT, WaitStatus::SIGNALED));
}

TEST_F(ProcTest, pipe) {
    IOConfig config;
    config.out = IOConfig::PIPE;
    auto ret = spawnAndWait(config, [&]{
       return 10;
    });
    ASSERT_NO_FATAL_FAILURE(this->expect(ret, 10));

    config.err = IOConfig::PIPE;
    ret = spawnAndWait(config, [&]{
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

    auto ret = spawnAndWait(config, [&]{
        if(!isatty(STDIN_FILENO)) {
            return 10;
        }
        if(!isatty(STDOUT_FILENO)) {
            return 20;
        }
        if(isatty(STDERR_FILENO)) {
            return 30;
        }
        return 0;
    });
    ASSERT_NO_FATAL_FAILURE(this->expect(ret));
}

TEST_F(ProcTest, pty2) {
    IOConfig config;
    config.in = IOConfig::PTY;
    config.out = IOConfig::PTY;
    config.err = IOConfig::PIPE;

    auto handle = ProcBuilder::spawn(config, [&]{
        char buf[64];
        auto size = read(STDIN_FILENO, buf, 64);
        if(size > 0) {
            buf[size] = '\0';
            printf("%s\n", buf);
            return 0;
        }
        return 1;
    });
    std::string str = "hello";
    auto r = write(handle.in(), str.c_str(), str.size());
    (void) r;
    auto ret2 = handle.waitAndGetResult(false);
    ASSERT_NO_FATAL_FAILURE(this->expect(ret2, 0, WaitStatus::EXITED, "hello\n"));
}

TEST_F(ProcTest, pty3) {
    IOConfig config;
    config.in = IOConfig::PTY;
    config.out = IOConfig::PTY;

    auto handle = ProcBuilder::spawn(config, [&]{
        char buf[1];
        while(read(STDIN_FILENO, buf, 1) > 0) {
            if(buf[0] == 'p') {
                printf("print\n");
            } else if(buf[0] == 'b') {
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
    (void) r;
    auto output = handle.readAll(20);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("print\n", output.first));

    r = write(handle.in(), "x", 1);
    (void) r;
    output = handle.readAll(20);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("ignore x\n", output.first));

    r = write(handle.in(), "b", 1);
    (void) r;
    auto ret = handle.waitAndGetResult(false);
    ASSERT_NO_FATAL_FAILURE(this->expect(ret, 0, WaitStatus::EXITED, "break!!\n"));
}

TEST(ANSITest, case1) {
    Screen screen;
    std::string line = "abc";
    screen.interpret(line.c_str(), line.size());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("abc", screen.toString()));

    screen = Screen();
    line = "a\bbc";
    screen.interpret(line.c_str(), line.size());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("bc", screen.toString()));

    screen = Screen();
    line = "ab\x1b[2Dc";
    screen.interpret(line.c_str(), line.size());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("cb", screen.toString()));

    screen = Screen();
    line = "abcdef\x1b[3D\x1b[0K0";
    screen.interpret(line.c_str(), line.size());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("abc0", screen.toString()));

    screen = Screen();
    line = "abcdef\x1b[2J";
    screen.interpret(line.c_str(), line.size());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", screen.toString()));

    std::string rep;
    screen = Screen();
    screen.setReporter([&](std::string &&m){
        rep = std::move(m);
    });
    line = "abcdef\x1b[2J\x1b[6n0";
    screen.interpret(line.c_str(), line.size());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("      0", screen.toString()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("\x1b[1;7R", rep));

    rep = "";
    screen = Screen();
    screen.setReporter([&](std::string &&m){
        rep = std::move(m);
    });
    line = "abc\x1b[H\x1b[6nd";
    screen.interpret(line.c_str(), line.size());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("dbc", screen.toString()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("\x1b[1;1R", rep));

    rep = "";
    screen = Screen();
    screen.setReporter([&](std::string &&m){
        rep = std::move(m);
    });
    line = "abc\x1b[H\x1b[1C\x1b[6nd";
    screen.interpret(line.c_str(), line.size());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("adc", screen.toString()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("\x1b[1;2R", rep));
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}