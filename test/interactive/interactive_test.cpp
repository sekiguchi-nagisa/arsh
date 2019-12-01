#include "gtest/gtest.h"

#include <config.h>
#include <misc/files.h>
#include "../test_common.h"


#ifndef INTERACTIVE_TEST_WORK_DIR
#error "require INTERACTIVE_TEST_WORK_DIR"
#endif


#ifndef BIN_PATH
#error "require BIN_PATH"
#endif

using namespace ydsh;


struct InteractiveTest : public InteractiveBase {
    InteractiveTest() : InteractiveBase(BIN_PATH, INTERACTIVE_TEST_WORK_DIR) {
        this->timeout = 120;
    }
};

#define CTRL_A "\x01"
#define CTRL_B "\x02"
#define CTRL_C "\x03"
#define CTRL_D "\x04"
#define CTRL_F "\x06"

#define UP "\x1b[A"
#define DOWN "\x1b[B"

#define XSTR(v) #v
#define STR(v) XSTR(v)

#define PROMPT "ydsh-" STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION) "$ "



TEST_F(InteractiveTest, exit) {
     this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("exit 30"));
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(30, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, ctrld1) {
    this->invoke("--norc");

    ASSERT_NO_FATAL_FAILURE(this->expectRegex("ydsh, version .+, build by .+\nCopy.+\nydsh-.+\\$ "));
    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, ctrld2) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("'hey'", ": String = hey\n" PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("false", PROMPT));
    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, ctrld3) {
    this->invoke("--quiet", "--norc", "-n");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, arg) {
    this->invoke("--quiet", "--norc", "-s", "hello", "world");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert $0 == 'ydsh'; assert $1 == 'hello';", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert $@.size() == 2; assert $# == 2;", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert $2 == 'world'", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert $@[0] == 'hello'; assert $@[1] == 'world'; exit"));
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, assert) {
    this->invoke("--quiet", "--norc", "-s", "hello", "world");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

    const char *e = "Assertion Error: `(1 == 2)'\n"
                    "    from (stdin):1 '<toplevel>()'\n";
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert(1 == 2)", "", e));
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, ctrlc) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

    std::string str = "throw 34";
    str += CTRL_C;
    this->send(str.c_str());
    ASSERT_NO_FATAL_FAILURE(this->expect("throw 34\n" PROMPT));
    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, tab) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    this->send("$F\t");
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(".+F"));
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(".+FALSE  False.+"));
    this->send("\t\r");
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(".+FALSE.+: Boolean = false.+"));
    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, edit1) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    this->send("t" CTRL_A "$" CTRL_F "re" CTRL_B "u\r");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "$true\n: Boolean = true\n" PROMPT));

    this->send("''" CTRL_F CTRL_F CTRL_B "い" CTRL_B "あ" CTRL_F "う" CTRL_B CTRL_B CTRL_B CTRL_B CTRL_B CTRL_B "\r");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "'あいう'\n: String = あいう\n" PROMPT));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

//TEST_F(InteractiveTest, edit2) {
//    this->invoke("--quiet", "--norc");
//
//    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
//    this->send("\u0041\u0303" CTRL_B "'" CTRL_F "'\r");
//    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "'\u0041\u0303'\n(String) \u0041\u0303\n" PROMPT));
//
//    this->send(CTRL_D);
//    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
//}

