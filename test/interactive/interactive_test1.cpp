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
  this->invoke("--quiet", "--norc", "-i");

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
  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));
  this->send("$F\t");
  ASSERT_NO_FATAL_FAILURE(this->expect("> $F\nFALSE   False   \n"));

  {
    auto cleanup = this->reuseScreen();
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> $FALSE\nFALSE   False   \n"));
    this->send("\t\r");
    ASSERT_NO_FATAL_FAILURE(this->expect("> $False\n\n"));
    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect("> $False\n: Bool = false\n> "));
  }

  // insert common suffix with multi-line
  this->send("$SIGU.\r");
  ASSERT_NO_FATAL_FAILURE(this->expect("> $SIGU.\n  "));

  {
    auto cleanup = this->reuseScreen();
    this->send("name()");
    ASSERT_NO_FATAL_FAILURE(this->expect("> $SIGU.\n  name()"));
    this->send(UP LEFT);
    ASSERT_NO_FATAL_FAILURE(this->expect("> $SIGU.\n  name()"));
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> $SIGUSR.\nSIGUSR1 SIGUSR2 \n"));
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> $SIGUSR1.\nSIGUSR1 SIGUSR2 \n"));
    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect("> $SIGUSR1.\n  name()\n"));
    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect("> $SIGUSR1.\n  name()\n: String = USR1\n> "));
  }

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

  ASSERT_NO_FATAL_FAILURE(this->changePrompt(">>> "));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$LINE_EDIT.setCompletion(function(m,s) => { ['@abc\\ -\\ @.csv']; })"));
  this->send("echo @\t");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "echo @abc\\ -\\ @.csv"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("", "@abc - @.csv"));

  // insert unprintable
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$LINE_EDIT.setCompletion(function(m,s) => [$s + $'\\t\\x01'])"));
  this->send("var a = 'A\t");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "var a = 'A  ^A"));
  this->send("'\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "var a = 'A  ^A'\n" + PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$a.quote()", ": String = A$'\\x09'$'\\x01'"));

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

