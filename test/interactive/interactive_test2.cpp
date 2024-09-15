#include "interactive_base.hpp"

TEST_F(InteractiveTest, exit) {
  this->invoke("--quiet", "--norc");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit 30", 30, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, arg) {
  this->invoke("--quiet", "--norc", "-s", "hello", "world");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      format("assert $0 == '%s'; assert $1 == 'hello';", BIN_PATH).c_str()));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $@.size() == 2; assert $# == 2;"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $2 == 'world'"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait(
      "assert $@[0] == 'hello'; assert $@[1] == 'world'; exit", 0, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, assert1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  const char *e = R"([runtime error]
Assertion Error: `(false)'
    from (stdin):1 '<toplevel>()'
)";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("assert(false)", 1, WaitStatus::EXITED, "", e));
}

TEST_F(InteractiveTest, assert2) {
  this->invoke("--quiet", "--norc", "-s", "hello", "world");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  const char *e = R"([runtime error]
Assertion Error: `($x == $y)'
binary expression `<LHS> == <RHS>' is false
  <LHS>: Int = 1
  <RHS>: Int = 2
    from (stdin):2 '<toplevel>()'
)";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var x = 1; var y = 2;"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("assert($x == $y)", 1, WaitStatus::EXITED, "", e));
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

  const char *estr = R"([runtime error]
ArithmeticError: zero division
    from (stdin):1 '<toplevel>()'
)";

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("45 / 0", "", estr));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 1, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, signal) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert ($SIG['int'].trap() as String) != $SIG_IGN as String"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert ($SIG['int'].trap() as String) != $SIG_DFL as String"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert ($SIG['quit'].trap() as String) == $SIG_IGN as String"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert ($SIG['tstp'].trap() as String) == $SIG_IGN as String"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert ($SIG['ttin'].trap() as String) == $SIG_IGN as String"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert ($SIG['ttou'].trap() as String) == $SIG_IGN as String"));

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
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(10, WaitStatus::EXITED, "\n"));
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
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT, "arsh: cannot load file: ., by `Is a directory'\n"));
  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
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
      "assert $RC_VAR == 'rcfile loaded: " INTERACTIVE_TEST_WORK_DIR "/arsh/arshrc'"));
  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, termHook1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "function f($s : Int, $a : Any) { echo atexit: $s, $a; }; $TERM_HOOK=$f;"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("false"));

  this->send(CTRL_D); // automatically insert 'exit'
  std::string err = format("\natexit: %d, 1\n", TERM_ON_EXIT);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, err));
}

TEST_F(InteractiveTest, termHook2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "function f($s : Int, $a : Any) { echo atexit: $s, $a; }; $TERM_HOOK=$f;"));
  std::string err = format("atexit: %d, 56\n", TERM_ON_EXIT);
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit 56", 56, WaitStatus::EXITED, err.c_str()));
}

TEST_F(InteractiveTest, termHook3) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "function f($s : Int, $a : Any) { echo atexit: $s, $a; }; $TERM_HOOK=$f;"));

  const char *estr = R"([runtime error]
