#include "gtest/gtest.h"

#include "../../tools/platform/platform.h"
#include "../test_common.h"
#include <config.h>

#ifndef INTERACTIVE_TEST_WORK_DIR
#error "require INTERACTIVE_TEST_WORK_DIR"
#endif

#ifndef BIN_PATH
#error "require BIN_PATH"
#endif

using namespace ydsh;

#define CTRL_A "\x01"
#define CTRL_B "\x02"
#define CTRL_C "\x03"
#define CTRL_D "\x04"
#define CTRL_F "\x06"

#define UP "\x1b[A"
#define DOWN "\x1b[B"

#define XSTR(v) #v
#define STR(v) XSTR(v)

#define PROMPT this->prompt

static std::string initPrompt() {
  std::string v = "ydsh-" STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION);
  v += (getuid() == 0 ? "# " : "$ ");
  return v;
}

struct InteractiveTest : public InteractiveShellBase {
  InteractiveTest() : InteractiveShellBase(BIN_PATH, INTERACTIVE_TEST_WORK_DIR) {
    this->timeout = 120;
    this->setPrompt(initPrompt());
  }
};

TEST_F(InteractiveTest, exit) {
  this->invoke("--quiet", "--norc");
  ASSERT_NO_FATAL_FAILURE(this->withTimeout(300, [&] { this->expect(PROMPT); }));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit 30", 30, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, ctrld1) {
  this->invoke("--norc");

  ASSERT_NO_FATAL_FAILURE(this->withTimeout(300, [&] {
    std::string re = "ydsh, version .+, build by .+\nCopy.+\nydsh-.+";
    re += (getuid() == 0 ? "# " : "\\$ ");
    this->expectRegex(re);
  }));
  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, ctrld2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("'hey'", ": String = hey"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("false"));
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
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $0 == 'ydsh'; assert $1 == 'hello';"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $@.size() == 2; assert $# == 2;"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $2 == 'world'"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait(
      "assert $@[0] == 'hello'; assert $@[1] == 'world'; exit", 0, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, assert) {
  this->invoke("--quiet", "--norc", "-s", "hello", "world");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  const char *e = "Assertion Error: `(1 == 2)'\n"
                  "    from (stdin):1 '<toplevel>()'\n";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("assert(1 == 2)", 1, WaitStatus::EXITED, "", e));
}

TEST_F(InteractiveTest, ctrlc1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  std::string str = "throw 34";
  str += CTRL_C;
  this->send(str.c_str());
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "throw 34\n" + PROMPT));
  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, ctrlc2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->sendLine("cat");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "cat\n"));
  sleep(1);
  this->send(CTRL_C);
  std::string err = strsignal(SIGINT);
  err += "\n";

  if (platform::platform() == platform::PlatformType::CYGWIN) {
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT, err));
  } else {
    ASSERT_NO_FATAL_FAILURE(this->expect("^C%\n" + PROMPT, err));
  }
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 128 + SIGINT));
}

TEST_F(InteractiveTest, ctrlc3) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->sendLine("cat < /dev/zero > /dev/null");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "cat < /dev/zero > /dev/null\n"));
  sleep(1);
  this->send(CTRL_C);
  std::string err = strsignal(SIGINT);
  err += "\n";
  if (platform::platform() == platform::PlatformType::CYGWIN) {
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT, err));
  } else {
    ASSERT_NO_FATAL_FAILURE(this->expect("^C%\n" + PROMPT, err));
  }
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 128 + SIGINT));
}

TEST_F(InteractiveTest, ctrlc4) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->sendLine("read");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "read\n"));
  sleep(1);
  this->send(CTRL_C);

  std::string err = format(R"(ydsh: read: 0: %s
[runtime error]
SystemError: %s
    from (builtin):17 'function _DEF_SIGINT()'
    from (stdin):1 '<toplevel>()'
)",
                           strerror(EINTR), strsignal(SIGINT));

  if (platform::platform() == platform::PlatformType::CYGWIN) {
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT, err));
  } else {
    ASSERT_NO_FATAL_FAILURE(this->expect("^C%\n" + PROMPT, err));
  }
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 1));
}

