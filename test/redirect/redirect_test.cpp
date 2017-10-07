#include <gtest/gtest.h>

#include <misc/files.h>
#include <misc/util.hpp>
#include "../test_common.h"


#ifndef REDIRECT_TEST_DIR
#define REDIRECT_TEST_DIR "."
#endif

#ifndef BIN_PATH
#define BIN_PATH "./ydsh"
#endif


using namespace ydsh;

class RedirectTestOLD : public ::testing::TestWithParam<std::string> {
private:
    std::string targetName;

public:
    RedirectTestOLD() = default;
    virtual ~RedirectTestOLD() = default;

    virtual void SetUp() {
        this->targetName = this->GetParam();
    }

    virtual void TearDown() { }

    virtual void doTest() {
        SCOPED_TRACE("");

        std::string cmd("bash ");
        cmd += this->targetName;
        cmd += " ";
        cmd += BIN_PATH;

        int status = system(cmd.c_str());
        ASSERT_EQ(0, status);
    }
};


TEST_P(RedirectTestOLD, base) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->doTest();
    });
}

INSTANTIATE_TEST_CASE_P(RedirectTest, RedirectTestOLD, ::testing::ValuesIn(getFileList(REDIRECT_TEST_DIR, true)));

class RedirectTest : public ::testing::Test, public TempFileFactory {
private:
    std::string targetName;

public:
    RedirectTest() = default;
    virtual ~RedirectTest() = default;

    virtual void SetUp() {
        this->createTemp();
        this->targetName += this->getTmpDirName();
        this->targetName += "/target";
    }

    virtual void TearDown() {
        this->deleteTemp();
    }

    const char *getTargetName() const {
        return this->targetName.c_str();
    }

    void expect(ProcBuilder &&builder, int status, const char *out = "", const char *err = "") {
        SCOPED_TRACE("");

        auto result = builder.execAndGetResult(false);

        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(status, result.status));

        if(out != nullptr) {
            ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(out, result.out.c_str()));
        }
        if(err != nullptr) {
            ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(err, result.err.c_str()));
        }
    }

    void contentEq(const char *str) {
        SCOPED_TRACE("");

        // read file contents
        ByteBuffer buffer;
        char data[256];
        FILE *fp = fopen(this->getTargetName(), "r");
        ASSERT_TRUE(fp != nullptr);
        int fd = fileno(fp);
        while(true) {
            int readSize = read(fd, data, arraySize(data));
            if(readSize) {
                if(readSize > 0) {
                    buffer.append(data, readSize);
                }
                if(readSize == -1 && (errno == EAGAIN || errno == EINTR)) {
                    continue;
                }
                if(readSize <= 0) {
                    break;
                }
            }
        }
        fclose(fp);

        // compare
        std::string content(buffer.get(), buffer.size());
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(str, content.c_str()));
    }
};

#define CL(...) ProcBuilder {BIN_PATH, "-c", format(__VA_ARGS__).c_str()}

TEST_F(RedirectTest, stdin) {
    // create target file
    ProcBuilder builder = {
            "sh", "-c", format("echo hello world > %s", this->getTargetName()).c_str(),
    };
    int s = builder.exec();
    s = WEXITSTATUS(s);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(s, 0));

    // builtin
    ASSERT_NO_FATAL_FAILURE(this->expect(CL("__gets < %s", this->getTargetName()), 0, "hello world\n"));

    // external
    ASSERT_NO_FATAL_FAILURE(this->expect(CL("grep < %s 'hello world'", this->getTargetName()), 0, "hello world\n"));

    // user-defined
    ASSERT_NO_FATAL_FAILURE(this->expect(CL("cat2() { cat; }; cat2 < %s", this->getTargetName()), 0, "hello world\n"));

    // eval
    ASSERT_NO_FATAL_FAILURE(this->expect(CL("eval __gets < %s", this->getTargetName()), 0, "hello world\n"));
    ASSERT_NO_FATAL_FAILURE(this->expect(CL("eval grep < %s 'hello world'", this->getTargetName()), 0, "hello world\n"));
    ASSERT_NO_FATAL_FAILURE(this->expect(CL("cat2() { cat; }; eval cat2 < %s", this->getTargetName()), 0, "hello world\n"));

    // with
    ASSERT_NO_FATAL_FAILURE(this->expect(CL("{ grep 'hello world'; } with < %s", this->getTargetName()), 0, "hello world\n"));

    // command
    ASSERT_NO_FATAL_FAILURE(this->expect(CL("command __gets < %s", this->getTargetName()), 0, "hello world\n"));
    ASSERT_NO_FATAL_FAILURE(this->expect(CL("command grep < %s 'hello world'", this->getTargetName()), 0, "hello world\n"));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}