TEST_F(InteractiveTest, history1) {
#ifdef CODE_COVERAGE
    this->timeout = 500;
#endif

    this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile3");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("1", ": Int32 = 1\n" PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("2", ": Int32 = 2\n" PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("3", ": Int32 = 3\n" PROMPT));

    this->send(UP);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "3"));
    this->send(UP);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "2"));
    this->send(DOWN);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "3"));
    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect("\n: Int32 = 3\n" PROMPT));

    this->send(UP);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "3"));
    this->send(UP);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "2"));
    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect("\n: Int32 = 2\n" PROMPT));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, history2) {
#ifdef CODE_COVERAGE
    this->timeout = 500;
#endif

    this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile3");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("1", ": Int32 = 1\n" PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("2", ": Int32 = 2\n" PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("3", ": Int32 = 3\n" PROMPT));

    this->send(UP);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "3"));
    this->send(UP);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "2"));
    this->send(DOWN);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "3"));
    this->send(DOWN);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT ""));
    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect("\n" PROMPT));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, history3) {
#ifdef CODE_COVERAGE
    this->timeout = 500;
#endif

    this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile3");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("1", ": Int32 = 1\n" PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("2", ": Int32 = 2\n" PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("3", ": Int32 = 3\n" PROMPT));

    this->send(UP);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "3"));
    this->send(UP);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "2"));
    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect("\n: Int32 = 2\n" PROMPT));

    this->send(UP);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "2"));
    this->send(UP);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "3"));
    this->send(UP);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "2"));
    this->send(UP);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "1"));
    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect("\n: Int32 = 1\n" PROMPT));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, history4) {
#ifdef CODE_COVERAGE
    this->timeout = 500;
#endif

    this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile3");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("1", ": Int32 = 1\n" PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("2", ": Int32 = 2\n" PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("3", ": Int32 = 3\n" PROMPT));

    this->send(UP);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "3"));
    this->send(UP);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "2"));
    this->send("4");
    ASSERT_NO_FATAL_FAILURE(this->expect("4"));
    this->send(DOWN);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "3"));
    this->send(UP);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "24"));
    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect("\n: Int32 = 24\n" PROMPT));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}



TEST_F(InteractiveTest, status) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("true", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert $? == 0", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("false", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert $? == 1", PROMPT));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, except1) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

    const char *estr = R"([runtime error]
ArithmeticError: zero division
    from (stdin):1 '<toplevel>()'
)";

    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("45 / 0", PROMPT, estr));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, except2) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("throw 2", PROMPT, "[runtime error]\n2\n"));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, except5) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

    const char *estr = R"([runtime error]
ArithmeticError: zero division
    from (stdin):1 '<toplevel>()'
)";

    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("45 / 0", PROMPT, estr));

    this->send("exit\r");
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "exit\n"));
}

TEST_F(InteractiveTest, except4) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("throw 2", PROMPT, "[runtime error]\n2\n"));

    this->send("exit\r");
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "exit\n"));
}

TEST_F(InteractiveTest, signal) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert ($SIG[%'int'] as String) == $SIG_IGN as String", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert ($SIG[%'quit'] as String) == $SIG_IGN as String", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert ($SIG[%'tstp'] as String) == $SIG_IGN as String", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert ($SIG[%'ttin'] as String) == $SIG_IGN as String", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert ($SIG[%'ttou'] as String) == $SIG_IGN as String", PROMPT));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, standardInput) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert test -t 0;", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert test -t $STDIN", PROMPT));

    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert test -t 1;", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert test -t $STDOUT", PROMPT));

    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert !test -t 2;", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert !test -t $STDERR", PROMPT));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, rc1) {
    this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile1");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert $RC_VAR == 'rc file'; exit 23;"));
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(23, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, rc2) {
    this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile2");
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(10, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, rc3) {
    this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/faiurehfianf325d");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert $? == 0", PROMPT));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, rc4) {
    this->invoke("--quiet", "--rcfile", ".");
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "",
            "ydsh: [semantic error] cannot read module: `.', by `Is a directory'\n"));
}

TEST_F(InteractiveTest, termHook1) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("function f($s : Int, $a : Any) { echo hello; }; $TERM_HOOK=$f;", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("false", PROMPT));

    this->send(CTRL_D); // automatically insert 'exit'
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\nhello\n"));
}

