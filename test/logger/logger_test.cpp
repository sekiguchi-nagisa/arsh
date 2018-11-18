#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include <thread>

#include "../test_common.h"
#include <misc/logger_base.hpp>

using namespace ydsh;

struct TestLogger : public ydsh::LoggerBase {
    TestLogger() : ydsh::LoggerBase("testlog") {}
};


struct LoggerTest : public ExpectOutput {
    std::unordered_map<std::string, std::string> envs;

    void addEnv(const char *key, const char *value) {
        this->envs[key] = value;
    }

    template <typename Func>
    Output spawnAndWait(Func func) {
        IOConfig config;
        config.out = IOConfig::PIPE;
        config.err = IOConfig::PIPE;
        return ProcBuilder::spawn(config, [&, func]{
            for(auto &e : this->envs) {
                setenv(e.first.c_str(), e.second.c_str(), 1);
            }
            return func();
        }).waitAndGetResult(false);
    }

    void expectRegex(const Output &output, int status = 0,
                     WaitStatus::Kind type = WaitStatus::EXITED,
                     const char *out = "", const char *err = "") {
        SCOPED_TRACE("");

        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(out != nullptr));
        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(err != nullptr));

        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(status, output.status.value));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(type, output.status.kind));

        ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(output.out, ::testing::MatchesRegex(out)));
        ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(output.err, ::testing::MatchesRegex(err)));
    }
};

#define HEADER "^[1-9][0-9]{3}-[0-1][0-9]-[0-3][0-9] [0-2][0-9]:[0-5][0-9]:[0-5][0-9] \\[[0-9]+\\] "

static std::vector<std::string> split(const std::string &line) {
    std::vector<std::string> values;
    values.emplace_back();
    for(auto &ch : line) {
        if(ch == '\n') {
            values.emplace_back();
        } else {
            values.back() += ch;
        }
    }
    return values;
}

TEST_F(LoggerTest, base) {
    auto ret = this->spawnAndWait([]{
        TestLogger logger;
        if(logger.enabled(TestLogger::FATAL)) {
            printf("hello fatal!!\n");
            fflush(stdout);
        }
        logger(TestLogger::FATAL, "broken!!");
        return 0;
    });
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ret, SIGABRT, WaitStatus::SIGNALED, "^hello fatal!!\n$", HEADER"\\[fatal\\] broken!!\n$"));


    auto func = []{
        TestLogger logger;
        logger(TestLogger::ERROR, "error!!");
        if(logger.enabled(TestLogger::ERROR)) {
            printf("hello error!!\n");
        }
        logger(TestLogger::WARNING, "warning!!");
        if(logger.enabled(TestLogger::WARNING)) {
            printf("hello warning!!\n");
        }
        logger(TestLogger::INFO, "info!!");
        if(logger.enabled(TestLogger::INFO)) {
            printf("hello info!!\n");
        }
        return 0;
    };

    this->envs.clear();
    this->addEnv("testlog_LEVEL", "ERROR");
    ret = this->spawnAndWait(func);
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ret, 0, WaitStatus::EXITED, "^hello error!!\n$", HEADER"\\[error\\] error!!\n$"));

    this->envs.clear();
    this->addEnv("testlog_LEVEL", "INFO");
    ret = this->spawnAndWait(func);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, ret.status.value));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(WaitStatus::EXITED, ret.status.kind));

    auto outs = split(ret.out);
    outs.pop_back();
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3, outs.size()));

    auto errs = split(ret.err);
    errs.pop_back();
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3, errs.size()));

    const char *kind[] = {
            "error", "warning", "info"
    };
    for(unsigned int i = 0; i < 3; i++) {
        auto outPattern = format("^hello %s!!$", kind[i]);
        auto errPattern = format(HEADER"\\[%s\\] %s!!$", kind[i], kind[i]);

        ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(outs[i], ::testing::MatchesRegex(outPattern)));
        ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(errs[i], ::testing::MatchesRegex(errPattern)));
    }
}

TEST_F(LoggerTest, thread) {
    this->addEnv("testlog_LEVEL", "INFO");
    auto ret = this->spawnAndWait([]{
        TestLogger logger;
        auto t1 = std::thread([&]{
            logger(TestLogger::INFO, "thread1-1");
            logger(TestLogger::INFO, "thread1-2");
        });

        auto t2 = std::thread([&]{
            logger(TestLogger::WARNING, "thread2-1");
            logger(TestLogger::WARNING, "thread2-2");
        });

        t1.join();
        t2.join();
        return 0;
    });

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, ret.status.value));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(WaitStatus::EXITED, ret.status.kind));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", ret.out));

    auto errs = split(ret.err);
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(errs.empty()));
    errs.pop_back();
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4, errs.size()));

    for(auto &e : errs) {
        const char *pattern = HEADER"\\[(info|warning)\\] thread[12]-[12]$";
        ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(e, ::testing::MatchesRegex(pattern)));
    }
}

TEST_F(LoggerTest, appender) {
    this->addEnv("testlog_LEVEL", "INFO");
    this->addEnv("testlog_APPENDER", "/dev/null");

    auto ret = this->spawnAndWait([]{
        TestLogger logger;
        logger(TestLogger::INFO, "hello!!");
        logger(TestLogger::ERROR, "world!!");
        return 0;
    });
    ASSERT_NO_FATAL_FAILURE(this->expect(ret, 0));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
