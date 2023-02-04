#include "interactive_base.hpp"

TEST_F(InteractiveTest, ctrld1) {
  this->invoke("--norc");

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  std::string re = "ydsh, version .+, build by .+\nCopy.+\nydsh-.+";
  re += (getuid() == 0 ? "# " : "\\$ ");
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(re););
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
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  this->send(CTRL_C);
  std::string err = strsignal(SIGINT);
  err += "\n";
  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 128 + SIGINT));
}

TEST_F(InteractiveTest, ctrlc3) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->sendLine("cat < /dev/zero > /dev/null");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "cat < /dev/zero > /dev/null\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  this->send(CTRL_C);
  std::string err = strsignal(SIGINT);
  err += "\n";
  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 128 + SIGINT));
}

TEST_F(InteractiveTest, ctrlc4) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->sendLine("read");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "read\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  this->send(CTRL_C);

  std::string err = format(R"(ydsh: read: 0: %s
[runtime error]
SystemError: %s
    from (builtin):8 'function _DEF_SIGINT()'
    from (stdin):1 '<toplevel>()'
)",
                           strerror(EINTR), strsignal(SIGINT));

  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 1));
}

TEST_F(InteractiveTest, ctrlc5) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->sendLine("read | grep hoge");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "read | grep hoge\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  this->send(CTRL_C);
  std::string err = strsignal(SIGINT);
  err += "\n";
  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 128 + SIGINT));
}

TEST_F(InteractiveTest, tab1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$LINE_EDIT.setCompletion(function(m, s) => { complete -m $m -q -s -- $s; $COMPREPLY;})"));
  this->send("$F\t");
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(".+F"));
  this->send("\t");
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(".+FALSE  False.+"));
  this->send("\t\r");
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(".+FALSE.+: Bool = false.+"));
  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, tab2) {
  this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile1");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$LINE_EDIT.setCompletion(function(m, s) => { complete -m $m -q -s -- $s; $COMPREPLY;})"));
  this->send("$RC\t");
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(".+RC_VAR"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(".+: String = rc file.+"));
  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, tab3) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$LINE_EDIT.setCompletion(function(m,s) => { ['@abc\\ -\\ @.csv']; })"));
  this->send("echo @\t");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "echo @abc\\ -\\ @.csv"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("", "@abc - @.csv"));
  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, edit1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->send("t" CTRL_A "$" CTRL_F "re" CTRL_B "u\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "$true\n: Bool = true\n" + PROMPT));

  this->send("''" CTRL_F CTRL_F CTRL_B "い" CTRL_B "あ" CTRL_F
             "う" CTRL_B CTRL_B CTRL_B CTRL_B CTRL_B CTRL_B "\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "'あいう'\n: String = あいう\n" + PROMPT));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, edit2) {
  this->invoke("--quiet", "--norc");

  // for unicode-aware word edit

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->send("echo /usr/shareケケケ" CTRL_W CTRL_W "bin\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "echo /usr/bin\n/usr/bin\n" + PROMPT));

  this->send("echo /usr/ケケケ" ESC_("b") ESC_("d") "hare" ESC_("b") "s" ESC_("f") "\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "echo /usr/share\n/usr/share\n" + PROMPT));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, edit3) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->send("23" CTRL_U "'あいう" CTRL_H "え" CTRL_A CTRL_T RIGHT RIGHT CTRL_T CTRL_T CTRL_T
             "'\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "'いあえ'\n: String = いあえ\n" + PROMPT));

  this->send("あい" LEFT CTRL_D "いぇお" CTRL_A CTRL_K "@@@" LEFT LEFT CTRL_E CTRL_U "12\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "12\n: Int = 12\n" + PROMPT));

  this->send("@" HOME "1" END "あ" ESC_("OH") "2" ESC_("OF") "い" ESC_("[1~") "'" ESC_(
      "[4~") "○" LEFT ESC_("[3~") "'\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "'21@あい'\n: String = 21@あい\n" + PROMPT));

  // alt-left, alt-right ^[[1;3C ^[[1;3D ^[^[[C ^[^[[D
  this->send("'home'" ESC_("[1;3D") ESC_("\x1b[D") "/" ESC_("[1;3C") "/user" ESC_("\x1b[C") "\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "'/home/user'\n: String = /home/user\n" + PROMPT));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, keybind) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->send(ESC_("q") "\r");
  ASSERT_NO_FATAL_FAILURE(this->expect("\n" + PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^[q', 'backward-word')"));
  this->send("'/user" ESC_("q") "home/" ESC_("[F") "'\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "'/home/user'\n: String = /home/user\n" + PROMPT));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, customAction1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  // replace-whole
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$LINE_EDIT.action('action1', 'replace-whole', "
                              "function(s, m) => $s.chars().reverse().join(''))"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^V', 'action1')"));
  this->send("echo" CTRL_V);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "ohce"));
  this->send("'" CTRL_A "'\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "'ohce'\n: String = ohce\n" + PROMPT));

  // replace-whole-accept
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$LINE_EDIT.action('action2', 'replace-whole-accept', "
                              "function(s, m) => $s.chars().reverse().join(''))"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^V', 'action2')"));
  this->send("'echo'" CTRL_V);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "'ohce'\n: String = ohce\n" + PROMPT));

  // replace-line //FIXME: multi-line mode
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$LINE_EDIT.action('action3', 'replace-line', "
                              "function(s, m) => $s.chars().reverse().join(''))"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^X', 'action3')"));
  this->send("echo" CTRL_X);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "ohce"));
  this->send("'" CTRL_A "'\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "'ohce'\n: String = ohce\n" + PROMPT));

  // insert
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$LINE_EDIT.action('action4', 'insert', function(s, m) => '/home/home')"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^Y', 'action4')"));
  this->send("echo " CTRL_Y);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "echo /home/home"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("", "/home/home"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, customAction2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  // hist-select
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("var hist = ['echo AA', 'echo BB', 'echo CC', 'echo DD']"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.setHistory($hist)"));

  const char *src =
      "$LINE_EDIT.action('hist-search', 'hist-select', function(s,b) => $b![$s.size()])";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(src));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^R', 'hist-search')"));
  this->send("rr" CTRL_R);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "echo CC"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("", "CC"));

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

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->withTimeout(400, [&] {
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("1", ": Int = 1"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("2", ": Int = 2"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("3", ": Int = 3"));
  });

  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "3"));
  this->send(CTRL_P); // UP
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

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->withTimeout(400, [&] {
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var c = \"$(" HIGHLIGHTER_PATH " --dump)\""));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.setColor($c)"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("1", ": Int = 1"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("2", ": Int = 2"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("3", ": Int = 3"));
  });

  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "3"));
  this->send(CTRL_P); // UP
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "2"));
  this->send(CTRL_N); // DOWN
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "3"));
  this->send(ESC_("\x1b[B")); // ALT-DOWN
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

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->withTimeout(400, [&] {
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("1", ": Int = 1"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("2", ": Int = 2"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("3", ": Int = 3"));
  });

  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "3"));
  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "2"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect("\n: Int = 2\n" + PROMPT));

  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "2"));
  this->send(ESC_("\x1b[A")); // ALT-UP
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "3"));
  this->send(ESC_("[1;3A")); // ALT-UP
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

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->withTimeout(400, [&] {
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("1", ": Int = 1"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("2", ": Int = 2"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("3", ": Int = 3"));
  });

  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "3"));
  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "2"));
  this->send("4");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "24"));
  this->send(ESC_("[1;3B")); // DOWN
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "3"));
  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "24"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect("\n: Int = 24\n" + PROMPT));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, bracketPaste) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->send(ESC_("[200~1234") ESC_("[201~"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "1234\n: Int = 1234\n" + PROMPT));

  // bracket paste with escape
  this->send(ESC_("[200~assert '2\x1b[23m'.size() == 6") ESC_("[201~"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "assert '2'.size() == 6\n" + PROMPT));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

