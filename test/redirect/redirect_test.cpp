#include <gtest/gtest.h>

#include <misc/files.h>


#ifndef REDIRECT_TEST_DIR
#define REDIRECT_TEST_DIR "."
#endif

#ifndef BIN_PATH
#define BIN_PATH "./ydsh"
#endif


using namespace ydsh;

class RedirectTest : public ::testing::TestWithParam<std::string> {
private:
    std::string targetName;

public:
    RedirectTest() = default;
    virtual ~RedirectTest() = default;

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


TEST_P(RedirectTest, base) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->doTest();
    });
}

INSTANTIATE_TEST_CASE_P(RedirectTest, RedirectTest, ::testing::ValuesIn(getFileList(REDIRECT_TEST_DIR, true)));

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}