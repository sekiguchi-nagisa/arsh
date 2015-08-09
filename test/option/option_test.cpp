#include <gtest/gtest.h>

#include <misc/files.h>


#ifndef OPTION_TEST_DIR
#define OPTION_TEST_DIR "."
#endif

#ifndef BIN_PATH
#define BIN_PATH "./ydsh"
#endif

using namespace ydsh::misc;

class OptionTest : public ::testing::TestWithParam<std::string> {
private:
    std::string targetName;

public:
    OptionTest() = default;
    virtual ~OptionTest() = default;

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


TEST_P(OptionTest, base) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->doTest();
    });
}

INSTANTIATE_TEST_CASE_P(OptionTest, OptionTest, ::testing::ValuesIn(getFileList(OPTION_TEST_DIR, true)));

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}