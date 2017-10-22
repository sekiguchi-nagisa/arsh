#include <gtest/gtest.h>

#include <misc/files.h>
#include "../test_common.h"

using namespace ydsh;

class FuzzTest : public ::testing::TestWithParam<std::string> {
private:
    std::string targetName;

public:
    FuzzTest() = default;
    virtual ~FuzzTest() = default;

    virtual void SetUp() {
        this->targetName = this->GetParam();
    }

    virtual void TearDown() { }

    virtual void doTest() {
        SCOPED_TRACE("");

        ProcBuilder builder = {
                BIN_PATH,
                this->targetName.c_str(),
        };
        int status = builder.exec();
        ASSERT_EQ(0, status);
    }
};

INSTANTIATE_TEST_CASE_P(FuzzTest, FuzzTest, ::testing::ValuesIn(getFileList(FUZZ_TEST_DIR, true)));

TEST_P(FuzzTest, base) {
    ASSERT_NO_FATAL_FAILURE(this->doTest());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}