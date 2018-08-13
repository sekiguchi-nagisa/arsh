#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include <csignal>
#include <fstream>

#include <config.h>
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
        int r = write(this->handle.in(), str, strlen(str));
        (void) r;
        fsync(this->handle.in());
    }

    std::pair<std::string, std::string> readAll() {
        return this->handle.readAll(50);
    }

    void expectRegex(const char *out = "", const char *err = "") {
        SCOPED_TRACE("");

        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(out != nullptr));
        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(err != nullptr));

        auto pair = this->readAll();
        ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(pair.first, ::testing::MatchesRegex(out)));
        ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(pair.second, ::testing::MatchesRegex(err)));
    }

    void expect(const char *out = "", const char *err = "") {
        SCOPED_TRACE("");

        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(out != nullptr));
        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(err != nullptr));

        auto pair = this->readAll();
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(out, pair.first));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(err, pair.second));
    }

    void sendAndExpect(const char *str, const char *out = "", const char *err = "") {
        this->send(str);
        this->send("\r");

        std::string eout = str;
        eout += "\r\n";
        eout += out;
        this->expect(eout.c_str(), err);
    }

    void waitAndExpect(int status = 0, WaitStatus::Kind type = WaitStatus::EXITED,
                       const char *out = "", const char *err = "") {
        auto ret = this->handle.waitAndGetResult(false);
        ExpectOutput::expect(ret, status, type, out, err);
    }
};

//static constexpr const char *CTRL_C = "\x03";
static constexpr const char *CTRL_D = "\x04";

#define XSTR(v) #v
#define STR(v) XSTR(v)

static constexpr const char *PROMPT = "ydsh-" STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION) "$ ";

#undef XSTR
#undef STR


TEST_F(InteractiveTest, exit) {
     this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("exit 30"));
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(30, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, ctrld1) {
    this->invoke("--norc");

    ASSERT_NO_FATAL_FAILURE(this->expectRegex("ydsh, version .+, build by .+\nCopy.+\nydsh-.+\\$ "));
    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\r\n"));
}

TEST_F(InteractiveTest, ctrld2) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

    std::string str = "(String) hey\r\n";
    str += PROMPT;

    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("'hey'", str.c_str()));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("false", PROMPT));
    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\r\n"));
}

TEST_F(InteractiveTest, arg) {
    this->invoke("--quiet", "--norc", "-s", "hello", "world");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert $0 == 'ydsh'; assert $1 == 'hello';", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert $@.size() == 2; assert $# == 2;", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert $2 == 'world'", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert $@[0] == 'hello'; assert $@[1] == 'world'; exit"));
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, assert) {
    this->invoke("--quiet", "--norc", "-s", "hello", "world");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

    const char *e = "Assertion Error: `(1 == 2)'\n"
                    "    from (stdin):1 '<toplevel>()'\n";
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert(1 == 2)", "", e));
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED));
}

//TEST_F(InteractiveTest, ctrlc) {
//    this->invoke("--quiet", "--norc");
//
//    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
//
//    std::string str = "throw 34";
//    str += CTRL_C;
//    this->send(str.c_str());
//    ASSERT_NO_FATAL_FAILURE(this->expect("throw 34"));
//    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("exit 0", PROMPT));
//    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED));
//}

TEST_F(InteractiveTest, status) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("eval $(which true)", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert $? == 0", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("eval $(which false)", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert $? == 1", PROMPT));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\r\n"));
}

TEST_F(InteractiveTest, except) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

    const char *estr = R"([runtime error]
ArithmeticError: zero division
    from (stdin):1 '<toplevel>()'
)";

    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("45 / 0", PROMPT, estr));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\r\n"));
}

TEST_F(InteractiveTest, signal) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert ($SIG[%'int'] as String) == $SIG_IGN as String", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert ($SIG[%'quit'] as String) == $SIG_IGN as String", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert ($SIG[%'tstp'] as String) == $SIG_IGN as String", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert ($SIG[%'ttin'] as String) == $SIG_IGN as String", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert ($SIG[%'ttou'] as String) == $SIG_IGN as String", PROMPT));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\r\n"));
}

TEST_F(InteractiveTest, stdin) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert test -t 0;", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert test -t $STDIN", PROMPT));

    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert test -t 1;", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert test -t $STDOUT", PROMPT));

    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert !test -t 2;", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert !test -t $STDERR", PROMPT));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\r\n"));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}