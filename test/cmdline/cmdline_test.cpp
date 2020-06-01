#include "gtest/gtest.h"

#include <config.h>
#include <constant.h>
#include <misc/files.h>
#include <misc/util.hpp>
#include <misc/fatal.h>
#include "../test_common.h"
#include "../../tools/platform/platform.h"


#ifndef BIN_PATH
#define "require BIN_PATH"
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

    auto execAndGetResult() {
        auto handle = this->builder
                .setIn(IOConfig::PIPE)
                .setOut(IOConfig::PIPE)
                .setErr(IOConfig::PIPE)();
        if(write(handle.in(), this->value.c_str(), this->value.size()) < 0) {
            fatal_perror("");
        }
        close(handle.in());
        return handle.waitAndGetResult(false);
    }
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
    using ExpectOutput::expectRegex;

    void expect(InputWrapper &&wrapper, int status, const std::string &out = "", const std::string &err = "") {
        auto result = wrapper.execAndGetResult();
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

    ASSERT_NO_FATAL_FAILURE( this->expectRegex(
            ds("--dump-ast", "-c", "23 / /"), 1, "", "^.+\\[semantic error\\].*$"));

    ASSERT_NO_FATAL_FAILURE( this->expectRegex(
            ds("--dump-ast", "-c", "23 / "), 1, "", "^.+\\[syntax error\\].*$"));
}

TEST_F(CmdlineTest, uast) {
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(
            ds("--dump-untyped-ast", "-c", "12"), 0, "^### dump untyped AST ###.*$"));

    ASSERT_NO_FATAL_FAILURE( this->expectRegex(
            ds("--dump-untyped-ast", "-c", "23 / "), 1, "", "^.+\\[syntax error\\].*$"));
}

TEST_F(CmdlineTest, cmd1) {
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "assert($0 == 'ydsh')"), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect(
            ds("-c", "assert($0 == \"A\"); assert($@.size() == 1); assert($@[0] == \"G\")", "A", "G"), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "assert $SCRIPT_DIR == \"$(pwd -L)\""), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "\\"), 0));    // do nothing
}

template <unsigned int N>
static std::string toString(const char (&value)[N]) {
    static_assert(N > 0, "");
    return std::string(value, N - 1);
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

    msg = R"([runtime error]
SystemError: execution error: lajfeoifreo: command not found
    from (string):1 '<toplevel>()'
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "lajfeoifreo"), 1, "", msg));

    const char out[] = "[runtime error]\nhe\0llo\n";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "var a = $'he\\000llo'; throw $a"), 1, "", toString(out)));

    // normal
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
            ds("-c", "shctl set traceonexit; exit 23"), 23, "", "^Shell Exit: terminated by exit 23\n.*$"));
    ASSERT_NO_FATAL_FAILURE(this->expect(
            ds("--trace-exit", "-c", "shctl unset traceonexit; exit 23"), 23));
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(
            ds("--trace-exit", "-c", "exit 2300"), 252, "", "^Shell Exit: terminated by exit 252\n.*$"));
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(
            ds("--trace-exit", "-c", "$? = 123; exit hoge"), 123, "", "^Shell Exit: terminated by exit 123\n.*$"));
    ASSERT_NO_FATAL_FAILURE(this->expect(
            ds("--trace-exit", "-e", "exit", "34"), 34, "", "Shell Exit: terminated by exit 34\n"));
}

static std::string getCwd() {
    char *ptr = realpath(".", nullptr);
    std::string ret = ptr;
    free(ptr);
    return ret;
}

TEST_F(CmdlineTest, bytecode) {
    std::string msg = format(R"(### dump compiled code ###
Source File: (string)
DSCode: top level
  code size: 12
  max stack depth: 1
  number of local variable: 0
  number of global variable: 53
Code:
   0: PUSH_INT  34
   2: STORE_GLOBAL  52
   5: PUSH_INT  34
   7: CALL_NATIVE2  1  %s
  10: POP
  11: RETURN
Constant Pool:
  0: String (string)
  1: String %s
Line Number Table:
  lineNum: 1, address:  7
Exception Table:

)", "%str", getCwd().c_str());
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("--dump-code", "-c", "var a = 34; 34 as String"), 0, msg));

    msg = format(R"(### dump compiled code ###
Source File: (string)
DSCode: top level
  code size: 26
  max stack depth: 3
  number of local variable: 0
  number of global variable: 53
Code:
   0: LOAD_CONST  2
   2: STORE_GLOBAL  52
   5: LOAD_GLOBAL  52
   8: PUSH_INT  1
  10: CALL_FUNC  1
  12: ENTER_FINALLY  8
  15: GOTO  24
  20: PUSH_INT  3
  22: POP
  23: EXIT_FINALLY
  24: POP
  25: RETURN
Constant Pool:
  0: String (string)
  1: String %s
  2: (Any) -> Boolean function(f)
Line Number Table:
  lineNum: 1, address: 10
Exception Table:
  begin: 5, end: 20, type: Any, dest: 20, offset: 0, size: 0

DSCode: function f
  code size: 7
  max stack depth: 1
  number of local variable: 1
Code:
  0: LOAD_LOCAL  0
  2: INSTANCE_OF  [Int]
  6: RETURN_V
Constant Pool:
  0: String (string)
  1: String %s
Line Number Table:
Exception Table:

)", getCwd().c_str(), getCwd().c_str());
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
}

