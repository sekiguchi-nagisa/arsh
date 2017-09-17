#include <gtest/gtest.h>
#include <gmock/gmock.h>

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

    virtual void expect(CommandBuilder &&builder, int status, const char *out = "", const char *err = "") {
        SCOPED_TRACE("");

        auto result = builder.execAndGetResult(false);

        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(status, result.status));

        if(out != nullptr) {
            ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(out, result.out.c_str()));
        }
        if(err != nullptr) {
            ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(err, result.err.c_str()));
        }
    }

    virtual void expectRegex(CommandBuilder &&builder, int status, const char *out, const char *err = "") {
        SCOPED_TRACE("");

        auto result = builder.execAndGetResult(false);

        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(status, result.status));

        if(out != nullptr) {
            ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(result.out, ::testing::MatchesRegex(out)));
        }
        if(err != nullptr) {
            ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(result.err, ::testing::MatchesRegex(err)));
        }
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

TEST_F(CmdlineTest, cmd1) {
    this->expect(ds("-c", "assert($0 == 'ydsh')"), 0);
    this->expect(ds("-c", "assert($0 == \"A\"); assert($@.size() == 1); assert($@[0] == \"G\")", "A", "G"), 0);
    this->expect(ds("-c", "assert $SCRIPT_DIR == \"$(pwd -L)\""), 0);
    this->expect(ds("-c", "\\"), 0);    // do nothing
}

TEST_F(CmdlineTest, cmd2) {
    // assertion
    const char *msg = R"(Assertion Error: `(12 == 4)'
    from (string):1 '<toplevel>()'
)";
    this->expect(ds("-c", "assert(12 == 4)"), 1, "", msg);

    // exception
    msg = R"([runtime error]
ArithmeticError: zero division
    from (string):1 '<toplevel>()'
)";
    this->expect(ds("-c", "34 / 0"), 1, "", msg);

    // normal
    msg = R"([runtime error]
SystemError: execution error: lajfeoifreo: command not found
    from (string):1 '<toplevel>()'
)";
    this->expect(ds("-c", "lajfeoifreo"), 1, "", msg);
    this->expect(ds("-c", "__puts -3"), 1);
    this->expect(ds("-c", "echo hello"), 0, "hello\n");

    // exit
    this->expect(ds("-c", "exit 0"), 0);
    this->expect(ds("-c", "exit 66"), 66);

    // exec
    this->expect(ds("-c", "exec > /dev/null; echo hello"), 0);

    // command error
    msg = R"([runtime error]
SystemError: execution error: hoge: command not found
    from (string):1 '<toplevel>()'
)";
    this->expect(ds("-c", "hoge | :"), 0, "", msg);
    this->expect(ds("-c", ": | hoge"), 1, "", msg);
}

TEST_F(CmdlineTest, help) {
    this->expectRegex(ds("--help"), 0, "^.*\nOptions:\n.*$");
    this->expectRegex(ds("--norc", "--help","--version"), 0, "^.*\nOptions:\n.*$");
}

TEST_F(CmdlineTest, illegal) {
    const char *p = R"(^illegal option: --ho
ydsh, version .*, build by .*
Options:
.*$)";

    this->expectRegex(ds("--ho"), 1, "", p);
}

TEST_F(CmdlineTest, exit) {
    this->expectRegex(ds("--trace-exit", "-c", "exit 23"), 23, "", "^Shell Exit: terminated by exit 23\n.*$");
    this->expectRegex(ds("--trace-exit", "-c", "exit 2300"), 2300 % 256, "", "^Shell Exit: terminated by exit 2300\n.*$");
}

TEST_F(CmdlineTest, bytecode) {
    const char *msg = R"(### dump compiled code ###
Source File: (string)
DSCode: top level
  code size: 22
  number of local variable: 0
  number of global variable: 47
Code:
   8: LOAD_CONST  0
  10: STORE_GLOBAL  46
  13: LOAD_CONST  1
  15: CALL_METHOD  0  0
  20: POP
  21: HALT
Constant Pool:
  0: Int32 34
  1: Int32 34
Source Pos Entry:
  lineNum: 1, address: 15, pos: 12
Exception Table:
)";
    this->expect(ds("--dump-code", "-c", "var a = 34; 34 as String"), 0, msg);

    msg = R"(### dump compiled code ###
Source File: (string)
DSCode: top level
  code size: 35
  number of local variable: 0
  number of global variable: 47
Code:
   8: LOAD_CONST  0
  10: STORE_GLOBAL  46
  13: LOAD_GLOBAL  46
  16: LOAD_CONST  1
  18: CALL_FUNC  1
  21: POP
  22: ENTER_FINALLY  8
  25: GOTO  34
  30: LOAD_CONST  2
  32: POP
  33: EXIT_FINALLY
  34: HALT
Constant Pool:
  0: (null) function(f)
  1: Int32 1
  2: Int32 3
Source Pos Entry:
  lineNum: 1, address: 18, pos: 67
Exception Table:
  begin: 13, end: 30, type: Any, dest: 30, offset: 0, size: 0

DSCode: function f
  code size: 18
  number of local variable: 1
Code:
   6: LOAD_LOCAL  0
   8: INSTANCE_OF  Array<Int32>
  17: RETURN_V
Constant Pool:
Source Pos Entry:
Exception Table:
)";
    this->expect(ds("--dump-code", "-c", "function f($a : Any) : Boolean { return $a is Array<Int>; }; try { $f(1) } finally {3}"), 0, msg);
}

TEST_F(CmdlineTest, parse_only) {
    this->expect(ds("--parse-only", "-c", "var a = 34; $a = $a;"), 0);

    // when specified '--parse-only' option, only work '--dump-untyped-ast'
    this->expectRegex(ds("--parse-only", "--dump-untyped-ast", "-c", "var a = 34; $a = $a;"), 0, ".*");
    this->expect(ds("--parse-only", "--dump-ast", "-c", "var a = 34; $a = $a;"), 0);
    this->expect(ds("--parse-only", "--dump-code", "-c", "var a = 34; $a = $a;"), 0);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}