#include "gtest/gtest.h"

#include "../test_common.h"

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

//static std::string

TEST_F(ProcTest, pty) {
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
        printf("hello pty\n");
        printf("!!");
        return 0;
    });
    ASSERT_NO_FATAL_FAILURE(this->expect(ret, 0, WaitStatus::EXITED, "hello pty\n!!"));

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
    write(handle.in(), str.c_str(), str.size());
    auto ret2 = handle.waitAndGetResult(false);
    ASSERT_NO_FATAL_FAILURE(this->expect(ret2, 0, WaitStatus::EXITED, "hello\n"));
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}