TEST_F(CmdlineTest, compile_only) {
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("--compile-only", "-c", "exit 88"), 0));

    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("--compile-only", "--dump-untyped-ast", "-c", "exit 88"), 0, ".*"));
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("--compile-only", "--dump-ast", "-c", "exit 88"), 0, ".*"));
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("--compile-only", "--dump-code", "-c", "exit 88"), 0, ".*"));

    // equivalent to '--compile-only' option
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-n", "-c", "exit 88"), 0));

    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("-n", "--dump-untyped-ast", "-c", "exit 88"), 0, ".*"));
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("-n", "--dump-ast", "-c", "exit 88"), 0, ".*"));
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("-n", "--dump-code", "-c", "exit 88"), 0, ".*"));
}

TEST_F(CmdlineTest, exec) {
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "echo", "hello"), 0, "hello\n"));

    // not found builtin command
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "fhurehfurei"), 1, "", "[runtime error]\nSystemError: execution error: fhurehfurei: command not found\n"));

    // command
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "command", "hogehoge"), 1, "", "[runtime error]\nSystemError: execution error: hogehoge: command not found\n"));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "command", "exit", "999"), 231));

    // eval
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "eval", "hogehoge"), 1, "", "[runtime error]\nSystemError: execution error: hogehoge: command not found\n"));
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "eval", "exit", "45"), 45));

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
            ds("-e", "exec", "-c", BIN_PATH, "-c", "assert(\"$(printenv LOGNAME)\" == \"$(basename ~)\")"), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect(
            ds("-e", "exec", "-c", BIN_PATH, "-c", "assert(\"$(printenv USER)\" == \"$(basename ~)\")"), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect(
            ds("-e", "exec", "-c", BIN_PATH, "-c", "assert(\"$(printenv HOME)\" == \"$(echo ~)\")"), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect(
            ds("-e", "exec", "-c", BIN_PATH, "-c", "assert(\"$(printenv _)\" == \"$(command -v printenv)\")"), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect(
            ds("-e", "exec", "-c", BIN_PATH, "-c", "assert(\"$(printenv PWD)\" == \"$(printenv OLDPWD)\")"), 0));

    if(platform::platform() == platform::PlatformType::CYGWIN) {
        ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "exec", "-c", BIN_PATH, "-c", "assert(checkenv WINDIR)"), 0));
        ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "exec", "-c", BIN_PATH, "-c", "assert(checkenv SYSTEMROOT)"), 0));

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
    ASSERT_EQ(1, result.status.value);
    ASSERT_STREQ(");\n^\n", strchr(result.err.c_str(), '\n') + 1);
    ASSERT_EQ("", result.out);


    // line marker of semantic error
    msg = R"((string):1: [semantic error] require `Int' type, but is `String' type
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
    msg = R"((string):3: [semantic error] require `Int' type, but is `String' type
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

TEST_F(CmdlineTest, marker3) {
    const char *msg = R"((string):1: [semantic error] undefined symbol: `a'
"${a.b}"
   ^
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", R"EOF("${a.b}")EOF"), 1, "", msg));

    msg = R"((string):1: [semantic error] undefined field: `t'
echo ${true.t}
            ^
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", R"EOF(echo ${true.t})EOF"), 1, "", msg));
}

TEST_F(CmdlineTest, version) {
    std::string msg = "^ydsh, version ";
    msg += X_INFO_VERSION;
    msg += ", build by .+\n";
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("--version"), 0, msg));
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

TEST_F(CmdlineTest, syntax) {
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "echo  \n$true"), 0, "\n"));
}

