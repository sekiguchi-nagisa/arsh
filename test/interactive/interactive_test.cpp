#include <gtest/gtest.h>

#include <misc/files.h>

#ifndef INTERACTIVE_TEST_DIR
#define INTERACTIVE_TEST_DIR "."
#endif

#ifndef INTERACTIVE_TEST_WORK_DIR
#define INTERACTIVE_TEST_WORK_DIR "."
#endif


#ifndef BIN_PATH
#define BIN_PATH "./ydsh"
#endif

using namespace ydsh;

class InteractiveTest : public ::testing::TestWithParam<std::string> {
private:
    std::string targetName;

public:
    InteractiveTest() = default;
    virtual ~InteractiveTest() = default;

    virtual void SetUp() {
        this->targetName = this->GetParam();
    }

    virtual void TearDown() { }

    virtual void doTest() {
        SCOPED_TRACE("");

        std::string cmd("expect ");
        cmd += this->targetName;
        cmd += " ";
        cmd += BIN_PATH;
        cmd += " ";
        cmd += INTERACTIVE_TEST_WORK_DIR;

        int status = system(cmd.c_str());
        ASSERT_EQ(0, status);
    }
};


TEST_P(InteractiveTest, base) {
    ASSERT_NO_FATAL_FAILURE({
    SCOPED_TRACE("");
        this->doTest();
    });
}

INSTANTIATE_TEST_CASE_P(InteractiveTest, InteractiveTest, ::testing::ValuesIn(getFileList(INTERACTIVE_TEST_DIR, true)));

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}