TEST_F(InteractiveTest, ctrlc5) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->sendLine("read | grep hoge");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "read | grep hoge\n"));
  sleep(1);
  this->send(CTRL_C);
  std::string err = strsignal(SIGINT);
  err += "\n";
  if (platform::platform() == platform::PlatformType::CYGWIN) {
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT, err));
  } else {
    ASSERT_NO_FATAL_FAILURE(this->expect("^C%\n" + PROMPT, err));
  }
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 128 + SIGINT));
}

TEST_F(InteractiveTest, wait_ctrlc1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var j = while(true){} &"));
  this->sendLine("$j.wait()");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "$j.wait()\n"));
  sleep(1);
  this->send(CTRL_C);

  std::string err = format(R"([runtime error]
SystemError: wait failed: %s
    from (stdin):2 '<toplevel>()'
)",
                           strerror(EINTR));

  if (platform::platform() == platform::PlatformType::CYGWIN) {
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT, err));
  } else {
    ASSERT_NO_FATAL_FAILURE(this->expect("^C%\n" + PROMPT, err));
  }
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 1));
}

TEST_F(InteractiveTest, wait_ctrlc2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("while(true){} &", ": Job = %1"));
  this->sendLine("fg");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "fg\n"));
  sleep(1);
  this->send(CTRL_C);

  std::string err = strsignal(SIGINT);
  err += "\n";
  if (platform::platform() == platform::PlatformType::CYGWIN) {
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT, err));
  } else {
    ASSERT_NO_FATAL_FAILURE(this->expect("^C%\n" + PROMPT, err));
  }
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 128 + SIGINT));
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
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "$true\n: Boolean = true\n" + PROMPT));

  this->send("''" CTRL_F CTRL_F CTRL_B "い" CTRL_B "あ" CTRL_F
             "う" CTRL_B CTRL_B CTRL_B CTRL_B CTRL_B CTRL_B "\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "'あいう'\n: String = あいう\n" + PROMPT));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

// TEST_F(InteractiveTest, edit2) {
//     this->invoke("--quiet", "--norc");
//
//     ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
//     this->send("\u0041\u0303" CTRL_B "'" CTRL_F "'\r");
//     ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT "'\u0041\u0303'\n(String) \u0041\u0303\n"
//     PROMPT));
//
//     this->send(CTRL_D);
//     ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
// }

TEST_F(InteractiveTest, history1) {
#ifdef CODE_COVERAGE
  this->timeout = 500;
#endif

  this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile3");

  ASSERT_NO_FATAL_FAILURE(this->withTimeout(800, [&] { this->expect(PROMPT); }));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("1", ": Int = 1"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("2", ": Int = 2"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("3", ": Int = 3"));

  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "3"));
  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "2"));
  this->send(DOWN);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "3"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect("\n: Int = 3\n" + PROMPT));

  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "3"));
  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "2"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect("\n: Int = 2\n" + PROMPT));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, history2) {
#ifdef CODE_COVERAGE
  this->timeout = 500;
#endif

  this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile3");

  ASSERT_NO_FATAL_FAILURE(this->withTimeout(800, [&] { this->expect(PROMPT); }));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("1", ": Int = 1"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("2", ": Int = 2"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("3", ": Int = 3"));

  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "3"));
  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "2"));
  this->send(DOWN);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "3"));
  this->send(DOWN);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect("\n" + PROMPT));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, history3) {
#ifdef CODE_COVERAGE
  this->timeout = 500;
#endif

  this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile3");

  ASSERT_NO_FATAL_FAILURE(this->withTimeout(800, [&] { this->expect(PROMPT); }));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("1", ": Int = 1"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("2", ": Int = 2"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("3", ": Int = 3"));

  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "3"));
  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "2"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect("\n: Int = 2\n" + PROMPT));

  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "2"));
  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "3"));
  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "2"));
  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "1"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect("\n: Int = 1\n" + PROMPT));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, history4) {
#ifdef CODE_COVERAGE
  this->timeout = 500;
#endif

  this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile3");

  ASSERT_NO_FATAL_FAILURE(this->withTimeout(800, [&] { this->expect(PROMPT); }));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("1", ": Int = 1"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("2", ": Int = 2"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("3", ": Int = 3"));

  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "3"));
  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "2"));
  this->send("4");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "24"));
  this->send(DOWN);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "3"));
  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "24"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect("\n: Int = 24\n" + PROMPT));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, status) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("true"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $? == 0"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("false"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $? == 1"));

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

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("45 / 0", "", estr));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, except2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("throw 2", "", "[runtime error]\n2\n"));

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

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("45 / 0", "", estr));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 1, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, except4) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("throw 2", "", "[runtime error]\n2\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 1, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, signal) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert ($SIG[%'int'] as String) != $SIG_IGN as String"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert ($SIG[%'int'] as String) != $SIG_DFL as String"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert ($SIG[%'quit'] as String) == $SIG_IGN as String"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert ($SIG[%'tstp'] as String) == $SIG_IGN as String"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert ($SIG[%'ttin'] as String) == $SIG_IGN as String"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert ($SIG[%'ttou'] as String) == $SIG_IGN as String"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, standardInput) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert test -t 0;"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert test -t $STDIN"));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert test -t 1;"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert test -t $STDOUT"));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert !test -t 2;"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert !test -t $STDERR"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, rc1) {
  this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile1");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $IS_REPL"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndWait("assert $RC_VAR == 'rc file'; exit 23;", 23, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, rc2) {
  this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile2");
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(10, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, rc3) {
  this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/faiurehfianf325d");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $? == 0"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, rc4) {
  this->invoke("--quiet", "--rcfile", ".");
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "",
                                              "ydsh: cannot load file: ., by `Is a directory'\n"));
}