TEST_F(CmdlineTest, pipeline) {
    ASSERT_NO_FATAL_FAILURE(this->expect("assert($0 == 'ydsh')" | ds(), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect("\\" | ds(), 0));

    // with argument
    ASSERT_NO_FATAL_FAILURE(
            this->expect("assert($0 == 'ydsh' && $1 == 'hoge' && $2 == '123')" | ds("-s", "hoge", "123"), 0));
}

static auto interactive() {
    return ds("-i", "--quiet", "--norc");
}

TEST_F(CmdlineTest, forceInteractive1) {
    ASSERT_NO_FATAL_FAILURE(this->expect("23 as String" | interactive(), 0, ": String = 23\n"));
    ASSERT_NO_FATAL_FAILURE(this->expect("$true" | interactive(), 0, ": Boolean = true\n"));
    ASSERT_NO_FATAL_FAILURE(this->expect("true" | interactive(), 0));
    ASSERT_NO_FATAL_FAILURE(this->expect("true | false" | interactive(), 1));

    // runtime error
    const char *msg = R"([runtime error]
UnwrappingError: invalid value
    from (stdin):1 '<toplevel>()'
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("var a = (9, new Int!()); $a" | interactive(), 1, "", msg));

    msg = R"([runtime error]
cannot obtain string representation
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("var a = (9, new Int!()); throw $a" | interactive(), 1, "", msg));

    // option type
    ASSERT_NO_FATAL_FAILURE(
            this->expect("var a = $true as Option<Boolean>; $a" | interactive(), 0, ": Boolean! = true\n"));
    ASSERT_NO_FATAL_FAILURE(
            this->expect("new Option<Boolean>()" | interactive(), 0, ": Boolean! = (invalid)\n"));
    ASSERT_NO_FATAL_FAILURE(
            this->expect("var a = $true as String as Option<String>; $a" | interactive(), 0, ": String! = true\n"));
    ASSERT_NO_FATAL_FAILURE(
            this->expect("new Option<String>()" | interactive(), 0, ": String! = (invalid)\n"));
}

TEST_F(CmdlineTest, forceInteractive2) {
    // escape
    auto wrapper = "$'hello\\x00world'" | interactive();
    auto r = wrapper.execAndGetResult();

    ASSERT_EQ(0, r.status.value);

    const char msg[] = ": String = hello\0world\n";
    std::string out(msg, arraySize(msg) - 1);
    ASSERT_EQ(out, r.out);
    ASSERT_STREQ("", r.err.c_str());
}

TEST_F(CmdlineTest, pipelineCode) {
    auto ret1 = ds("--compile-only", "--dump-code", "-c", "1|2|3").execAndGetResult();
    ASSERT_FALSE(ret1.out.empty());

    auto ret2 = ds("--compile-only", "--dump-code", "-c", "(1|2)|3").execAndGetResult();
    ASSERT_FALSE(ret2.out.empty());

    auto ret3 = ds("--compile-only", "--dump-code", "-c", "1|(2|3)").execAndGetResult();
    ASSERT_FALSE(ret3.out.empty());

    ASSERT_EQ(ret1.out, ret2.out);
    ASSERT_EQ(ret1.out, ret3.out);
}

#define DS(S) ds("-c", S)

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

TEST_F(CmdlineTest, signal) {
    std::string str = strsignal(SIGKILL);
    str += "\n";
    ASSERT_NO_FATAL_FAILURE(this->expect(DS("sh -c 'kill -s kill $$'"), 128 + SIGKILL, "", str));
    ASSERT_NO_FATAL_FAILURE(this->expect(DS("echo ${%'kill'.message()}"), 0, str));
}

struct CmdlineTest2 : public CmdlineTest, public TempFileFactory {
    CmdlineTest2() = default;
};

TEST_F(CmdlineTest2, exec) {
    auto fileName = this->createTempFile("run.sh", "echo hey: $0: $1 $2");
    errno = 0;
    auto mode = getStMode(fileName.c_str());
    mode |= S_IXUSR | S_IXGRP | S_IXOTH;
    chmod(fileName.c_str(), mode);
    ASSERT_STREQ(strerror(0), strerror(errno));

    auto out = format("hey: %s: 11111 8888\n", fileName.c_str());
    auto cmd = format("%s 11111 8888", fileName.c_str());
    ASSERT_NO_FATAL_FAILURE(this->expect(DS(cmd.c_str()), 0, out));
}

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

TEST_F(CmdlineTest2, import1) {
    if(platform::platform() == platform::PlatformType::CYGWIN) {
        return;
    }

    auto fileName = this->createTempFile("target.ds", "throw new Error('invalid!!')");
    chmod(fileName.c_str(), ~S_IRUSR);

    std::string str = format("(string):1: [semantic error] cannot read module: `%s', by `Permission denied'\n"
                             "source %s as mod\n"
                             "       %s\n", fileName.c_str(), fileName.c_str(), makeLineMarker(fileName).c_str());

    ASSERT_NO_FATAL_FAILURE(this->expect(CL("source %s as mod", fileName.c_str()), 1, "", str));
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

    ASSERT_NO_FATAL_FAILURE(this->expect(ProcBuilder{ BIN_PATH, fileName.c_str() }, 1, "", str));
}

