#include "interactive_base.hpp"

TEST_F(InteractiveTest, ctrld1) {
  this->invoke("--norc");

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  std::string re = "arsh, version .+, build by .+\nCopy.+\narsh-.+";
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

TEST_F(InteractiveTest, cmd_ctrlc1) {
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

TEST_F(InteractiveTest, cmd_ctrlc2) {
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

TEST_F(InteractiveTest, read_ctrlc1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->sendLine("read");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "read\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  this->send(CTRL_C);

  std::string err = format(R"((stdin):1: read: 0: %s
[runtime error]
SystemError: %s
    from (builtin):8 'function _DEF_SIGINT()'
    from (stdin):1 '<toplevel>()'
)",
                           strerror(EINTR), strsignal(SIGINT));

  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 1));
}

TEST_F(InteractiveTest, read_ctrlc2) {
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

TEST_F(InteractiveTest, read_ctrlc3) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->sendLine("for _ in <(cat) {}");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "for _ in <(cat) {}\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  this->send(CTRL_C);
  std::string err = format(R"([runtime error]
SystemError: read failed, caused by `%s'
    from (stdin):1 '<toplevel>()'
)",
                           strerror(EINTR));
  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
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
    ASSERT_NO_FATAL_FAILURE(this->expect("> $False"));
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
    ASSERT_NO_FATAL_FAILURE(this->expect("> $SIGUSR1.\n  name()"));
    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect("> $SIGUSR1.\n  name()\n: String = USR1\n> "));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, tab2) {
  this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile1");

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$LINE_EDIT.setCompletion(function(m, s) => { complete -m $m -q -s -- $s; $COMPREPLY;})"));
  this->send("$RC\t");
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(".+RC_VAR"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(".+: String = rc file.+"));

  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));
  this->send("shct\t");
  ASSERT_NO_FATAL_FAILURE(this->expect("> shctl "));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(""));

  this->send("shctl\t");
  ASSERT_NO_FATAL_FAILURE(this->expect("> shctl "));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(""));

  unsigned int bellCount = 0;
  this->setBellCallback([&bellCount] { bellCount++; });
  this->send("12345\t");
  ASSERT_NO_FATAL_FAILURE(this->expect("> 12345"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("", ": Int = 12345"));
  ASSERT_EQ(1, bellCount);

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, tab3) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->changePrompt(">>> "));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$LINE_EDIT.setCompletion(function(m,s) => new Candidates(['@abc\\ -\\ @.csv']))"));
  this->send("echo @\t");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "echo @abc\\ -\\ @.csv"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("", "@abc - @.csv"));

  // insert unprintable
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$LINE_EDIT.setCompletion(function(m,s) => new Candidates([$s + $'\\t\\x01']))"));
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

TEST_F(InteractiveTest, mlEdit1) { // basic multi-line mode
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

TEST_F(InteractiveTest, mlEdit2) { // edit op in multi-line mode
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));
  this->send("(\r");
  ASSERT_NO_FATAL_FAILURE(this->expect("> (\n  "));
  {
    auto cleanup = this->reuseScreen();

    this->send("@@@" CTRL_A "カタカナ" ESC_("b") "'" CTRL_E CTRL_W ESC_("b") ESC_("d") "'\r");
    ASSERT_NO_FATAL_FAILURE(this->expect("> (\n  'カタカナ@'\n  "));
    this->send("$true" ESC_("b") LEFT "+" ESC_("f") ESC_("f") ")" UP UP "1+" DOWN DOWN CTRL_H "T");
    ASSERT_NO_FATAL_FAILURE(this->expect("> (1+\n  'カタカナ@'\n  +$True)"));
    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(
        this->expect("> (1+\n  'カタカナ@'\n  +$True)\n: String = 1カタカナ@true\n> "));
  }

  this->send("123" ALT_ENTER);
  ASSERT_NO_FATAL_FAILURE(this->expect("> 123\n  "));
  {
    auto cleanup = this->reuseScreen();

    this->send("$true" ESC_("b") CTRL_K "NONE" ESC_("<") CTRL_K);
    ASSERT_NO_FATAL_FAILURE(this->expect("> \n  $NONE"));
    this->send(DOWN CTRL_H);
    ASSERT_NO_FATAL_FAILURE(this->expect("> $NONE"));
    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect("> $NONE\n: Nothing? = (invalid)\n> "));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, mlEdit3) { // clean line
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("echo hello", "hello"));
  {
    auto cleanup = this->reuseScreen();

    this->send("'@@@@\r");
    ASSERT_NO_FATAL_FAILURE(this->expect("> echo hello\nhello\n> '@@@@\n  "));
    this->send(CTRL_L);
    ASSERT_NO_FATAL_FAILURE(this->expect("> '@@@@\n  "));
    this->send("'\r");
    ASSERT_NO_FATAL_FAILURE(this->expect("> '@@@@\n  '\n: String = @@@@\n\n> "));
  }

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

TEST_F(InteractiveTest, killRing1) {
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

TEST_F(InteractiveTest, killRing2) { // resize kill-ring
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var killed : [String]"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$LINE_EDIT.action('action1', 'kill-ring-select', function(q, l) => "
                              "{ $killed = $l!.copy(); '12'; })"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^R', 'action1')"));

  // kill-ring-select (not work)
  this->send(CTRL_R "34\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "34\n: Int = 34\n" + PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $killed.size() == 0"));

  // set size (to 7)
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.config('killring-size', 4)"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$LINE_EDIT.configs()['killring-size'] as Int", ": Int = 7"));

  this->send("12345" CTRL_U "abcde" CTRL_U "ABCDE" CTRL_U "67890" CTRL_U "FGHIJ" CTRL_U);
  this->send(CTRL_R);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "12"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("", ": Int = 12"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$killed.size()", ": Int = 5"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$killed[0]", ": String = 12345"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$killed[1]", ": String = abcde"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$killed[2]", ": String = ABCDE"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$killed[3]", ": String = 67890"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$killed[4]", ": String = FGHIJ"));

  // resize (shrink to 3)
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.config('killring-size', 2)"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$LINE_EDIT.configs()['killring-size'] as Int", ": Int = 3"));

  this->send(CTRL_R);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "12"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("", ": Int = 12"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$killed.size()", ": Int = 3"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$killed[0]", ": String = ABCDE"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$killed[1]", ": String = 67890"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$killed[2]", ": String = FGHIJ"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, killRing3) { // token-aware edit
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.config('lang-extension', $true)"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^W', 'backward-kill-token')"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^[d', 'kill-token')"));

  // // kill and yank
  this->send("/usr/local/bin");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "/usr/local/bin"));
  this->send(CTRL_W);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "/usr/local/"));
  this->send(CTRL_Y);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "/usr/local/bin"));
  this->send(CTRL_A);
  this->send(ESC_("d"));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "/local/bin"));
  this->send(CTRL_Y);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "/usr/local/bin"));
  this->send("/\r");

  const char *err = R"([runtime error]
SystemError: execution error: /usr//local/bin: Permission denied
    from (stdin):4 '<toplevel>()'
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "/usr//local/bin\n" + PROMPT, err));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(126, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, history1) {
#ifdef CODE_COVERAGE
  this->timeoutMSec = 500;
#endif

  this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile3");

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  {
    auto cleanup = this->withTimeout(400);
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("1", ": Int = 1"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("2", ": Int = 2"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("3", ": Int = 3"));
  }

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
  this->timeoutMSec = 500;
#endif

  this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile3");

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  {
    auto cleanup = this->withTimeout(400);
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var c = \"$(" HIGHLIGHTER_PATH " --dump)\""));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.config('color', $c)"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("1", ": Int = 1"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("2", ": Int = 2"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("3", ": Int = 3"));
  }

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
  this->timeoutMSec = 500;
#endif

  this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile3");

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  {
    auto cleanup = this->withTimeout(400);
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("1", ": Int = 1"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("2", ": Int = 2"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("3", ": Int = 3"));
  }

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
  this->timeoutMSec = 500;
#endif

  this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile3");

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  {
    auto cleanup = this->withTimeout(400);
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("1", ": Int = 1"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("2", ": Int = 2"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("3", ": Int = 3"));
  }

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

TEST_F(InteractiveTest, prePrompt) {
  this->invoke("--quiet", "--rcfile", INTERACTIVE_TEST_WORK_DIR "/rcfile4");

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $PRE_PROMPTS.empty()"));

  this->send("var c = 0; $PROMPT_HOOK = function(s) => $c + '> '\r");
  ASSERT_NO_FATAL_FAILURE(
      this->expect(PROMPT + "var c = 0; $PROMPT_HOOK = function(s) => $c + '> '\n0> "));
  this->send("$PRE_PROMPTS.push(function() => { $c++; })\r");
  ASSERT_NO_FATAL_FAILURE(this->expect("0> $PRE_PROMPTS.push(function() => { $c++; })\n1> "));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect("\n2> "));
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
  this->paste("1234");
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "1234\n: Int = 1234\n" + PROMPT));

  // bracket paste with escape
  this->paste("assert '2\x1b[23m'.size() == 6");
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "assert '2^[[23m'.size() == 6\n" + PROMPT));

  // edit
  this->paste("\t@\ta");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "    @   a"));
  this->send(CTRL_A "var a = '" RIGHT RIGHT RIGHT "b" CTRL_E "'\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "var a = '   @   ba'\n" + PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$a.quote()", ": String = $'\\x09'@$'\\x09'ba"));

  // paste with newlines
  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));
  this->paste("echo aaa\recho bbb\necho ccc");
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
  this->paste("\xFF\xFE");
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect("> ��\n" + PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$a!.quote()", ": String = $'\\xff'$'\\xfe'"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, bracketPaste3) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));

  // bracket paste with ctrl-v (insert-keycode)
  this->send(CTRL_V);
  this->paste("1234");
  ASSERT_NO_FATAL_FAILURE(this->expect("> 1234"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect("\n: Int = 1234\n> "));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, bracketPasteError1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));

  // too large paste input
  std::string largeInput;
  largeInput.resize(SYS_LIMIT_READLINE_INPUT_SIZE + 1, '@');

  std::string in = largeInput;
  in += "#";

  std::string err = format(R"([runtime error]
SystemError: readLine failed, caused by `%s'
)",
                           strerror(ENOMEM));

  this->paste(in);
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT, err)); // in scrolling mode

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, bracketPasteError2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));

  // timeout paste
  std::string err = format(R"([fatal] readLine failed, caused by `%s'
)",
                           strerror(ETIME));
  this->send(ESC_("[200~") "hello world!!");                   // unclosed bracket paste
  std::this_thread::sleep_for(std::chrono::milliseconds(300)); // wait read timeout
  ASSERT_NO_FATAL_FAILURE(
      this->waitAndExpect(1, WaitStatus::EXITED, PROMPT + "hello world!!\n", err));
}

