#include "interative_base.hpp"

TEST_F(InteractiveTest, exit) {
  this->invoke("--quiet", "--norc");
  ASSERT_NO_FATAL_FAILURE(this->withTimeout(300, [&] { this->expect(PROMPT); }));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit 30", 30, WaitStatus::EXITED));
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
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "assert ($SIG['int'].trap($SIG_NUL) as String) != $SIG_IGN as String"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "assert ($SIG['int'].trap($SIG_NUL) as String) != $SIG_DFL as String"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "assert ($SIG['quit'].trap($SIG_NUL) as String) == $SIG_IGN as String"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "assert ($SIG['tstp'].trap($SIG_NUL) as String) == $SIG_IGN as String"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "assert ($SIG['ttin'].trap($SIG_NUL) as String) == $SIG_IGN as String"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "assert ($SIG['ttou'].trap($SIG_NUL) as String) == $SIG_IGN as String"));

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

TEST_F(InteractiveTest, rc7) {
  this->addEnv("XDG_CONFIG_HOME", INTERACTIVE_TEST_WORK_DIR);
  this->invoke("--quiet");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "assert $RC_VAR == 'rcfile loaded: " INTERACTIVE_TEST_WORK_DIR "/ydsh/ydshrc'"));
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
  this->invokeImpl({"--quiet", "--norc"}, true);

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  auto eout = format("" INTERACTIVE_TEST_WORK_DIR
                     "/mod1.ds:6:6: [semantic error] require `Int' type, but is `Boolean' type\n"
                     "34 / /\n"
                     "     ^\n"
                     "(stdin):1:8: [note] at module import\n"
                     "source " INTERACTIVE_TEST_WORK_DIR "/mod1.ds\n"
                     "       %s",
                     makeLineMarker(INTERACTIVE_TEST_WORK_DIR "/mod1.ds").c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("source " INTERACTIVE_TEST_WORK_DIR "/mod1.ds", eout.c_str()));
  ASSERT_NO_FATAL_FAILURE(this->withTimeout(400, [&] {
    this->sendLineAndExpect("f", "[runtime error]\n"
                                 "SystemError: execution error: f: command not found\n"
                                 "    from (stdin):2 '<toplevel>()'");
  }));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(127, WaitStatus::EXITED, "\n"));
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
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(127, WaitStatus::EXITED, "\n"));
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
         "IllegalAccessError: attempt to access uninitialized user-defined command: `f'\n"
         "    from (stdin):3 '<toplevel>()'\n";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("f 1 2 3", "", eout));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("command -v f; assert $? == 1"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("command -V f", "", "ydsh: command: f: uninitialized\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert ! $MODULE._fullname('f')"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, illegalMethod) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  // illegal method call
  const char *eout = "[runtime error]\n"
                     "ArithmeticError: zero division\n"
                     "    from (stdin):1 '<toplevel>()'\n";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("23/0; function f() for Int {}", "", eout));
  eout = "[runtime error]\n"
         "IllegalAccessError: attempt to call uninitialized method or constructor\n"
         "    from (stdin):2 '<toplevel>()'\n";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("34.f()", "", eout));

  // illegal constructor call
  eout = "[runtime error]\n"
         "ArithmeticError: zero modulo\n"
         "    from (stdin):3 '<toplevel>()'\n";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("23%0; typedef AAA { var a = 0; }", "", eout));
  eout = "[runtime error]\n"
         "IllegalAccessError: attempt to call uninitialized method or constructor\n"
         "    from (stdin):4 '<toplevel>()'\n";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("new AAA()", "", eout));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}