TEST_F(CmdlineTest2, import3) {
    std::string str = format("(string):1: [semantic error] cannot read module: `.', by `Is a directory'\n"
                             "source .\n"
                             "       %s\n", makeLineMarker(".").c_str());
    ASSERT_NO_FATAL_FAILURE(this->expect(CL("source ."), 1, "", str));
}

TEST_F(CmdlineTest2, import4) {
    std::string str = format("(string):1: [semantic error] module not found: `hogehuga'\n"
                             "source hogehuga\n"
                             "       %s\n", makeLineMarker("hogehuga").c_str());
    ASSERT_NO_FATAL_FAILURE(this->expect(CL("source hogehuga"), 1, "", str));
}

TEST_F(CmdlineTest2, import5) {
    auto modName = this->createTempFile("mod.ds", "var a = 0;\n\n34/$a");
    auto str = format("[runtime error]\n"
                      "ArithmeticError: zero division\n"
                      "    from %s:3 '<toplevel>()'\n"
                      "    from (string):2 '<toplevel>()'\n", modName.c_str());
    ASSERT_NO_FATAL_FAILURE(this->expect(CL("34\nsource %s", modName.c_str()), 1, "", str));
}

TEST_F(CmdlineTest2, backtrace) {
    auto fileName = this->createTempFile("file.ds", "function f() { shctl backtrace; }");
    auto str = format("from %s:1 'function f()'\n"
                      "from (string):2 '<toplevel>()'\n", fileName.c_str());
    ASSERT_NO_FATAL_FAILURE(this->expect(CL("source %s; 34\n$f()", fileName.c_str()), 0, str));

    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "shctl", "backtrace"), 0));
}

TEST_F(CmdlineTest2, nocwd) {
    auto builder = CL("assert '.' == $SCRIPT_DIR")
            .setWorkingDir(this->getTempDirName())
            .setBeforeExec([&]{
                removeDirWithRecursively(this->getTempDirName());
            });
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 0));
}

struct CmdlineTest3 : public CmdlineTest2 {
    using Pair = std::pair<std::string, std::string>;

    using ExpectOutput::expect;

    struct Param {
        std::string workdir;
        std::function<void()> beforeExec;
        std::pair<std::string, std::string> before;
        std::pair<std::string, std::string> after;
    };

    void expect(Param &&p) {
        auto builder = CL("import-env PWD; import-env OLDPWD; echo -n $PWD $OLDPWD")
                .setWorkingDir(p.workdir.c_str())
                .setBeforeExec([&]{
                    if(p.beforeExec) {
                        p.beforeExec();
                    }

                    // set PWD
                    if(p.before.first.empty()) {
                        unsetenv(ENV_PWD);
                    } else {
                        setenv(ENV_PWD, p.before.first.c_str(), 1);
                    }

                    // set OLDPWD
                    if(p.before.second.empty()) {
                        unsetenv(ENV_OLDPWD);
                    } else {
                        setenv(ENV_OLDPWD, p.before.second.c_str(), 1);
                    }
                });

        std::string out = p.after.first;
        out += " ";
        out += p.after.second;
        this->expect(std::move(builder), 0, out);
    }
};

