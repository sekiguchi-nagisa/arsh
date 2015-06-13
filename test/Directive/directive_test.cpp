#include <gtest/gtest.h>

#include <directive.h>

using namespace ydsh::directive;

class DirectiveTest : public ::testing::Test {
private:
    Directive d;

public:
    DirectiveTest() = default;
    virtual ~DirectiveTest() = default;

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    virtual void parse(const char *line, bool status) {
        SCOPED_TRACE("");
        ASSERT_TRUE(line != nullptr);

        bool s = Directive::init("(dummy)", line, this->d);
        ASSERT_EQ(status, s);
    }

    virtual const Directive &getDirective() {
        return this->d;
    }
};

/**
 * check default result
 */
TEST_F(DirectiveTest, empty1) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#!/usr/bin/ydsh", true);
        ASSERT_EQ(ydsh::ExecStatus::SUCCESS, this->getDirective().getResult());
        ASSERT_EQ(0, this->getDirective().getParams().size());
    });
}

TEST_F(DirectiveTest, empty2) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("fhreuifre", true);
        ASSERT_EQ(ydsh::ExecStatus::SUCCESS, this->getDirective().getResult());
        ASSERT_EQ(0, this->getDirective().getParams().size());
    });
}

TEST_F(DirectiveTest, empty3) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#$tesd", true);
    });
}

TEST_F(DirectiveTest, fail1) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#$test", false);
    });
}

TEST_F(DirectiveTest, fail2) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#$test($result = \"SUCCESS\")", false);
    });
}

TEST_F(DirectiveTest, fail3) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#$test($params = [23, '34'])", false);
    });
}

TEST_F(DirectiveTest, result1) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#$test($result = 'SUCCESS')", true);
        ASSERT_EQ(ydsh::ExecStatus::SUCCESS, this->getDirective().getResult());
    });
}

TEST_F(DirectiveTest, result2) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#$test($result = 'success')", true);
        ASSERT_EQ(ydsh::ExecStatus::SUCCESS, this->getDirective().getResult());
    });
}

TEST_F(DirectiveTest, result3) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#$test($result = 'TYPE_ERROR')", true);
        ASSERT_EQ(ydsh::ExecStatus::TYPE_ERROR, this->getDirective().getResult());
    });
}

TEST_F(DirectiveTest, result4) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#$test($result = 'type')", true);
        ASSERT_EQ(ydsh::ExecStatus::TYPE_ERROR, this->getDirective().getResult());
    });
}

TEST_F(DirectiveTest, result5) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#$test($result = 'PARSE_ERROR')", true);
        ASSERT_EQ(ydsh::ExecStatus::PARSE_ERROR, this->getDirective().getResult());
    });
}

TEST_F(DirectiveTest, result6) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#$test($result = 'parse')", true);
        ASSERT_EQ(ydsh::ExecStatus::PARSE_ERROR, this->getDirective().getResult());
    });
}

TEST_F(DirectiveTest, result7) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#$test($result = 'RUNTIME_ERROR'", true);
        ASSERT_EQ(ydsh::ExecStatus::RUNTIME_ERROR, this->getDirective().getResult());
    });
}

TEST_F(DirectiveTest, result8) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#$test($result = 'runtime')", true);
        ASSERT_EQ(ydsh::ExecStatus::RUNTIME_ERROR, this->getDirective().getResult());
    });
}

TEST_F(DirectiveTest, result9) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#$test($result = 'THROW')", true);
        ASSERT_EQ(ydsh::ExecStatus::RUNTIME_ERROR, this->getDirective().getResult());
    });
}

TEST_F(DirectiveTest, result10) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#$test($result = 'throw'", true);
        ASSERT_EQ(ydsh::ExecStatus::RUNTIME_ERROR, this->getDirective().getResult());
    });
}

TEST_F(DirectiveTest, result11) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#$test($result = 'ASSERTION_ERROR')", true);
        ASSERT_EQ(ydsh::ExecStatus::ASSERTION_ERROR, this->getDirective().getResult());
    });
}

TEST_F(DirectiveTest, result12) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#$test($result = 'ASSERT')", true);
        ASSERT_EQ(ydsh::ExecStatus::ASSERTION_ERROR, this->getDirective().getResult());
    });
}

TEST_F(DirectiveTest, result13) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#$test($result = 'assert')", true);
        ASSERT_EQ(ydsh::ExecStatus::ASSERTION_ERROR, this->getDirective().getResult());
    });
}

TEST_F(DirectiveTest, result14) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#$test($result = 'EXIT')", true);
        ASSERT_EQ(ydsh::ExecStatus::EXIT, this->getDirective().getResult());
    });
}

TEST_F(DirectiveTest, result15) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#$test($result = 'exit')", true);
        ASSERT_EQ(ydsh::ExecStatus::EXIT, this->getDirective().getResult());
    });
}

TEST_F(DirectiveTest, param) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->parse("#$test($params = ['1', 'hello'])", true);
        ASSERT_EQ(ydsh::ExecStatus::SUCCESS, this->getDirective().getResult());
        ASSERT_EQ(2, this->getDirective().getParams().size());
        ASSERT_STREQ("1", this->getDirective().getParams()[0].c_str());
        ASSERT_STREQ("hello", this->getDirective().getParams()[1].c_str());
    });
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}