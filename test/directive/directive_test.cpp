#include <climits>

#include "gtest/gtest.h"

#include <directive.h>

using namespace ydsh::directive;

class DirectiveTest : public ::testing::Test {
private:
    Directive d;

public:
    DirectiveTest() = default;

    virtual void parse(const char *line, bool status) {
        SCOPED_TRACE("");
        ASSERT_TRUE(line != nullptr);

        bool s = Directive::init(this->getSourceName(), line, this->d);
        ASSERT_EQ(status, s);
    }

    virtual const Directive &getDirective() {
        return this->d;
    }

    const char *getSourceName() const {
        return "(dummy)";
    }
};

/**
 * check default result
 */
TEST_F(DirectiveTest, empty1) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#!/usr/bin/ydsh", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_SUCCESS, this->getDirective().getResult()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, this->getDirective().getParams().size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, this->getDirective().getLineNum()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("", this->getDirective().getErrorKind().c_str()));
}

TEST_F(DirectiveTest, empty2) {
    ASSERT_NO_FATAL_FAILURE(this->parse("fhreuifre", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_SUCCESS, this->getDirective().getResult()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, this->getDirective().getParams().size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, this->getDirective().getLineNum()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("", this->getDirective().getErrorKind().c_str()));
}

TEST_F(DirectiveTest, empty3) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$tesd", true));
}

TEST_F(DirectiveTest, fail1) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test", false));
}

TEST_F(DirectiveTest, fail2) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($params = [23, '34'])", false));
}

TEST_F(DirectiveTest, fail3) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($status = -23)", false));
}

TEST_F(DirectiveTest, fail4) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($result = 123)", false));
}

TEST_F(DirectiveTest, fail5) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($result = [23])", false));
}

TEST_F(DirectiveTest, fail6) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($params = 23)", false));
}

TEST_F(DirectiveTest, fail7) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($params = 'grt')", false));
}

TEST_F(DirectiveTest, fail8) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($result = 'exit', $result = 'success')", false));
}

TEST_F(DirectiveTest, fail9) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($status = 1 + 2)", false));
}

TEST_F(DirectiveTest, fail10) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($fileName = 'hgoiehruhfr')", false));
}

TEST_F(DirectiveTest, result1) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($result = 'SUCCESS')", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_SUCCESS, this->getDirective().getResult()));
}

TEST_F(DirectiveTest, result2) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($result = 'SUCcESS')", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_SUCCESS, this->getDirective().getResult()));
}

TEST_F(DirectiveTest, result3) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($result = 'success')", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_SUCCESS, this->getDirective().getResult()));
}

TEST_F(DirectiveTest, result4) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($result = 'TYPE_ERROR')", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_TYPE_ERROR, this->getDirective().getResult()));
}

TEST_F(DirectiveTest, result5) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($result = 'type')", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_TYPE_ERROR, this->getDirective().getResult()));
}

TEST_F(DirectiveTest, result6) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($result = 'PARSE_ERROR')", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_PARSE_ERROR, this->getDirective().getResult()));
}

TEST_F(DirectiveTest, result7) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($result = 'parse')", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_PARSE_ERROR, this->getDirective().getResult()));
}

TEST_F(DirectiveTest, result8) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($result = 'RUNTIME_ERROR')", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_RUNTIME_ERROR, this->getDirective().getResult()));
}

TEST_F(DirectiveTest, result9) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($result = 'runtime')", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_RUNTIME_ERROR, this->getDirective().getResult()));
}

TEST_F(DirectiveTest, result10) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($result = 'THROW')", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_RUNTIME_ERROR, this->getDirective().getResult()));
}

TEST_F(DirectiveTest, result11) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($result = 'throw')", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_RUNTIME_ERROR, this->getDirective().getResult()));
}

TEST_F(DirectiveTest, result12) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($result = 'ASSERTION_ERROR')", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_ASSERTION_ERROR, this->getDirective().getResult()));
}

TEST_F(DirectiveTest, result13) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($result = 'ASSERT')", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_ASSERTION_ERROR, this->getDirective().getResult()));
}

TEST_F(DirectiveTest, result14) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($result = 'assert')", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_ASSERTION_ERROR, this->getDirective().getResult()));
}

TEST_F(DirectiveTest, result15) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($result = 'EXIT')", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_EXIT, this->getDirective().getResult()));
}

TEST_F(DirectiveTest, result16) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($result = 'exit')", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_EXIT, this->getDirective().getResult()));
}

TEST_F(DirectiveTest, param) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($params = ['1', 'hello'])", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_SUCCESS, this->getDirective().getResult()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, this->getDirective().getParams().size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("1", this->getDirective().getParams()[0].c_str()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("hello", this->getDirective().getParams()[1].c_str()));
}

TEST_F(DirectiveTest, status) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($status = 23)", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(23u, this->getDirective().getStatus()));
}

TEST_F(DirectiveTest, lineNum) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($lineNum = 9)", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(9u, this->getDirective().getLineNum()));
}

TEST_F(DirectiveTest, errorKind) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($errorKind = 'Error')", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("Error", this->getDirective().getErrorKind().c_str()));
}

TEST_F(DirectiveTest, out) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($out = $'hello\\nworld', $err = '12345')", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("hello\nworld", this->getDirective().getOut()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("12345", this->getDirective().getErr()));
}

TEST_F(DirectiveTest, fileName1) {
    char buf[PATH_MAX];
    const char *dir = getcwd(buf, PATH_MAX);
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(dir != nullptr));
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($fileName = './././')", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(!this->getDirective().getFileName().empty()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(dir, this->getDirective().getFileName().c_str()));
}

TEST_F(DirectiveTest, fileName2) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($fileName = $0)", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(!this->getDirective().getFileName().empty()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(this->getSourceName(), this->getDirective().getFileName().c_str()));
}

TEST_F(DirectiveTest, envs1) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($envs = ['hoge' : '1', 'huga' : '2'])", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, this->getDirective().getEnvs().size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("1", this->getDirective().getEnvs().find("hoge")->second));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("2", this->getDirective().getEnvs().find("huga")->second));
}

TEST_F(DirectiveTest, envs2) {
    ASSERT_NO_FATAL_FAILURE(this->parse("#$test($envs = ['hoge' : '1', 'hoge' : '2'])", true));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, this->getDirective().getEnvs().size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("2", this->getDirective().getEnvs().find("hoge")->second));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}