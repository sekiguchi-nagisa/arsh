#include "gtest/gtest.h"

#include <misc/files.h>

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

INSTANTIATE_TEST_CASE_P(InteractiveTest, InteractiveTest, ::testing::ValuesIn(getSortedFileList(INTERACTIVE_TEST_DIR)));

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}