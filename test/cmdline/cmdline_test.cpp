#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include <sys/time.h>
#include <sys/resource.h>

#include <config.h>
#include <misc/files.h>
#include <misc/util.hpp>
#include <misc/fatal.h>
#include <misc/flag_util.hpp>
#include "../test_common.h"
#include "../../tools/platform/platform.h"


#ifndef BIN_PATH
#define BIN_PATH "./ydsh"
#endif

#ifndef PID_CHECK_PATH
#error "require PID_CHECK_PATH"
#endif

using namespace ydsh;

template <typename ... T>
static ProcBuilder ds(T && ...args) {
    return ProcBuilder{BIN_PATH, std::forward<T>(args)...};
}

struct InputWrapper {
    std::string value;
    ProcBuilder builder;
};

template <unsigned int N>
InputWrapper operator|(const char (&value)[N], ProcBuilder &&builder) {
    return InputWrapper{
            .value = std::string(value, N - 1),
            .builder = std::forward<ProcBuilder>(builder),
    };
}

class CmdlineTest : public ExpectOutput {
public:
    CmdlineTest() = default;

    using ExpectOutput::expect;

    void expectRegex(ProcBuilder &&builder, int status, const char *out, const char *err = "") {
        SCOPED_TRACE("");

        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(out != nullptr));
        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(err != nullptr));

        auto result = builder.execAndGetResult(false);

        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(status, result.status.value));

        ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(result.out, ::testing::MatchesRegex(out)));
        ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(result.err, ::testing::MatchesRegex(err)));
    }

    void expect(InputWrapper &&wrapper, int status, const char *out = "", const char *err = "") {
        SCOPED_TRACE("");

        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(out != nullptr));
        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(err != nullptr));

        auto handle = wrapper.builder
                .setIn(IOConfig::PIPE)
                .setOut(IOConfig::PIPE)
                .setErr(IOConfig::PIPE)();
        if(write(handle.in(), wrapper.value.c_str(), wrapper.value.size()) < 0) {
            fatal_perror("");
        }
        close(handle.in());
        auto result = handle.waitAndGetResult(false);
        ExpectOutput::expect(result, status, WaitStatus::EXITED, out, err);
    }
};

TEST_F(CmdlineTest, assert) {
    // no assert
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("--disable-assertion", "-c", "assert(12 / 0 == 12)"), 0, ""));

    // assert with message
    const char *cmd = R"(assert
    (false)    :
        "hello assertion")";

    const char *msg = R"(Assertion Error: hello assertion
    from (string):2 '<toplevel>()'
)";

    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", cmd), 1, "", msg));

    // assert without message
    msg = R"(Assertion Error: `34 == 43'
    from (string):1 '<toplevel>()'
)";

    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "assert 34 == 43"), 1, "", msg));
}

TEST_F(CmdlineTest, ast) {
    ASSERT_NO_FATAL_FAILURE( this->expectRegex(
            ds("--dump-ast", "-c", "[12, 32] is Array<Int>"), 0, "^### dump typed AST ###.*$"));
}

TEST_F(CmdlineTest, uast) {
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(
            ds("--dump-untyped-ast", "-c", "12"), 0, "^### dump untyped AST ###.*$"));
}

TEST_F(CmdlineTest, cmd1) {
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "assert($0 == 'ydsh')"), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect(
            ds("-c", "assert($0 == \"A\"); assert($@.size() == 1); assert($@[0] == \"G\")", "A", "G"), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "assert $SCRIPT_DIR == \"$(pwd -L)\""), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "\\"), 0));    // do nothing
}

TEST_F(CmdlineTest, cmd2) {
    // assertion
    const char *msg = R"(Assertion Error: `(12 == 4)'
    from (string):1 '<toplevel>()'
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "assert(12 == 4)"), 1, "", msg));

    // exception
    msg = R"([runtime error]
ArithmeticError: zero division
    from (string):1 '<toplevel>()'
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "34 / 0"), 1, "", msg));

    // normal
    msg = R"([runtime error]
SystemError: execution error: lajfeoifreo: command not found
    from (string):1 '<toplevel>()'
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "lajfeoifreo"), 1, "", msg));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "__puts -3"), 1));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "echo hello"), 0, "hello\n"));

    // exit
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "exit 0"), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "exit 66"), 66));

    // exec
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "exec > /dev/null; echo hello"), 0));

    // command error
    msg = R"([runtime error]