Assertion Error: `false'
    from (stdin):2 '<toplevel>()'
)";
  std::string err = format("atexit: %d, 1\n", TERM_ON_ASSERT);
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndWait("assert false", 1, WaitStatus::EXITED, err.c_str(), estr));
}

TEST_F(InteractiveTest, termHook4) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "function f($s : Int, $a : Any) { echo atexit: $s, $a; }; $TERM_HOOK=$f;"));

  const char *estr = R"([runtime error]
ArithmeticError: zero division
    from (stdin):2 '<toplevel>()'
)";
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("34 / 0", "", estr)); // call term hook in interactive mode

  this->send(CTRL_D); // automatically insert 'exit'
  std::string err = format("\natexit: %d, 1\n", TERM_ON_EXIT);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, err));
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

TEST_F(InteractiveTest, throwFromLastPipe) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  const char *estr = R"([runtime error]
ExecError: foo
    from (stdin):1 '<toplevel>()'
)";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("true | throw new ExecError('foo')", "", estr));

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

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  {
    auto cleanup = this->withTimeout(300);
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("34|$false", ": Bool = false"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("34|true"));
  }
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("true"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("AAA=34 true"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, cmdSubstitution) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  {
    auto cleanup = this->withTimeout(600);
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var a = \"$(stty sane)\""));
  }
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
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("true"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("fg %1", "", "(stdin):8: fg: %1: no such job\n"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, fallback) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$CMD_FALLBACK = function(m,s) => $false"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("{ fjeriferi; }", ": Bool = false"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, execFail) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  const char *error = R"([runtime error]
SystemError: execution error: hgoirahj: command not found
    from (stdin):1 '<toplevel>()'
)";

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("exec hgoirahj", "", error));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(127, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, winSize1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  const auto size = this->handle.getWinSize();
  std::string out = format("LINES=%d\nCOLUMNS=%d", size.rows, size.cols);
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("shctl winsize", out.c_str()));
  std::string src = format("assert $LINES == %d; assert $COLUMNS == %d", size.rows, size.cols);
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(src.c_str()));

  // change winsize
  this->changeWinSize({.rows = 100, .cols = 100});
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $LINES == 100; assert $COLUMNS == 100"));

  if (platform::isLinux(platform::platform())) {
    std::string err = format("(stdin):4: shctl winsize: io error: %s\n", strerror(ENOSPC));
    ASSERT_NO_FATAL_FAILURE(
        this->sendLineAndExpect("shctl winsize > /dev/full; true", "", err.c_str()));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, winSize2) {
  if (!platform::isLinux(platform::platform())) {
    return;
  }

  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  {
    const auto size = this->handle.getWinSize();
    ASSERT_NE(100, size.rows);
    ASSERT_NE(300, size.cols);
  }

  // change winsize
  std::string line = "call $BIN_NAME -c 'for(var i = 0; $i < 5; $i++) { sleep 0.5; }; true; assert "
                     "$LINES == 100; assert "
                     "$COLUMNS == 300; echo $LINES;'";
  this->sendLine(line.c_str());
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  this->changeWinSize({.rows = 100, .cols = 300});
  std::this_thread::sleep_for(std::chrono::seconds(3));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + line + "\n100\n" + PROMPT));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, moduleError1) {
  this->invokeImpl({"--quiet", "--norc"}, true);

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  auto eout = format("[semantic error] require `Int' type, but is `Bool' type\n"
                     " --> " INTERACTIVE_TEST_WORK_DIR "/mod1.ds:6:6\n"
                     "34 / /\n"
                     "     ^\n"
                     "[note] at module import\n"
                     " --> (stdin):1:8\n"
                     "source " INTERACTIVE_TEST_WORK_DIR "/mod1.ds\n"
                     "       %s",
                     makeLineMarker(INTERACTIVE_TEST_WORK_DIR "/mod1.ds").c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("source " INTERACTIVE_TEST_WORK_DIR "/mod1.ds", eout.c_str()));
  {
    auto cleanup = this->withTimeout(400);
    ASSERT_NO_FATAL_FAILURE(
        this->sendLineAndExpect("f", "[runtime error]\n"
                                     "SystemError: execution error: f: command not found\n"
                                     "    from (stdin):2 '<toplevel>()'"));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(127, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, moduleError2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  auto eout = format("[semantic error] require `Int' type, but is `Bool' type\n"
                     " --> " INTERACTIVE_TEST_WORK_DIR "/mod1.ds:6:6\n"
                     "34 / /\n"
                     "     ^\n"
                     "[note] at module import\n"
                     " --> (stdin):1:8\n"
                     "source " INTERACTIVE_TEST_WORK_DIR "/mod1.ds\n"
                     "       %s\n",
                     makeLineMarker(INTERACTIVE_TEST_WORK_DIR "/mod1.ds").c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("source " INTERACTIVE_TEST_WORK_DIR "/mod1.ds", "", eout.c_str()));
  {
    auto cleanup = this->withTimeout(600);
    ASSERT_NO_FATAL_FAILURE(
        this->sendLineAndExpect("hey", "",
                                "[runtime error]\n"
                                "SystemError: execution error: hey: command not found\n"
                                "    from (stdin):2 '<toplevel>()'\n"));
  }

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

TEST_F(InteractiveTest, sourceGlobLimit) {
  if (isCygwinOrMsys(platform::platform())) {
    return; // skip since ulimit -S -n does not work in cygwin
  }

  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("ulimit -S -n 6; assert \"$(ulimit -S -n)\" == '6'"));
  std::string err =
      format(R"([semantic error] not enough resources for glob expansion, caused by `%s'
 --> (stdin):2:9
source? /*//*//*/*//*/*//*/*/*//**/?!/*/*/*/*/s*/../*/../*
        ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
)",
             strerror(EMFILE));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "source? /*//*//*/*//*/*//*/*/*//**/?!/*/*/*/*/s*/../*/../*", "", err.c_str()));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
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
      this->sendLineAndExpect("command -V f", "", "(stdin):5: command: f: uninitialized\n"));
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
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("typedef BigInt(a:Int) { let v = $a;}; 23/0; "
                                                  "function f() for BigInt {}",
                                                  "", eout));
  eout = "[runtime error]\n"
         "IllegalAccessError: attempt to call uninitialized method or constructor\n"
         "    from (stdin):2 '<toplevel>()'\n";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("new BigInt(34).f()", "", eout));

  // illegal constructor call
  eout = "[runtime error]\n"
         "ArithmeticError: zero modulo\n"
         "    from (stdin):3 '<toplevel>()'\n";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("23%0; typedef AAA ( ){ var a = 0; }", "", eout));
  eout = "[runtime error]\n"
         "IllegalAccessError: attempt to call uninitialized method or constructor\n"
         "    from (stdin):4 '<toplevel>()'\n";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("new AAA()", "", eout));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}