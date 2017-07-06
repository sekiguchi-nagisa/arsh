#include <gtest/gtest.h>

#include <misc/files.h>


#ifndef CMDLINE_TEST_DIR
#define CMDLINE_TEST_DIR "."
#endif

#ifndef BIN_PATH
#define BIN_PATH "./ydsh"
#endif

using namespace ydsh;

class CmdlineTest : public ::testing::TestWithParam<std::string> {
private:
    std::string targetName;

public:
    CmdlineTest() = default;
    virtual ~CmdlineTest() = default;

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


TEST_P(CmdlineTest, base) {
    ASSERT_NO_FATAL_FAILURE(this->doTest());
}

INSTANTIATE_TEST_CASE_P(CmdlineTest, CmdlineTest, ::testing::ValuesIn(getFileList(CMDLINE_TEST_DIR, true)));

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}