SystemError: execution error: hoge: command not found
    from (string):1 '<toplevel>()'
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "hoge | :"), 0, "", msg));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", ": | hoge"), 1, "", msg));
}

TEST_F(CmdlineTest, help) {
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("--help"), 0, "^.*\nOptions:\n.*$"));
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("--norc", "--help","--version"), 0, "^.*\nOptions:\n.*$"));
}

TEST_F(CmdlineTest, illegal) {
    const char *p = R"(^invalid option: --ho
ydsh, version .*, build by .*
Options:
.*$)";

    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("--ho"), 1, "", p));
}

TEST_F(CmdlineTest, exit) {
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(
            ds("--trace-exit", "-c", "exit 23"), 23, "", "^Shell Exit: terminated by exit 23\n.*$"));
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(
            ds("--trace-exit", "-c", "exit 2300"), 2300 % 256, "", "^Shell Exit: terminated by exit 2300\n.*$"));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("--trace-exit", "-e", "exit", "34"), 34));
}

TEST_F(CmdlineTest, bytecode) {
    const char *msg = R"(### dump compiled code ###
Source File: (string)
DSCode: top level
  code size: 22
  max stack depth: 1
  number of local variable: 0
  number of global variable: 50
Code:
   8: LOAD_CONST  0
  10: STORE_GLOBAL  49
  13: LOAD_CONST  1
  15: CALL_METHOD  0  0
  20: POP
  21: HALT
Constant Pool:
  0: Int32 34
  1: Int32 34
Line Number Table:
  lineNum: 1, address: 15
Exception Table:
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("--dump-code", "-c", "var a = 34; 34 as String"), 0, msg));

    msg = R"(### dump compiled code ###
Source File: (string)
DSCode: top level
  code size: 35
  max stack depth: 3
  number of local variable: 0
  number of global variable: 50
Code:
   8: LOAD_CONST  0
  10: STORE_GLOBAL  49
  13: LOAD_GLOBAL  49
  16: LOAD_CONST  1
  18: CALL_FUNC  1
  21: ENTER_FINALLY  8
  24: GOTO  33
  29: LOAD_CONST  2
  31: POP
  32: EXIT_FINALLY
  33: POP
  34: HALT
Constant Pool:
  0: Func<Boolean,[Any]> function(f)
  1: Int32 1
  2: Int32 3
Line Number Table:
  lineNum: 1, address: 18
Exception Table:
  begin: 13, end: 29, type: Any, dest: 29, offset: 0, size: 0

DSCode: function f
  code size: 16
  max stack depth: 1
  number of local variable: 1
Code:
   8: LOAD_LOCAL  0
  10: INSTANCE_OF  Array<Int32>
  15: RETURN_V
Constant Pool:
Line Number Table:
Exception Table:
)";
    const char *s = "function f($a : Any) : Boolean { return $a is Array<Int>; }; try { $f(1) } finally {3}";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("--dump-code", "-c", s), 0, msg));
}

TEST_F(CmdlineTest, parse_only) {
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("--parse-only", "-c", "var a = 34; $a = $a;"), 0));

    // when specified '--parse-only' option, only work '--dump-untyped-ast'
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(
            ds("--parse-only", "--dump-untyped-ast", "-c", "var a = 34; $a = $a;"), 0, ".*"));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("--parse-only", "--dump-ast", "-c", "var a = 34; $a = $a;"), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("--parse-only", "--dump-code", "-c", "var a = 34; $a = $a;"), 0));
}

TEST_F(CmdlineTest, check_only) {
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("--check-only", "-c", "exit 88"), 0));

    // when specified '--check-only' option, only work '--dump-untyped-ast' and '--dump-ast'
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("--check-only", "--dump-untyped-ast", "-c", "exit 88"), 0, ".*"));
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("--check-only", "--dump-ast", "-c", "exit 88"), 0, ".*"));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("--check-only", "--dump-code", "-c", "exit 88"), 0));


    // equivalent to '--check-only' option
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-n", "-c", "exit 88"), 0));

    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("-n", "--dump-untyped-ast", "-c", "exit 88"), 0, ".*"));
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("-n", "--dump-ast", "-c", "exit 88"), 0, ".*"));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-n", "--dump-code", "-c", "exit 88"), 0));
}