TEST_F(InteractiveTest, mlEdit1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));
  this->send("34" ALT_ENTER "45\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(R"(> 34
  45
: Int = 34
: Int = 45
> )"));

  this->send("echo \\\r" CTRL_V "\t45\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(R"(> echo \
    45
45
> )"));

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

  // replace-line
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$LINE_EDIT.action('action3', 'replace-line', "
                              "function(s, m) => $s.chars().reverse().join(''))"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^X', 'action3')"));
  this->send("echo" CTRL_X);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "ohce"));
  this->send("'" CTRL_A "'\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "'ohce'\n: String = ohce\n" + PROMPT));

  // replace-line in multiline mode
  this->changePrompt("> ");
  this->send("'\r");
  ASSERT_NO_FATAL_FAILURE(this->expect("> '\n  "));

  {
    auto cleanup = this->reuseScreen();
    this->send("1234\r");
    ASSERT_NO_FATAL_FAILURE(this->expect("> '\n  1234\n  "));
    this->send("'");
    ASSERT_NO_FATAL_FAILURE(this->expect("> '\n  1234\n  '"));
    this->send(LEFT LEFT CTRL_X);
    ASSERT_NO_FATAL_FAILURE(this->expect("> '\n  4321\n  '"));
    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect("> '\n  4321\n  '\n: String = \n4321\n\n> "));
  }

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
  const char *src =
      "$LINE_EDIT.action('hist-search', 'hist-select', function(s,b) => $b![$s.size()])";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(src));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^R', 'hist-search')"));
  this->send("echo a" CTRL_R);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "echo a")); // no happened (history is empty)
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("", "a"));

  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("var hist = ['echo AA', 'echo BB', 'echo CC', 'echo DD']"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.setHistory($hist)"));
  this->send("rr" CTRL_R);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "echo CC"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("", "CC"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, customActionError1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.action('action1', 'replace-whole', "
                                                  "function(s, m) => exit 199)"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^V', 'action1')"));
  this->send("echo" CTRL_V);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "echo\n"));
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(199, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, customActionError2) {
  this->invoke("--quiet", "--norc", "--trace-exit");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.action('action1', 'replace-whole', "
                                                  "function(s, m) => exit 199)"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^V', 'action1')"));
  this->send("echo" CTRL_V);

  const char *err = R"([runtime error]
Shell Exit: terminated by exit 199
    from (stdin):1 'function ()'
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "echo\n", err));
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(199, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, customActionError3) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.action('action1', 'replace-whole', "
                                                  "function(s, m) => { assert $false; 'hoge'; })"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^V', 'action1')"));
  this->send("echo" CTRL_V);

  const char *err = R"([runtime error]
Assertion Error: `$false'
    from (stdin):1 'function ()'
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "echo\n", err));
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, killRing) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var a : [String]"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$LINE_EDIT.action('action1', 'kill-ring-select', function(q, l) => "
                              "{ $a = $l!.copy(); '12'; })"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^R', 'action1')"));

  // kill-ring-select (not work)
  this->send(CTRL_R "34\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "34\n: Int = 34\n" + PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $a.size() == 0"));

  // kill-line
  this->send(CTRL_Y "81234567" ESC_("<") RIGHT CTRL_K LEFT "-" CTRL_Y "\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "-12345678\n: Int = -12345678\n" + PROMPT));

  // backward-kill-line
  this->send("9999-" LEFT CTRL_U RIGHT CTRL_Y "\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "-9999\n: Int = -9999\n" + PROMPT));

  // kill-word
  this->send("888-8" CTRL_A ESC_("d") ESC_(">") CTRL_Y "\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "-8888\n: Int = -8888\n" + PROMPT));

  // backward-kill-word
  this->send("''ABCDEF" CTRL_W LEFT CTRL_Y "\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "'ABCDEF'\n: String = ABCDEF\n" + PROMPT));

  // kill-ring-select action
  this->send(CTRL_R);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "12"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("", ": Int = 12"));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $a.size() == 4"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $a[0] == '1234567' : $a[0]"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $a[1] == '9999' : $a[1]"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $a[2] == '888' : $a[2]"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $a[3] == 'ABCDEF' : $a[3]"));

  // yank-pop
  this->send("var bb = " ESC_("y"));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "var bb = "));
  this->send(CTRL_Y);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "var bb = ABCDEF"));
  this->send(ESC_("y") ESC_("y") ESC_("y"));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "var bb = 1234567"));
  this->send(ESC_("y") ESC_("y"));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "var bb = 888"));

  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect("\n" + PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$bb", ": Int = 888"));

  this->send("00" CTRL_A CTRL_Y);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "ABCDEF00"));
  this->send(ESC_("y") ESC_("y"));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "999900"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "999900\n: Int = 999900\n" + PROMPT));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

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

TEST_F(InteractiveTest, insert) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->changePrompt(">>> "));
  this->send(CTRL_V CTRL_E);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "^E"));
  this->send(CTRL_V CTRL_I);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "^E  "));
  this->send("'" CTRL_A "var a = '\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "var a = '^E '\n" + PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$a.quote()", ": String = $'\\x05'$'\\x09'"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, bracketPaste1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->changePrompt(">>> "));
  this->send(ESC_("[200~1234") ESC_("[201~"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "1234\n: Int = 1234\n" + PROMPT));

  // bracket paste with escape
  this->send(ESC_("[200~assert '2\x1b[23m'.size() == 6") ESC_("[201~"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "assert '2^[[23m'.size() == 6\n" + PROMPT));

  // edit
  this->send(ESC_("[200~\t@\ta") ESC_("[201~"));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "    @   a"));
  this->send(CTRL_A "var a = '" RIGHT RIGHT RIGHT "b" CTRL_E "'\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "var a = '   @   ba'\n" + PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$a.quote()", ": String = $'\\x09'@$'\\x09'ba"));

  // paste with newlines
  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));
  this->send(ESC_("[200~") "echo aaa\recho bbb\necho ccc" ESC_("[201~"));
  ASSERT_NO_FATAL_FAILURE(this->expect("> echo aaa\n  echo bbb\n  echo ccc"));

  {
    auto cleanup = this->reuseScreen();
    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect("> echo aaa\n  echo bbb\n  echo ccc\naaa\nbbb\nccc\n> "));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, bracketPaste2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  const char *line = "var a = new LineEditor().readLine('> ')";
  this->sendLine(line);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + line + "\n> "));

  // bracket paste with invalid utf8
  this->send(ESC_("[200~\xFF\xFE") ESC_("[201~"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect("> ��\n" + PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$a!.quote()", ": String = $'\\xff'$'\\xfe'"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, lineEditorBase) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  // ctrl-d
  const char *line = "var a = new LineEditor().readLine('> ')";
  this->sendLine(line);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + line + "\n> "));
  this->send(CTRL_D); // no line
  ASSERT_NO_FATAL_FAILURE(this->expect("\n" + PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$a", ": String? = (invalid)"));

  // ctrl-c
  line = "$a = new LineEditor().readLine('> ')";
  this->sendLine(line);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + line + "\n> "));
  this->send(CTRL_C); // no line
  ASSERT_NO_FATAL_FAILURE(this->expect("\n" + PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$a", ": String? = (invalid)"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

// test recursive api call
TEST_F(InteractiveTest, lineEditorRec) {
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
TEST_F(InteractiveTest, lineEditorPrompt) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var e = new LineEditor()"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$e.setPrompt(function(p)=> '%' + $p)"));
  const char *line = "$e.readLine($'>\\x00> ')";
  this->sendLine(line);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + line + "\n%>^@> "));
  this->sendLine("1234");
  ASSERT_NO_FATAL_FAILURE(this->expect("%>^@> 1234\n: String? = 1234\n" + PROMPT));

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

// test history
TEST_F(InteractiveTest, lineEditorHistory) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->changePrompt(">>> "));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.action('action1', 'replace-whole', "
                                                  "function(m,s) => $m + $'\\t\\x00' + $'\\xFE')"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^Y', 'action1')"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var a : [String]"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.setHistory($a)"));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("1234", ": Int = 1234"));
  this->send("##" CTRL_Y);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "##  ^@�"));
  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "1234"));
  this->send(DOWN);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "##  ^@�"));

  this->send(CTRL_C);
  ASSERT_NO_FATAL_FAILURE(this->expect("\n" + PROMPT));
  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

// test completion
TEST_F(InteractiveTest, lineEditorComp) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->changePrompt(">>> "));

  // insert single candidates
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$LINE_EDIT.setCompletion(function(s,m) => ['true'])"));
  this->send("()" LEFT "$t\t");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($true)"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(".+: Bool = true.+"));

  // insert unprintable (invalid, null,,)
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var aa = new LineEditor()"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$aa.setCompletion(function(m,s) => [$s + $'\\t\\x00' + $'\\xFF'])"));
  const char *line = "var ret = $aa.readLine('> ')";
  this->sendLine(line);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + line + "\n> "));
  this->send("12\t");
  ASSERT_NO_FATAL_FAILURE(this->expect("> 12    ^@�"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect("\n" + PROMPT));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$ret!.quote()", ": String = 12$'\\x09'$'\\x00'$'\\xff'"));

  // rotate candidates
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$LINE_EDIT.setCompletion(function(s,m) => ['true', 'tee'])"));
  this->changePrompt("> ");
  this->send("()" LEFT "$t");
  ASSERT_NO_FATAL_FAILURE(this->expect("> ($t)"));

  {
    auto cleanup = this->reuseScreen();

    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> ($t)\ntrue    tee     \n"));

    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($true)\ntrue    tee     \n"));
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($tee)\ntrue    tee     \n"));
    this->send("\t\r");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($true)\n\n"));
    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($true)\n: Bool = true\n" + PROMPT));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

// test tty
TEST_F(InteractiveTest, lineEditorTTY) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  const char *in = "var aa = new LineEditor().readLine('> ') with < /dev/null > /dev/null";
  this->sendLine(in);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + in + "\n> "));
  this->sendLine("1234"); // read from tty (even if stdin/out is redirected)
  ASSERT_NO_FATAL_FAILURE(this->expect("> 1234\n" + PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$aa", ": String? = 1234"));
  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}