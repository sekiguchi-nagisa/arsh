#include "gtest/gtest.h"

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

        ProcBuilder builder = {
                "expect",
                this->targetName.c_str(),
                BIN_PATH,
                INTERACTIVE_TEST_WORK_DIR
        };
        auto pair = inspectStatus(builder.exec());
        ASSERT_EQ(WaitType::EXITED, pair.second);
        ASSERT_EQ(extractStatus(this->targetName, 0), pair.first);
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