TEST_F(CmdlineTest, compile_only) {
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("--compile-only", "-c", "exit 88"), 0));

    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("--compile-only", "--dump-untyped-ast", "-c", "exit 88"), 0, ".*"));
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("--compile-only", "--dump-ast", "-c", "exit 88"), 0, ".*"));
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("--compile-only", "--dump-code", "-c", "exit 88"), 0, ".*"));
}

TEST_F(CmdlineTest, exec) {
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "echo", "hello"), 0, "hello\n"));

    // not found builtin command
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "fhurehfurei"), 1, "", "ydsh: fhurehfurei: not builtin command\n"));

    // command
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "command", "hogehoge"), 1, "", "ydsh: hogehoge: command not found\n"));

    // eval
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "eval", "hogehoge"), 1, "", "ydsh: hogehoge: command not found\n"));

    // exit
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "exit", "34"), 34));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "exit", "999"), 231));

    // exec
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "exec"), 0));  // do nothing
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "exec", "echo", "hello"), 0, "hello\n"));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "exec", "-a", "hoge", "echo", "hello"), 0, "hello\n"));

    // default env
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "exec", "-c", BIN_PATH, "-c", "assert(\"$(printenv SHLVL)\" == \"1\")"), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect(
            ds("-e", "exec", "-c", BIN_PATH, "-c", "assert(\"$(printenv PATH)\" == \"/bin:/usr/bin:/usr/local/bin\")"), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect(
            ds("-e", "exec", "-c", BIN_PATH, "-c", "assert(\"$(printenv LOGNAME)\" == \"$(ps_intrp \\\\u)\")"), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect(
            ds("-e", "exec", "-c", BIN_PATH, "-c", "assert(\"$(printenv USER)\" == \"$(ps_intrp \\\\u)\")"), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect(
            ds("-e", "exec", "-c", BIN_PATH, "-c", "assert(\"$(printenv HOME)\" == \"$(echo ~)\")"), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect(
            ds("-e", "exec", "-c", BIN_PATH, "-c", "assert(\"$(printenv _)\" == \"$(command -v printenv)\")"), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect(
            ds("-e", "exec", "-c", BIN_PATH, "-c", "assert(\"$(printenv PWD)\" == \"$(printenv OLDPWD)\")"), 0));

    if(platform::detect() == platform::PlatformType::CYGWIN) {
        ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "exec", "-c", BIN_PATH, "-c", "assert(check_env WINDIR)"), 0));
        ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "exec", "-c", BIN_PATH, "-c", "assert(check_env SYSTEMROOT)"), 0));

        ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "exec", "-c", BIN_PATH, "-c", "assert $(printenv).size() == 10"), 0));
    } else {
        ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "exec", "-c", BIN_PATH, "-c", "assert $(printenv).size() == 8"), 0));
    }

    // invalid option
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "exec", "-u"), 2, "", "ydsh: exec: -u: invalid option\n"
                                                                        "exec: exec [-c] [-a name] file [args ...]\n"));
}

TEST_F(CmdlineTest, marker1) {
    // line marker of syntax error
    const char *msg = R"((string):1: [syntax error] mismatched token: <EOS>, expected: =
var a
      ^
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "var a   \n    \\\n   \t  \t  \n   "), 1, "", msg));

    auto result = ds("-c", "\n);").execAndGetResult(false);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, result.status.value));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(");\n^\n", strchr(result.err.c_str(), '\n') + 1));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", result.out));


    // line marker of semantic error
    msg = R"((string):1: [semantic error] require `Int32' type, but is `String' type
[34, "hey"]
     ^~~~~
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "[34, \"hey\"]"), 1, "", msg));

    // line marker containing newline
    const char *s = R"(
var a = 34
$a = 34 +
     'de'
)";
    msg = R"((string):3: [semantic error] require `Int32' type, but is `String' type
$a = 34 +
     ^~~~
     'de'
~~~~~~~~~
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", s), 1, "", msg));

    // line marker (reach null character)
    msg = "(stdin):1: [syntax error] invalid token, expected: <NewLine>\nhello\n     \n";
    ASSERT_NO_FATAL_FAILURE(this->expect("hello\0world" | ds(), 1, "", msg));
}