TEST_F(CmdlineTest3, pwd) {
    std::string target = this->getTempDirName();
    target += "/work/actual";
    std::string link = this->getTempDirName();
    link += "/link";

    // create working dir
    auto builder = CL("mkdir -p %s; ln -s %s ./link", target.c_str(), target.c_str())
            .setWorkingDir(this->getTempDirName());
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 0));

    // [PWD, OLDWD] is [valid, valid]
    // [PWD, OLDPWD] is [valid, valid] => [not update, not update]
    Param p = {
            .workdir = target,
            .beforeExec = {},
            .before = {target, this->getTempDirName()},
            .after = {target, this->getTempDirName()}
    };
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

    // [PWD, OLDPWD] is [valid with symlink, valid] => [not update, not update]
    p = {
            .workdir = link,
            .beforeExec = {},
            .before = {link, this->getTempDirName()},
            .after = {link, this->getTempDirName()}
    };
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

    // [PWD, OLDPWD] is [valid, valid with symlink] => [not update, not update]
    p = {
            .workdir = target,
            .beforeExec = {},
            .before = {target, link},
            .after = {target, link}
    };
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

    // [PWD, OLDPWD] is [valid, invalid]
    // [PWD, OLDPWD] is [valid, not set] => [not update, PWD]
    p = {
            .workdir = target,
            .beforeExec = {},
            .before = {target, ""},
            .after = {target, target}
    };
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

    // [PWD, OLDPWD] is [valid, not dir] => [not update, PWD]
    p = {
            .workdir = target,
            .beforeExec = {},
            .before = {target, BIN_PATH},
            .after = {target, target}
    };
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

    // [PWD, OLDPWD] is [valid, not full path] => [not update, PWD]
    p = {
            .workdir = target,
            .beforeExec = {},
            .before = {target, "."},
            .after = {target, target}
    };
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

    // [PWD, OLDPWD] is [valid with symlink, not set] => [not update, PWD]
    p = {
            .workdir = link,
            .beforeExec = {},
            .before = {link, ""},
            .after = {link, link}
    };
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

    // [PWD, OLDPWD] is [invalid, valid]
    // [PWD, OLDPWD] is [not set, valid] => [cwd, not update]
    p = {
            .workdir = target,
            .beforeExec = {},
            .before = {"", this->getTempDirName()},
            .after = {target, this->getTempDirName()}
    };
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

    // [PWD, OLDPWD] is [invalid, valid] => [cwd, not update]
    p = {
            .workdir = target,
            .beforeExec = {},
            .before = {"hgoehgoa", this->getTempDirName()},
            .after = {target, this->getTempDirName()}
    };
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

    // [PWD, OLDPWD] is [not full path, valid] => [cwd, not update]
    p = {
            .workdir = link,
            .beforeExec = {},
            .before = {".", this->getTempDirName()},
            .after = {target, this->getTempDirName()}
    };
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

    // [PWD, OLDPWD] is [not full path, valid with symlink] => [cwd, not update]
    p = {
            .workdir = this->getTempDirName(),
            .beforeExec = {},
            .before = {".", link},
            .after = {this->getTempDirName(), link}
    };
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

    // [PWD, OLDPWD] is [not dir, valid] => [cwd, not update]
    p = {
            .workdir = target,
            .beforeExec = {},
            .before = {BIN_PATH, this->getTempDirName()},
            .after = {target, this->getTempDirName()}
    };
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

    // [PWD, OLDPWD] is [invalid, invalid]
    // [PWD, OLDPWD] is [not set, not set] => [cwd, cwd]
    p = {
            .workdir = target,
            .beforeExec = {},
            .before = {"", ""},
            .after = {target, target}
    };
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

    // [PWD, OLDPWD] is [not dir, not dir] => [cwd, cwd]
    p = {
            .workdir = link,
            .beforeExec = {},
            .before = {BIN_PATH, BIN_PATH},
            .after = {target, target}
    };
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

    // [PWD, OLDPWD] is [not dir, not dir] => [cwd, cwd]
    p = {
            .workdir = link,
            .beforeExec = {},
            .before = {BIN_PATH, "."},
            .after = {target, target}
    };
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

    // cwd is not removed
    // [PWD, OLDPWD] is [valid with symlink, valid with symlink] => [cwd, cwd]
    p = {
            .workdir = link,
            .beforeExec = [&]{ removeDirWithRecursively(link.c_str()); },
            .before = {link, link},
            .after = {target, target}
    };
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

    // [PWD, OLDPWD] is [valid, valid] => [., not update]
    p = {
            .workdir = target,
            .beforeExec = [&]{ removeDirWithRecursively(target.c_str()); },
            .before = {target, this->getTempDirName()},
            .after = {".", this->getTempDirName()}
    };
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));


    builder = CL("mkdir -p %s; ln -s %s ./link", target.c_str(), target.c_str())
            .setWorkingDir(this->getTempDirName());
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 0));

    // [PWD, OLDPWD] is [valid with symlink, valid with symlink] => [., .]
    p = {
            .workdir = link,
            .beforeExec = [&]{ removeDirWithRecursively(target.c_str()); },
            .before = {link, link},
            .after = {".", "."}
    };
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));


    builder = CL("mkdir -p %s", target.c_str())
            .setWorkingDir(this->getTempDirName());
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 0));

    // [PWD, OLDPWD] is [valid, valid with symlink] => [not update, PWD]
    p = {
            .workdir = this->getTempDirName(),
            .beforeExec = [&]{ removeDirWithRecursively(link.c_str()); },
            .before = {this->getTempDirName(), link},
            .after = {this->getTempDirName(), this->getTempDirName()}
    };
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}