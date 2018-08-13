#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include <csignal>
#include <fstream>

#include <misc/files.h>
#include <misc/num.h>
#include "../test_common.h"

#ifndef INTERACTIVE_TEST_DIR
#error require INTERACTIVE_TEST_DIR
#endif

#ifndef INTERACTIVE_TEST_WORK_DIR
#error require INTERACTIVE_TEST_WORK_DIR
#endif


#ifndef BIN_PATH
#error require BIN_PATH
#endif

using namespace ydsh;

static std::vector<std::string> getSortedFileList(const char *dir) {
    auto ret = getFileList(dir, true);
    assert(!ret.empty());
    std::sort(ret.begin(), ret.end());
    ret.erase(std::unique(ret.begin(), ret.end()), ret.end());
    return ret;
}

static int extractStatus(const std::string &fileName, int defaultValue) {
    std::ifstream input(fileName);
    if(!input) {
        fatal("broken file: %s\n", fileName.c_str());
    }

    for(std::string line; std::getline(input, line);) {
        const char prefix[] = "#@status: ";
        auto *ptr = strstr(line.c_str(), prefix);
        if(ptr == line.c_str()) {
            const char *str = ptr + strlen(prefix);
            int s;
            int v = convertToInt64(str, s);
            if(s != 0) {
                fatal("broken number: %s\n", str);
            }
            return v;
        }
    }
    return defaultValue;
}

class InteractiveTestOld : public ::testing::TestWithParam<std::string> {
private:
    std::string targetName;

public:
    InteractiveTestOld() = default;
    virtual ~InteractiveTestOld() = default;

    virtual void SetUp() {
        this->targetName = this->GetParam();
    }

    virtual void TearDown() { }

    virtual void doTest() {
        SCOPED_TRACE("");

        ProcBuilder builder = {
                "expect",
                this->targetName.c_str(),
                BIN_PATH,
                INTERACTIVE_TEST_WORK_DIR
        };
        auto pair = builder.exec();
        ASSERT_EQ(WaitStatus::EXITED, pair.kind);
        ASSERT_EQ(extractStatus(this->targetName, 0), pair.value);
    }
};


TEST_P(InteractiveTestOld, base) {
    ASSERT_NO_FATAL_FAILURE({
    SCOPED_TRACE("");
        this->doTest();
    });
}

INSTANTIATE_TEST_CASE_P(InteractiveTestOld, InteractiveTestOld, ::testing::ValuesIn(getSortedFileList(INTERACTIVE_TEST_DIR)));

class InteractiveTest : public ExpectOutput {
private:
    ProcHandle handle;

protected:
    virtual void TearDown() {
        if(this->handle) {
            auto pid = this->handle.pid();
            kill(pid, SIGKILL);
        }
    }

    template <typename ... T>
    void invoke(T && ...args) {
        termios term;
        xcfmakesane(term);
        this->handle = ProcBuilder{BIN_PATH, std::forward<T>(args)...}.setWorkingDir(INTERACTIVE_TEST_WORK_DIR)
                .setIn(IOConfig::PTY)
                .setOut(IOConfig::PTY)
                .setErr(IOConfig::PIPE)
                .setTerm(term)();
    }

    void send(const char *str) {
        write(this->handle.in(), str, strlen(str));
        fsync(this->handle.in());
    }

    void expectRegex(const char *out = "", const char *err = "") {
        SCOPED_TRACE("");

        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(out != nullptr));
        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(err != nullptr));

        auto pair = this->handle.readAll(50);
        ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(pair.first, ::testing::MatchesRegex(out)));
        ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(pair.second, ::testing::MatchesRegex(err)));
    }

    void waitAndExpect(int status = 0, WaitStatus::Kind type = WaitStatus::EXITED,
                       const char *out = "", const char *err = "") {
        auto ret = this->handle.waitAndGetResult(false);
        this->expect(ret, status, type, out, err);
    }
};

#define CTRL_C "\x03"
#define CTRL_D "\x04"


TEST_F(InteractiveTest, exit) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expectRegex("ydsh.*"));
    this->send("exit 30\r");
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(30, WaitStatus::EXITED, "exit 30\r\n"));
}

TEST_F(InteractiveTest, ctrld1) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expectRegex("ydsh.*"));
    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\r\n"));
}

TEST_F(InteractiveTest, ctrld2) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expectRegex("ydsh.*"));
    this->send("false\r");
    ASSERT_NO_FATAL_FAILURE(this->expectRegex("false\r\nydsh.*"));
    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\r\n"));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}