TEST_F(CmdlineTest, marker2) {
    const char *s = "$e";
    const char *msg = R"((string):1: [semantic error] undefined symbol: `e'
$e
^~
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", s), 1, "", msg));

    s = "for $a in 34 {}";
    msg = "(string):1: [semantic error] undefined method: `%iter'\n"
          "for $a in 34 {}\n"
          "          ^~\n";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", s), 1, "", msg));
}

TEST_F(CmdlineTest, version) {
    std::string msg = "^ydsh, version ";
    msg += X_INFO_VERSION;
    msg += ", build by .+\n";
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("--version"), 0, msg.c_str()));
}


TEST_F(CmdlineTest, prompt) {
#ifdef USE_FIXED_TIME
    bool useFixedTime = true;
#else
    bool useFixedTime = false;
#endif

    std::string cmd = BIN_PATH;
    cmd += " --feature | grep USE_FIXED_TIME";
    if(useFixedTime) {
        ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", cmd.c_str()), 0, "USE_FIXED_TIME\n"));

        const char *name = "TIME_SOURCE";
        const char *value = "2016-1-13T15:15:12Z";

        ASSERT_NO_FATAL_FAILURE(this->expect(std::move(ds("-c", "ps_intrp '\\d'").addEnv(name, value)), 0, "Wed 01 13\n"));
        ASSERT_NO_FATAL_FAILURE(this->expect(std::move(ds("-c", "ps_intrp '\\t'").addEnv(name, value)), 0, "15:15:12\n"));
        ASSERT_NO_FATAL_FAILURE(this->expect(std::move(ds("-c", "ps_intrp '\\T'").addEnv(name, value)), 0, "03:15:12\n"));
        ASSERT_NO_FATAL_FAILURE(this->expect(std::move(ds("-c", "ps_intrp '\\@'").addEnv(name, value)), 0, "03:15 PM\n"));
    } else {
        ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", cmd.c_str()), 1));
    }
}

TEST_F(CmdlineTest, logger) {
#ifdef USE_LOGGING
    bool useLogging = true;
#else
    bool useLogging = false;
#endif

    std::string cmd = BIN_PATH;
    cmd += " --feature | grep USE_LOGGING";
    if(useLogging) {
        ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", cmd.c_str()), 0, "USE_LOGGING\n"));

        auto builder = ds("-c", "sh -c true").addEnv("YDSH_DUMP_EXEC", "on");
        const char *re = ".+\\(xexecve\\).+";
        ASSERT_NO_FATAL_FAILURE(this->expectRegex(std::move(builder), 0, "", re));

        // specify appender
        builder = ds("-c", "var a = 0; exit $a")
                .addEnv("YDSH_TRACE_TOKEN", "on")
                .addEnv("YDSH_APPENDER", "/dev/stdout");
        ASSERT_NO_FATAL_FAILURE(this->expectRegex(std::move(builder), 0, ".+"));

        // specify appender (not found)
        builder = ds("-c", "var a = 0; exit $a")
                .addEnv("YDSH_TRACE_TOKEN", "on")
                .addEnv("YDSH_APPENDER", "/dev/null/hogehu");
        ASSERT_NO_FATAL_FAILURE(this->expectRegex(std::move(builder), 0, "", ".+"));
    } else {
        ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", cmd.c_str()), 1));
    }
}

#define CL(...) ProcBuilder {BIN_PATH, "-c", format(__VA_ARGS__).c_str()}

TEST_F(CmdlineTest, pid) {
    ASSERT_NO_FATAL_FAILURE(this->expect(CL("%s --pid $PID --ppid $PPID | grep .", PID_CHECK_PATH), 0, "OK\n"));
}

TEST_F(CmdlineTest, toplevel) {
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("--print-toplevel", "-c", "23 as String"), 0, "(String) 23\n"));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("--print-toplevel", "-c", "$true"), 0, "(Boolean) true\n"));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("--print-toplevel", "-c", "true"), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("--print-toplevel", "-c", "true | true"), 0));

    // runtime error
    const char *msg = R"([runtime error]
StackOverflowError: interpreter recursion depth reaches limit
    from (string):1 '<toplevel>()'
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(
            ds("--print-toplevel", "-c", "var a = (9 as Any,); $a._0 = $a; $a"), 1, "", msg));

    msg = R"([runtime error]