TEST_F(InteractiveTest, rc5) {
  this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile1");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var a = $(shctl module)"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $a.size() == 3: $a.size() as String"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $a[0] == '(builtin)'"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $a[1] == '(root)'"));
  const char *src = "assert $a[2] == '" INTERACTIVE_TEST_WORK_DIR "/rcfile1'";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(src));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, rc6) {
  this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile1");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $IS_REPL"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("source " INTERACTIVE_TEST_WORK_DIR "/rcfile1 as mod"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $RC_VAR == $mod.RC_VAR;"));
  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, termHook1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("function f($s : Int, $a : Any) { echo hello; }; $TERM_HOOK=$f;"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("false"));

  this->send(CTRL_D); // automatically insert 'exit'
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\nhello\n"));
}

TEST_F(InteractiveTest, termHook2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("function f($s : Int, $a : Any) { echo hello; }; $TERM_HOOK=$f;"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit 56", 56, WaitStatus::EXITED, "hello\n"));
}

TEST_F(InteractiveTest, termHook3) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("function f($s : Int, $a : Any) { echo hello; }; $TERM_HOOK=$f;"));

  const char *estr = R"(Assertion Error: `false'
    from (stdin):2 '<toplevel>()'
)";
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndWait("assert false", 1, WaitStatus::EXITED, "hello\n", estr));
}

TEST_F(InteractiveTest, termHook4) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("function f($s : Int, $a : Any) { echo hello; }; $TERM_HOOK=$f;"));

  const char *estr = R"([runtime error]
ArithmeticError: zero division
    from (stdin):2 '<toplevel>()'
)";
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("34 / 0", "hello", estr)); // call term hook in interactive mode

  this->send(CTRL_D); // automatically insert 'exit'
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\nhello\n"));
}