TEST_F(InteractiveTest, undo1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  // insert chars
  this->send("@@@@@");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "@@@@@"));
  this->send(CTRL_U);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + ""));
  this->send("111111");
  this->send(CTRL_Z CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "@@@@@"));
  this->send(ESC_("/") ESC_("/"));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "111111"));
  this->send(CTRL_Z);

  // insert space separated chars
  this->send("ls -la");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "ls -la"));
  this->send(CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "ls "));
  this->send(CTRL_H CTRL_H CTRL_H);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->send(CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "ls "));

  this->send(CTRL_C);
  ASSERT_NO_FATAL_FAILURE(this->expect("\n" + PROMPT));

  this->changePrompt("> ");
  this->send("echo hello" ALT_ENTER "ps");
  ASSERT_NO_FATAL_FAILURE(this->expect("> echo hello\n  ps"));
  this->send(CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(this->expect("> echo hello\n  "));
  this->send(CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(this->expect("> echo hello"));
  this->send(ESC_("/") ESC_("/"));
  ASSERT_NO_FATAL_FAILURE(this->expect("> echo hello\n  ps"));

  this->send(CTRL_C);
  ASSERT_NO_FATAL_FAILURE(this->expect("\n" + PROMPT));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, undo2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  // ctrl-v
  this->changePrompt("> ");
  this->send("1234" CTRL_V CTRL_J "@@@" CTRL_V " 111");
  ASSERT_NO_FATAL_FAILURE(this->expect("> 1234\n  @@@ 111"));
  this->send(CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(this->expect("> 1234\n  @@@ "));
  this->send(CTRL_Z CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(this->expect("> 1234\n  "));
  this->send(ESC_("/") ESC_("/"));
  ASSERT_NO_FATAL_FAILURE(this->expect("> 1234\n  @@@ "));

  this->send(CTRL_C);
  ASSERT_NO_FATAL_FAILURE(this->expect("\n" + PROMPT));

  // bracket paste
  this->send("12345");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "12345"));
  this->send(ESC_("[200~assert '2\x1b[23m'.size() == 6") ESC_("[201~"));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "12345assert '2^[[23m'.size() == 6"));
  this->send(CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "12345"));
  this->send(ESC_("/"));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "12345assert '2^[[23m'.size() == 6"));
  this->send(CTRL_C);
  ASSERT_NO_FATAL_FAILURE(this->expect("\n" + PROMPT));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, undoRotate1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  // kill-ring
  this->send("/usr/share");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "/usr/share"));
  this->send(CTRL_W CTRL_W);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "/usr"));
  this->send(CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "/usr/"));
  this->send(ESC_("/"));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "/usr"));
  this->send(CTRL_Z CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "/usr/share"));
  this->send(CTRL_Y);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "/usr/share/"));
  this->send(ESC_("y"));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "/usr/shareshare"));
  this->send(CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "/usr/share"));
  this->send(CTRL_C);
  ASSERT_NO_FATAL_FAILURE(this->expect("\n" + PROMPT));

  // history
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("source " INTERACTIVE_TEST_WORK_DIR "/rcfile3"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("echo hello11", "hello11"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("echo hello22", "hello22"));
  this->send("ls -la");
  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "echo hello22"));
  this->send(UP);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "echo hello11"));
  this->send(CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + ""));
  this->send(CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "ls -la"));

  this->send(CTRL_C);
  ASSERT_NO_FATAL_FAILURE(this->expect("\n" + PROMPT));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, undoRotate2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  // completion
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.setCompletion(function(m,s)=> new "
                                                  "Candidates(@(true tee touch)))"));
  this->changePrompt("> ");
  this->send("()" LEFT "$t");
  ASSERT_NO_FATAL_FAILURE(this->expect("> ($t)"));
  {
    auto cleanup = this->reuseScreen();

    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> ($t)\ntrue    tee     touch   \n"));
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($true)\ntrue    tee     touch   \n"));
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($tee)\ntrue    tee     touch   \n"));
    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($tee)"));
    this->send(CTRL_Z);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($t)"));
    this->send(ESC_("/"));
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($tee)"));
    this->send(CTRL_C);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($tee)\n" + PROMPT));
  }

  // completion with suffix space
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$LINE_EDIT.setCompletion(function(m, s) => { complete -m $m -q -d -- $s; $COMPREPLY;})"));
  this->send("(echo )" LEFT "$SIGU");
  ASSERT_NO_FATAL_FAILURE(this->expect("> (echo $SIGU)"));
  {
    auto cleanup = this->reuseScreen();

    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(
        this->expect("> (echo $SIGUSR)\nSIGUSR1 : Signal    SIGUSR2 : Signal    \n"));
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(
        this->expect("> (echo $SIGUSR1 )\nSIGUSR1 : Signal    SIGUSR2 : Signal    \n"));
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(
        this->expect("> (echo $SIGUSR2 )\nSIGUSR1 : Signal    SIGUSR2 : Signal    \n"));
    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect("> (echo $SIGUSR2 )"));
    this->send(CTRL_Z); // revert candidate insertion(include space),
    ASSERT_NO_FATAL_FAILURE(this->expect("> (echo $SIGUSR)")); // still remain prefix
    this->send(CTRL_Z);                                        // revert auto-inserted prefix
    ASSERT_NO_FATAL_FAILURE(this->expect("> (echo $SIGU)"));
    this->send(CTRL_C);
    ASSERT_NO_FATAL_FAILURE(this->expect("> (echo $SIGU)\n> "));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, undoRotate3) { // ESC
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  // insert common prefix
  this->changePrompt("> ");
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$LINE_EDIT.setCompletion(function(m, s) => { complete -m $m -q -d -- $s; $COMPREPLY;})"));
  this->send("(echo )" LEFT "$SIGU");
  ASSERT_NO_FATAL_FAILURE(this->expect("> (echo $SIGU)"));
  {
    auto cleanup = this->reuseScreen();

    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(
        this->expect("> (echo $SIGUSR)\nSIGUSR1 : Signal    SIGUSR2 : Signal    \n"));
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(
        this->expect("> (echo $SIGUSR1 )\nSIGUSR1 : Signal    SIGUSR2 : Signal    \n"));
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(
        this->expect("> (echo $SIGUSR2 )\nSIGUSR1 : Signal    SIGUSR2 : Signal    \n"));
    this->send("\x1b");
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    ASSERT_NO_FATAL_FAILURE(this->expect("> (echo $SIGU)"));

    this->send(CTRL_C);
    ASSERT_NO_FATAL_FAILURE(this->expect("> (echo $SIGU)\n> "));
  }

  // no insert common prefix
  this->send("$SIGS");
  ASSERT_NO_FATAL_FAILURE(this->expect("> $SIGS"));
  {
    auto cleanup = this->reuseScreen();

    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> $SIGS\nSIGSEGV : Signal    SIGSTOP : Signal    \n"));
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> $SIGSEGV\nSIGSEGV : Signal    SIGSTOP : Signal    \n"));
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> $SIGSTOP\nSIGSEGV : Signal    SIGSTOP : Signal    \n"));
    this->send("\x1b");
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    ASSERT_NO_FATAL_FAILURE(this->expect("> $SIGS"));

    this->send(CTRL_C);
    ASSERT_NO_FATAL_FAILURE(this->expect("> $SIGS\n> "));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}