cannot obtain string representation
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(
            ds("--print-toplevel", "-c", "var a = (9 as Any,); $a._0 = $a; throw $a"), 1, "", msg));

    // option type
    ASSERT_NO_FATAL_FAILURE(this->expect(
            ds("--print-toplevel", "-c", "var a = $true as Option<Boolean>; $a"), 0, "(Option<Boolean>) true\n"));
    ASSERT_NO_FATAL_FAILURE(this->expect(
            ds("--print-toplevel", "-c", "new Option<Boolean>()"), 0, "(Option<Boolean>) (invalid)\n"));
    ASSERT_NO_FATAL_FAILURE(this->expect(
            ds("--print-toplevel", "-c", "var a = $true as String as Option<String>; $a"), 0, "(Option<String>) true\n"));
    ASSERT_NO_FATAL_FAILURE(this->expect(
            ds("--print-toplevel", "-c", "new Option<String>()"), 0, "(Option<String>) (invalid)\n"));
}

TEST_F(CmdlineTest, toplevel_escape) {
    auto builder = ds("--print-toplevel", "-c", "$'hello\\x00world'");
    auto r = builder.execAndGetResult(false);

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, r.status.value));

    const char msg[] = "(String) hello\0world\n";
    std::string out(msg, arraySize(msg) - 1);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(out, r.out));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("", r.err.c_str()));
}

TEST_F(CmdlineTest, syntax) {
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "echo  \n$true"), 0, "\n"));
}