TEST_F(InteractiveTest, skip) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("      "));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(""));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, read) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->sendLine("read -u 0 -p '> ';");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "read -u 0 -p '> ';\n", "> "));
  this->sendLine("hello");
  ASSERT_NO_FATAL_FAILURE(this->expect("hello\n" + PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $REPLY == 'hello'"));

  // disable echo
  this->sendLine("read -u 0 -s -p '> ';");
  this->expect(PROMPT + "read -u 0 -s -p '> ';\n", "> ");
  this->sendLine("world!!");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $REPLY == 'world!!'"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, continuation) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->sendLine("echo \\");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "echo \\\n> "));
  this->sendLine("world");
  ASSERT_NO_FATAL_FAILURE(this->expect("> world\nworld\n" + PROMPT));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, throwFromLastPipe) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("true | throw 12", "", "[runtime error]\n12\n"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, readAfterLastPipe) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->sendLine("var a = 23|'>> '; read -u 0 -p $a;");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "var a = 23|'>> '; read -u 0 -p $a;\n", ">> "));
  this->sendLine("hello");
  ASSERT_NO_FATAL_FAILURE(this->expect("hello\n" + PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $REPLY == 'hello'"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, printStackTop) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->withTimeout(300, [&] { this->expect(PROMPT); }));
  ASSERT_NO_FATAL_FAILURE(
      this->withTimeout(300, [&] { this->sendLineAndExpect("34|$false", ": Boolean = false"); }));
  ASSERT_NO_FATAL_FAILURE(this->withTimeout(300, [&] { this->sendLineAndExpect("34|true"); }));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("true"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, cmdSubstitution) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(
      this->withTimeout(600, [&] { this->sendLineAndExpect("var a = \"$(stty sane)\""); }));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$a", ": String = "));
  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, procSubstitution) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("echo <(ls) > /dev/null"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, pipestatus) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("kill -s STOP $PID | kill -s STOP $PID | 1234", ": Int = 1234"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $PIPESTATUS.size() == 3"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      format("assert $PIPESTATUS[0] == %d : $PIPESTATUS[0] as String", 128 + SIGSTOP).c_str()));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      format("assert $PIPESTATUS[1] == %d : $PIPESTATUS[1] as String", 128 + SIGSTOP).c_str()));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $PIPESTATUS[2] == 0"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("kill -s CONT %1"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("fg %1", "", "ydsh: fg: %1: no such job\n"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, moduleError1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  auto eout = format("" INTERACTIVE_TEST_WORK_DIR
                     "/mod1.ds:6:6: [semantic error] require `Int' type, but is `Boolean' type\n"
                     "34 / /\n"
                     "     ^\n"
                     "(stdin):1:8: [note] at module import\n"
                     "source " INTERACTIVE_TEST_WORK_DIR "/mod1.ds\n"
                     "       %s\n",
                     makeLineMarker(INTERACTIVE_TEST_WORK_DIR "/mod1.ds").c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("source " INTERACTIVE_TEST_WORK_DIR "/mod1.ds", "", eout.c_str()));
  ASSERT_NO_FATAL_FAILURE(this->withTimeout(400, [&] {
    this->sendLineAndExpect("f", "",
                            "[runtime error]\n"
                            "SystemError: execution error: f: command not found\n"
                            "    from (stdin):2 '<toplevel>()'\n");
  }));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, moduleError2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  auto eout = format("" INTERACTIVE_TEST_WORK_DIR
                     "/mod1.ds:6:6: [semantic error] require `Int' type, but is `Boolean' type\n"
                     "34 / /\n"
                     "     ^\n"
                     "(stdin):1:8: [note] at module import\n"
                     "source " INTERACTIVE_TEST_WORK_DIR "/mod1.ds\n"
                     "       %s\n",
                     makeLineMarker(INTERACTIVE_TEST_WORK_DIR "/mod1.ds").c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("source " INTERACTIVE_TEST_WORK_DIR "/mod1.ds", "", eout.c_str()));
  ASSERT_NO_FATAL_FAILURE(this->withTimeout(600, [&] {
    this->sendLineAndExpect("hey", "",
                            "[runtime error]\n"
                            "SystemError: execution error: hey: command not found\n"
                            "    from (stdin):2 '<toplevel>()'\n");
  }));

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
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("source " INTERACTIVE_TEST_WORK_DIR "/mod2.ds", "", eout));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("hey", "hey!!"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, moduleError4) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $? == 0"));
  const char *eout = "[runtime error]\n"
                     "ArithmeticError: zero division\n"
                     "    from " INTERACTIVE_TEST_WORK_DIR "/mod2.ds:6 '<toplevel>()'\n"
                     "    from (stdin):2 '<toplevel>()'\n";
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("source " INTERACTIVE_TEST_WORK_DIR "/mod2.ds as mod", "", eout));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$mod", ": %mod2 = module(%mod2)"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, illegalcmd) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $? == 0"));
  const char *eout = "[runtime error]\n"
                     "ArithmeticError: zero division\n"
                     "    from (stdin):2 '<toplevel>()'\n";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("23/0; f() { echo $0 $@; }", "", eout));

  eout = "[runtime error]\n"
         "IllegalAccessError: attemp to access uninitialized user-defined command: `f'\n"
         "    from (stdin):3 '<toplevel>()'\n";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("f 1 2 3", "", eout));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("command -v f; assert $? == 1"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("command -V f", "", "ydsh: command: f: uninitialized\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert ! $MODULE.fullname('f')"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}