// test recursive api call
TEST_F(InteractiveTest, lineEditor1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  const char *text = "var e = new LineEditor(); $e.setPrompt(function(p)=>{"
                     "  $e.setCompletion(function(m, s) => $s.split(''));"
                     "  $p; "
                     "})";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(text));
  text = "var ex = 34 as Any;"
         "try { $e.readLine(); assert false; } catch e { $ex = $e; }; "
         "assert $ex is InvalidOperationError";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(text));

  text = "$ex = 34; $e.setPrompt(function(p)=>{"
         "  $e.readLine()!; })";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(text));
  text = "try { $e.readLine(); assert false; } catch e { $ex = $e; }; "
         "assert $ex is InvalidOperationError";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(text));

  text = "$ex = 34; $e.setPrompt(function(p)=>{"
         "  $e.setPrompt(function(pp)=> $pp);"
         "  $p; })";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(text));
  text = "try { $e.readLine(); assert false; } catch e { $ex = $e; }; "
         "assert $ex is InvalidOperationError";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(text));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

// test prompt
TEST_F(InteractiveTest, lineEditor2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var e = new LineEditor()"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$e.setPrompt(function(p)=> '%' + $p)"));
  this->sendLine("$e.readLine('> ')");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "$e.readLine('> ')\n%> "));
  this->sendLine("1234");
  ASSERT_NO_FATAL_FAILURE(this->expect("%> 1234\n: String! = 1234\n" + PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$e.setPrompt(function(p) => $p[100])"));
  const char *err = R"([runtime error]
OutOfRangeError: size is 2, but index is 100
    from (stdin):4 'function ()'
    from (stdin):5 '<toplevel>()'
)";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$e.readLine()", "", err));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

// test completion
TEST_F(InteractiveTest, lineEditor3) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  // insert single candidtaes
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$LINE_EDIT.setCompletion(function(s,m) => ['true'])"));
  this->send("()" LEFT "$t\t");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($true)"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(".+: Bool = true.+"));

  // rotate candidates
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$LINE_EDIT.setCompletion(function(s,m) => ['true', 'tee'])"));
  this->send("()" LEFT "$t\t");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($t)"));
  this->send("\t");
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(".+true  tee.+"));
  this->send("\t");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($true)"));
  this->send("\t");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($tee)"));
  this->send("\t\r");
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(".+: Bool = true.+"));
  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}