TEST_F(CmdlineTest, pipeline) {
    ASSERT_NO_FATAL_FAILURE(this->expect("assert($0 == 'ydsh')" | ds(), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect("\\" | ds(), 0));

    // with argument
    ASSERT_NO_FATAL_FAILURE(
            this->expect("assert($0 == 'ydsh' && $1 == 'hoge' && $2 == '123')" | ds("-s", "hoge", "123"), 0));

    // force interactive
    ASSERT_NO_FATAL_FAILURE(this->expect("$true\n" | ds("-i", "--quiet", "--norc"), 0, "(Boolean) true\n"));
}

#define DS(S) ds("-c", S)

TEST_F(CmdlineTest, read) {
    /**
     * read command status
     * if read success, return 0
     * if read failed (error or end of file), return 1
     */
    const char *src = R"(
        read; assert($? == 0);
        read; assert($? == 1);
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("hello\n" | DS(src), 1));

    /**
     * no splitting
     * not terminate
     */
    src = R"(
        read -u 0;
        assert $REPLY == "hello"
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("hello" | DS(src), 1));

    /**
     * no splitting
     * terminate newline
     */
    src = R"(
        read -u /dev/fd/0; assert($REPLY == "hello");
        read; assert($REPLY == "world")
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("   hello\n   world   \t   \n" | DS(src), 0));

    /**
     * no splitting
     * if not specified separator, use IFS
     */
    src = R"(
        read; assert($REPLY == "hello world");
        assert($reply.empty())
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(" \t  hello world \t \t  \n" | DS(src), 0));

    /**
     * no splitting
     * specify separator
     */
    src = R"(
        read -f 1; assert($REPLY == "1hello1world ")
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("1hello1world \n" | DS(src), 0));

    /**
     * no splitting
     * specify multiple separator
     * if separator contains spaces, ignore first and last spaces
     */
    src = R"(
        read -f " 1"; assert $REPLY == "1hello1world1"
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("  1hello1world1 \n" | DS(src), 0));

    /**
     * splitting
     * use IFS
     * remove first and last space
     */
    src = R"(
        read a b; assert($reply.size() == 2)
        assert $reply["a"] == "hello"
        assert $reply["b"] == "world"
        assert($REPLY.empty())
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("   \t hello   world    \n" | DS(src), 0));

    /**
     * splitting
     * use IFS
     * remove first and last spaces
     * split variables are less than specified them, set empty string.
     */
    src = R"(
        read a b c; assert($reply.size() == 3)
        assert $reply["a"] == "hello"
        assert $reply["b"] == "world"
        assert $reply["c"].empty()
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("   \t hello   world    \n" | DS(src), 0));

    /*
     * splitting
     * use IFS
     * remove fist and last spaces
     */
    src = R"(
        read a b; assert($reply.size() == 2)
        assert $reply["a"] == "hello"
        assert $reply["b"] == "world  !!"
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("   \t hello   world  !!  \n" | DS(src), 0));

    /**
     * splitting
     * use IFS
     * ignore the following string of newline
     */
    src = R"(
        read a b; assert($reply.size() == 2)
        assert $reply["a"] == "hello"
        assert $reply["b"].empty()
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("hello  \n world\n" | DS(src), 0));

    /**
     * splitting
     * use IFS
     * ignore newline
     */
    src = R"(
        read a b; assert($reply.size() == 2)
        assert $reply["a"] == "hello"
        assert $reply["b"] == "world"
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("hello  \\\n world\n" | DS(src), 0));

    /**
     * splitting
     * specify separator
     */
    src = R"(
        read -f 1 a b; assert($reply.size() == 2)
        assert $reply["a"] == "hello"
        assert $reply["b"] == "world"
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("hello1world\n" | DS(src), 0));

    /**
     * splitting
     * specify separator
     */
    src = R"(
        read -f 1 a b; assert($reply.size() == 2)
        assert $reply["a"] == "hello1world"
        assert $reply["b"].empty()
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("hello\\1world\n" | DS(src), 0));

    /**
     * splitting
     * specify multiple separator
     */
    src = R"(
        read -f 12 a b c; assert($reply.size() == 3)
        assert $reply["a"] == "hello"
        assert $reply["b"] == "world"
        assert $reply["c"] == "!!"
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("hello1world2!!\n" | DS(src), 0));

    /**
     * splitting
     * specify multiple separator
     */
    src = R"(
        read -f 1 a b; assert($reply.size() == 2)
        assert $reply["a"] == "hello"
        assert $reply["b"] == "world2!!"
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("hello1world2!!\n" | DS(src), 0));

    /**
     * splitting
     * specify multiple esparator
     * if separator contains spaces, remove spaces
     */
    src = R"(
        read -f " 21" a b c; assert($reply.size() == 3)
        assert $reply["a"] == "hello"
        assert $reply["b"] == "world"
        assert $reply["c"] == "2!!"
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("   hello  1  world22!!  \n" | DS(src), 0));

    /**
     * splitting
     * specify multiple separator
     * if separator contains spaces, remove spaces
     */
    src = R"(
        read -f " 21" a b c; assert($reply.size() == 3)
        assert $reply["a"] == "hello"
        assert $reply["b"] == ""
        assert $reply["c"] == "world22!!"
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("   hello  21  world22!!  \n" | DS(src), 0));

    /**
     * splitting
     * specify multiple separator
     * if separator contains spaces, remove spaces
     */
    src = R"(
        read -f " 21" a b c; assert($reply.size() == 3)
        assert $reply["a"] == "hello"
        assert $reply["b"] == "2"
        assert $reply["c"] == "world22!!"
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("   hello  \\21  world22!!  \n" | DS(src), 0));

    /**
     * splitting
     * specify separator (null character)
     */
    src = R"(
        read -f $'a\x00' a b c; assert($reply.size() == 3)
        assert $reply['a'] == '   hello'
        assert $reply['b'] == 'world'
        assert $reply['c'] == '22!!'
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("   hello\0worlda22!!\n" | DS(src), 0));

    /**
     * raw mode
     */
    src = R"(
        read -r a b; assert($reply.size() == 2)
        assert $reply["a"] == "hello\\"
        assert $reply["b"] == "world"
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("   hello\\ world  \n" | DS(src), 0));

    /**
     * raw mode
     */
    src = R"(
        read -r; assert($REPLY == "hello\\")
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("   hello\\\nworld  \n" | DS(src), 0));

    /**
     * raw mode
     */
    src = R"(
        read -r -f " 1" a b; assert($reply.size() == 2)
        assert $reply["a"] == "hello\\"
        assert $reply["b"] == "world"
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("   hello\\1world  \n" | DS(src), 0));

    /**
     * timeout
     */
    ASSERT_NO_FATAL_FAILURE(this->expect(CL("read -t 1"), 1));
}

TEST_F(CmdlineTest, termHook) {
    const char *src = R"(
        function f($k : Int, $a : Any) {
            echo receive error: $k: $a
        }
        $TERM_HOOK = $f

        exit 45
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(DS(src), 45, "receive error: 1: 45\n"));

    src = R"(
        function f($k : Int, $a : Any) {
            echo receive error: $k: $a
        }
        $TERM_HOOK = $f

        45 / 0
)";
    const char *e = R"([runtime error]