TEST_F(InteractiveTest, termHook2) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("function f($s : Int, $a : Any) { echo hello; }; $TERM_HOOK=$f;", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("exit 56", "hello\n"));
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(56, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, termHook3) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("function f($s : Int, $a : Any) { echo hello; }; $TERM_HOOK=$f;", PROMPT));

    const char *estr = R"(Assertion Error: `false'
    from (stdin):2 '<toplevel>()'
)";
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert false", "hello\n", estr));
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, termHook4) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("function f($s : Int, $a : Any) { echo hello; }; $TERM_HOOK=$f;", PROMPT));

    const char *estr = R"([runtime error]
ArithmeticError: zero division
    from (stdin):2 '<toplevel>()'
)";
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("34 / 0", "hello\n" PROMPT, estr));   // call term hook in interactive mode

    this->send(CTRL_D); // automatically insert 'exit'
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\nhello\n"));
}

TEST_F(InteractiveTest, skip) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("      ", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("", PROMPT));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, read) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("read -u 0 -p '> ';", "", "> "));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("hello", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert $REPLY == 'hello'", PROMPT));

    // disable echo
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("read -u 0 -s -p '> ';", "", "> "));
    this->send("world!!\r");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert $REPLY == 'world!!'", PROMPT));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, continuation) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("echo \\", "> "));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("world", "world\n" PROMPT));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, throwFromLastPipe) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("true | throw 12", PROMPT, "[runtime error]\n12\n"));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, readAfterLastPipe) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("var a = 23|'>> '; read -u 0 -p $a;", "", ">> "));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("hello", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert $REPLY == 'hello'", PROMPT));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, printStackTop) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("34|$false", ": Boolean = false\n" PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("34|true", PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("true", PROMPT));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, cmdSubstitution) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("\"$(stty sane)\"", ": String = \n" PROMPT));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, procSubstitution) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("echo <(ls) > /dev/null", PROMPT));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, moduleError1) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

    auto eout = format("" INTERACTIVE_TEST_WORK_DIR "/mod1.ds:6: [semantic error] require `Int32' type, but is `Boolean' type\n"
                       "34 / /\n"
                       "     ^\n"
                       "(stdin):1: [note] at module import\n"
                       "source " INTERACTIVE_TEST_WORK_DIR "/mod1.ds\n"
                       "       %s\n",
                       makeLineMarker(INTERACTIVE_TEST_WORK_DIR "/mod1.ds").c_str());
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("source " INTERACTIVE_TEST_WORK_DIR "/mod1.ds", PROMPT, eout.c_str()));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("f", PROMPT, "[runtime error]\n"
                                                             "SystemError: execution error: f: command not found\n"
                                                             "    from (stdin):2 '<toplevel>()'\n"));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, moduleError2) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

    auto eout = format("" INTERACTIVE_TEST_WORK_DIR "/mod1.ds:6: [semantic error] require `Int32' type, but is `Boolean' type\n"
                       "34 / /\n"
                       "     ^\n"
                       "(stdin):1: [note] at module import\n"
                       "source " INTERACTIVE_TEST_WORK_DIR "/mod1.ds\n"
                       "       %s\n",
                       makeLineMarker(INTERACTIVE_TEST_WORK_DIR "/mod1.ds").c_str());
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("source " INTERACTIVE_TEST_WORK_DIR "/mod1.ds", PROMPT, eout.c_str()));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("hey", PROMPT, "[runtime error]\n"
                                                               "SystemError: execution error: hey: command not found\n"
                                                               "    from (stdin):2 '<toplevel>()'\n"));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, moduleError3) {
    this->invoke("--quiet", "--norc");

    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

    const char *eout = "[runtime error]\n"
                       "ArithmeticError: zero division\n"
                       "    from " INTERACTIVE_TEST_WORK_DIR "/mod2.ds:6 '<toplevel>()'\n"
                       "    from (stdin):1 '<toplevel>()'\n";
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("source " INTERACTIVE_TEST_WORK_DIR "/mod2.ds", PROMPT, eout));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("hey", PROMPT, "[runtime error]\n"
                                                               "SystemError: execution error: hey: command not found\n"
                                                               "    from (stdin):2 '<toplevel>()'\n"));

    this->send(CTRL_D);
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}