#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include <misc/files.h>
#include "../test_common.h"

#ifndef CMDLINE_TEST_DIR
#define CMDLINE_TEST_DIR "."
#endif

#ifndef BIN_PATH
#define BIN_PATH "./ydsh"
#endif

using namespace ydsh;

class CmdlineTestOld : public ::testing::TestWithParam<std::string> {
private:
    std::string targetName;

public:
    CmdlineTestOld() = default;
    virtual ~CmdlineTestOld() = default;

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

template <typename ... T>
static CommandBuilder ds(const char *arg, T && ...args) {
    return CommandBuilder{BIN_PATH, arg, std::forward<T>(args)...};
}

class CmdlineTest : public ::testing::Test {
public:
    CmdlineTest() = default;
    virtual ~CmdlineTest() = default;

    virtual void SetUp() { }

    virtual void TearDown() { }

    virtual void expect(CommandBuilder &&builder, int status, const char *out, const char *err = "") {
        SCOPED_TRACE("");

        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(out != nullptr));
        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(err != nullptr));

        auto result = builder.execAndGetResult(false);

        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(status, result.status));
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(out, result.out.c_str()));
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(err, result.err.c_str()));
    }

    virtual void expectRegex(CommandBuilder &&builder, int status, const char *out, const char *err = "") {
        SCOPED_TRACE("");

        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(out != nullptr));
        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(err != nullptr));

        auto result = builder.execAndGetResult(false);

        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(status, result.status));
        ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(result.out, ::testing::MatchesRegex(out)));
        ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(result.err, ::testing::MatchesRegex(err)));
    }
};

TEST_P(CmdlineTestOld, base) {
    ASSERT_NO_FATAL_FAILURE(this->doTest());
}

INSTANTIATE_TEST_CASE_P(CmdlineTestOld, CmdlineTestOld, ::testing::ValuesIn(getFileList(CMDLINE_TEST_DIR, true)));

TEST_F(CmdlineTest, assert) {
    // no assert
    this->expect(ds("--disable-assertion", "-c", "assert(12 / 0 == 12)"), 0, "");

    // assert with message
    const char *cmd = R"(assert
    (false)    :
        "hello assertion")";

    const char *msg = R"(Assertion Error: hello assertion
    from (string):2 '<toplevel>()'
)";

    this->expect(ds("-c", cmd), 1, "", msg);

    // assert without message
    msg = R"(Assertion Error: `34 == 43'
    from (string):1 '<toplevel>()'
)";

    this->expect(ds("-c", "assert 34 == 43"), 1, "", msg);
}

TEST_F(CmdlineTest, ast) {
    this->expectRegex(ds("--dump-ast", "-c", "[12, 32] is Array<Int>"), 0, "^### dump typed AST ###.*$");
}

TEST_F(CmdlineTest, uast) {
    this->expectRegex(ds("--dump-untyped-ast", "-c", "12"), 0, "^### dump untyped AST ###.*$");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}