ArithmeticError: zero division
    from (string):7 '<toplevel>()'
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(DS(src), 1, "receive error: 2: ArithmeticError: zero division\n", e));

    src = R"(
        function f($k : Int, $a : Any) {
            echo receive error: $k: $a
        }
        $TERM_HOOK = $f

        assert false
)";
    e = R"(Assertion Error: `false'
    from (string):7 '<toplevel>()'
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(DS(src), 1, "receive error: 4: 1\n", e));
}

TEST_F(CmdlineTest, signal1) {
    std::string str = strsignal(SIGKILL);
    str += "\n";
    ASSERT_NO_FATAL_FAILURE(this->expect(DS("sh -c 'kill -s kill $$'"), 128 + SIGKILL, "", str.c_str()));
}

TEST_F(CmdlineTest, signal2) {
    ASSERT_NO_FATAL_FAILURE(this->expect(DS("cat /dev/random | grep 2> /dev/null"), 2));
}

struct CmdlineTest2 : public CmdlineTest, public TempFileFactory {
    CmdlineTest2() = default;

    void SetUp() override { this->createTemp(); }

    void TearDown() override { this->deleteTemp(); }
};

TEST_F(CmdlineTest2, script) {
    auto fileName = this->createTempFile("target.ds",
            format("assert($0 == \"%s/target.ds\"); assert($@.size() == 1); assert($@[0] == 'A')", this->getTempDirName()));

    ASSERT_NO_FATAL_FAILURE(this->expect(ds(fileName.c_str(), "A"), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("--", fileName.c_str(), "A"), 0));
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("hogehogehuga"), 1, "", "^ydsh: cannot open file: hogehogehuga, by .+$"));

    // script dir
    fileName = this->createTempFile("target2.ds", "assert $SCRIPT_DIR == \"$(cd $(dirname $0) && pwd -P)\"");

    ASSERT_NO_FATAL_FAILURE(this->expect(ds(fileName.c_str()), 0));
}

TEST_F(CmdlineTest2, complete) {
    std::string target = this->getTempDirName();
    target += "/work/actual";

    // create working dir
    auto builder = CL("mkdir -p %s; ln -s %s ./link && cd ./link && touch hogehuga && chmod +x hogehuga",
                      target.c_str(), target.c_str())
            .setWorkingDir(this->getTempDirName());
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 0));

    // follow symbolic link
    ASSERT_NO_FATAL_FAILURE(
            this->expect(CL("cd %s; assert \"$(complete ./link/)\" == 'hogehuga'", this->getTempDirName()), 0));

    builder = CL("cd %s; var ret = $(complete ./link/../);\n"
                 "assert $ret.size() == 2\n"
                 "assert $ret[0] == 'link/'\n"
                 "assert $ret[1] == 'work/'", this->getTempDirName());

    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 0));
}

