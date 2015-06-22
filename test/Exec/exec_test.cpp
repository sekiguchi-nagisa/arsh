#include <gtest/gtest.h>

#include <ydsh/ydsh.h>
#include <misc/files.h>
#include <directive.h>


#ifndef EXEC_TEST_DIR
#define EXEC_TEST_DIR "."
#endif

using namespace ydsh;
using namespace ydsh::directive;

class ExecTest : public ::testing::TestWithParam<std::string> {
private:
    std::string targetName;
    DSContext *ctx;

public:
    ExecTest()  : targetName(), ctx(DSContext_create()) {
    }

    virtual ~ExecTest() = default;

    virtual void SetUp() {
        this->targetName = this->GetParam();
    }

    virtual void TearDown() {
        this->targetName.clear();
    }

    virtual const std::string &getSourceName() {
        return this->targetName;
    }

    virtual void doTest() {
        SCOPED_TRACE("");

        Directive d;
        bool s = Directive::init(this->getSourceName().c_str(), d);
        ASSERT_TRUE(s);

        const char *scriptName = this->getSourceName().c_str();
        FILE *fp = fopen(scriptName, "r");
        ASSERT_TRUE(fp != nullptr);

        DSStatus *status;
        DSContext_loadAndEval(this->ctx, scriptName, fp, &status);

        ASSERT_EQ(d.getResult(), DSStatus_getType(status));

        if(d.getResult() == DS_STATUS_EXIT) {
            ASSERT_EQ(d.getStatus(), DSContext_getExitStatus(ctx));
        }
    }
};

TEST_P(ExecTest, baseTest) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->doTest();
    });
}

INSTANTIATE_TEST_CASE_P(ExecTest, ExecTest, ::testing::ValuesIn(getFileList(EXEC_TEST_DIR, true)));

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}