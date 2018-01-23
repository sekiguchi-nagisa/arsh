#include "gtest/gtest.h"

#include "../test_common.h"

class ProcTest : public ::testing::Test {
public:
    void expect(const Output &output, int status = 0,
                WaitStatus::Kind type = WaitStatus::EXITED,
                const char *out = "", const char *err = "") {
        SCOPED_TRACE("");

        ASSERT_EQ(status, output.status.value);
        ASSERT_EQ(type, output.status.kind);
        ASSERT_EQ(out, output.out);
        ASSERT_EQ(err, output.err);
    }
};

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


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}