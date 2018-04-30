#include "gtest/gtest.h"

#include <fstream>

#include <ydsh/ydsh.h>
#include <misc/files.h>
#include <directive.h>

#include "../test_common.h"


#ifndef EXEC_TEST_DIR
#error require EXEC_TEST_DIR
#endif

#ifndef BIN_PATH
#error require BIN_PATH
#endif

using namespace ydsh;
using namespace ydsh::directive;

// parse config(key = value)
template <typename ...T>
int parse(const std::string &src, T&& ...args) {
    return Extractor(src.c_str())(std::forward<T>(args)...);
}

template <typename ...T>
int parse(const char *src, T&& ...args) {
    return Extractor(src)(std::forward<T>(args)...);
}

static std::vector<std::string> getSortedFileList(const char *dir) {
    auto ret = getFileList(dir, true);
    assert(!ret.empty());
    std::sort(ret.begin(), ret.end());
    ret.erase(std::unique(ret.begin(), ret.end()), ret.end());
    return ret;
}

class ExecTest : public ::testing::TestWithParam<std::string>, public TempFileFactory {
private:
    std::string targetName;

public:
    ExecTest() : targetName() { }

    virtual ~ExecTest() = default;

    virtual void SetUp() {
        this->createTemp();
        this->targetName = this->GetParam();
    }

    virtual void TearDown() {
        this->deleteTemp();
    }

    virtual const std::string &getSourceName() {
        return this->targetName;
    }

    virtual void doTest() {
        SCOPED_TRACE("");

        // create directive
        Directive d;
        bool s = Directive::init(this->getSourceName().c_str(), d);
        ASSERT_TRUE(s);

        // check run condition
        RunCondition haveDBus = DSState_supportDBus() ? RunCondition::TRUE : RunCondition::FALSE;
        if(d.getIfHaveDBus() != RunCondition::IGNORE && haveDBus != d.getIfHaveDBus()) {
            return; // do nothing
        }

        const char *scriptName = this->getSourceName().c_str();
        ProcBuilder builder(BIN_PATH);
        builder.addArg("--status-log").addArg(this->getTmpFileName());

        // set argument
        builder.addArg(scriptName);
        builder.addArgs(d.getParams());

        // set IO config
        if(d.getOut()) {
            builder.setOut(IOConfig::PIPE);
        }
        if(d.getErr()) {
            builder.setErr(IOConfig::PIPE);
        }

        // set working dir
        builder.setWorkingDir(EXEC_TEST_DIR);

        // execute
        auto output = builder().waitAndGetResult(false);
        int ret = output.status.toShellStatus();

        // get internal status
        std::ifstream input(this->getTmpFileName());
        ASSERT_FALSE(!input);

        std::string line;
        std::getline(input, line);
        ASSERT_FALSE(line.empty());

        unsigned int kind;
        unsigned int lineNum;
        std::string name;

        int r = parse(line, "kind", "=", kind, "lineNum", "=", lineNum, "name", "=", name);
        ASSERT_EQ(0, r);

        // check status
        ASSERT_EQ(d.getResult(), kind);
        ASSERT_EQ(d.getLineNum(), lineNum);
        ASSERT_EQ(d.getStatus(), static_cast<unsigned int>(ret));
        ASSERT_EQ(d.getErrorKind(), name);
        if(d.getOut()) {
            ASSERT_STREQ(d.getOut(), output.out.c_str());
        }
        if(d.getErr()) {
            ASSERT_STREQ(d.getErr(), output.err.c_str());
        }
    }
};

TEST_P(ExecTest, baseTest) {
    SCOPED_TRACE("");
    ASSERT_NO_FATAL_FAILURE(this->doTest());
}

INSTANTIATE_TEST_CASE_P(ExecTest, ExecTest, ::testing::ValuesIn(getSortedFileList(EXEC_TEST_DIR)));


TEST(Base, case1) {
    SCOPED_TRACE("");

    std::string line("type=3 lineNum=1 kind=SystemError");
    unsigned int type;
    unsigned int lineNum;
    std::string kind;

    int ret = parse(line, "type", "=", type, "lineNum", "=", lineNum, "kind", "=", kind);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, ret));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, type));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1u, lineNum));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("SystemError", kind));
}

TEST(Base, case2) {
    SCOPED_TRACE("");

    std::string line("type=0 lineNum=0 kind=");
    unsigned int type;
    unsigned int lineNum;
    std::string kind;

    int ret = parse(line, "type", "=", type, "lineNum", "=", lineNum, "kind", "=", kind);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, ret));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, type));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, lineNum));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", kind));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}