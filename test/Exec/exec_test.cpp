#include <gtest/gtest.h>

#include <exe/Shell.h>
#include <misc/files.h>


#ifndef EXEC_TEST_DIR
#define EXEC_TEST_DIR "."
#endif

#ifndef EXEC_TEST_BIN
#define EXEC_TEST_BIN "./ydsh"
#endif

class ExecTest : public ::testing::TestWithParam<const char *> {
private:
    std::string targetName;

public:
    ExecTest() : targetName() {
    }

    virtual void SetUp() {
        this->targetName += EXEC_TEST_DIR;
        this->targetName += "/";
        this->targetName += this->GetParam();
    }

    virtual void TearDown() {
        this->targetName.clear();
    }

    virtual const std::string &getSourceName() {
        return this->targetName;
    }

    virtual void doTest() {
        SCOPED_TRACE("");

        std::string cmd;
        cmd += EXEC_TEST_BIN;
        cmd += " ";
        cmd += this->getSourceName();

        int status = system(cmd.c_str());
        ASSERT_EQ(0, status);
    }
};

TEST_P(ExecTest, baseTest) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->doTest();
    });
}

INSTANTIATE_TEST_CASE_P(ExecTest, ExecTest, ::testing::ValuesIn(getFileList(EXEC_TEST_DIR)));

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}