TEST_F(CmdlineTest2, cwd) {
    std::string target = this->getTempDirName();
    target += "/work/actual";

    // create working dir
    auto builder = CL("mkdir -p %s; ln -s %s ./link", target.c_str(), target.c_str())
            .setWorkingDir(this->getTempDirName());
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 0));

    // follow symbolic link
    ASSERT_NO_FATAL_FAILURE(this->expect(CL("cd %s/link; import-env PWD; assert $PWD == '%s/link'",
                                            this->getTempDirName(), this->getTempDirName()), 0));

    ASSERT_NO_FATAL_FAILURE(this->expect(CL("cd %s/link; assert \"$(pwd)\" == '%s/link'",
                                            this->getTempDirName(), this->getTempDirName()), 0));

    ASSERT_NO_FATAL_FAILURE(this->expect(CL("cd -L %s/link; assert \"$(pwd -L)\" == '%s/link'",
                                            this->getTempDirName(), this->getTempDirName()), 0));

    ASSERT_NO_FATAL_FAILURE(this->expect(CL("assert(cd %s/link); assert \"$(pwd -P)\" == '%s'",
                                            this->getTempDirName(), target.c_str()), 0));

    ASSERT_NO_FATAL_FAILURE(this->expect(CL("assert(cd %s/link); assert cd ../; assert \"$(pwd -P)\" == '%s'",
                                            this->getTempDirName(), this->getTempDirName()), 0));

    ASSERT_NO_FATAL_FAILURE(this->expect(CL("assert(cd %s/link); assert cd ../; assert \"$(pwd -L)\" == '%s'",
                                            this->getTempDirName(), this->getTempDirName()), 0));

    ASSERT_NO_FATAL_FAILURE(this->expect(CL("assert(cd %s/link); assert cd ../; import-env OLDPWD; assert $OLDPWD == '%s/link'",
                                            this->getTempDirName(), this->getTempDirName()), 0));

    // without symbolic link
    ASSERT_NO_FATAL_FAILURE(this->expect(CL("assert(cd -P %s/link); import-env PWD; assert $PWD == '%s'",
                                            this->getTempDirName(), target.c_str()), 0));

    ASSERT_NO_FATAL_FAILURE(this->expect(CL("assert(cd -P %s/link); assert \"$(pwd)\" == '%s'",
                                            this->getTempDirName(), target.c_str()), 0));

    ASSERT_NO_FATAL_FAILURE(this->expect(CL("assert(cd -P %s/link); assert \"$(pwd -P)\" == '%s'",
                                            this->getTempDirName(), target.c_str()), 0));

    ASSERT_NO_FATAL_FAILURE(this->expect(CL("assert(cd -P %s/link); assert \"$(pwd -L)\" == '%s'",
                                            this->getTempDirName(), target.c_str()), 0));

    ASSERT_NO_FATAL_FAILURE(this->expect(CL("assert(cd -P %s/link); assert cd ../; assert \"$(pwd -L)\" == '%s/work'",
                                            this->getTempDirName(), this->getTempDirName()), 0));

    ASSERT_NO_FATAL_FAILURE(this->expect(CL("assert(cd -P %s/link); assert cd ../; import-env OLDPWD; assert $OLDPWD == '%s'",
                                            this->getTempDirName(), target.c_str()), 0));
}

static std::string makeLineMarker(const std::string &line) {
    std::string str = "^";
    for(unsigned int i = 1; i < line.size(); i++) {
        str += "~";
    }
    return str;
}

TEST_F(CmdlineTest2, import1) {
    if(platform::detect() == platform::PlatformType::CYGWIN) {
        return;
    }

    auto fileName = this->createTempFile("target.ds", "throw new Error('invalid!!')");
    chmod(fileName.c_str(), ~S_IRUSR);

    std::string str = format("(string):1: [semantic error] cannot read module: `%s', by `Permission denied'\n"
                             "source %s as mod\n"
                             "       %s\n", fileName.c_str(), fileName.c_str(), makeLineMarker(fileName).c_str());

    ASSERT_NO_FATAL_FAILURE(this->expect(CL("source %s as mod", fileName.c_str()), 1, "", str.c_str()));
}

TEST_F(CmdlineTest2, import2) {
    auto modName = this->createTempFile("mod.ds", format("source %s/target.ds as mod2", this->getTempDirName()));
    auto fileName = this->createTempFile("target.ds", format("source %s/mod.ds as mod1", this->getTempDirName()));

    std::string str = format("%s:1: [semantic error] circular module import: `%s'\n"
                             "source %s as mod2\n"
                             "       %s\n",
                             modName.c_str(), fileName.c_str(), fileName.c_str(), makeLineMarker(fileName).c_str());
    str += format("%s:1: [note] at module import\n"
                  "source %s as mod1\n"
                  "       %s\n",
                  fileName.c_str(),modName.c_str(), makeLineMarker(modName).c_str());

    ASSERT_NO_FATAL_FAILURE(this->expect(ProcBuilder{ BIN_PATH, fileName.c_str() }, 1, "", str.c_str()));
}

TEST_F(CmdlineTest2, import3) {
    std::string str = format("(string):1: [semantic error] cannot read module: `.', by `Is a directory'\n"
                             "source .\n"
                             "       %s\n", makeLineMarker(".").c_str());
    ASSERT_NO_FATAL_FAILURE(this->expect(CL("source ."), 1, "", str.c_str()));
}

TEST_F(CmdlineTest2, import4) {
    std::string str = format("(string):1: [semantic error] module not found: `hogehuga'\n"
                             "source hogehuga\n"
                             "       %s\n", makeLineMarker("hogehuga").c_str());
    ASSERT_NO_FATAL_FAILURE(this->expect(CL("source hogehuga"), 1, "", str.c_str()));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}