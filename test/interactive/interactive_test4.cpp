#include "interactive_base.hpp"

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
ShellExit: terminated by exit 199
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
AssertionFailed: `$false'
    from (stdin):1 'function ()'
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "echo\n", err));
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED));
}

TEST_F(InteractiveTest, lineEditorConfig1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert !($LINE_EDIT.configs()['flow-control'] as Bool)"));
  termios setting{};
  ASSERT_TRUE(tcgetattr(this->handle.pty(), &setting) != -1);
  ASSERT_FALSE(hasFlag(setting.c_iflag, static_cast<tcflag_t>(IXON)));
  ASSERT_TRUE(hasFlag(setting.c_iflag, static_cast<tcflag_t>(IXOFF)));

  // disable flow control
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.config('flow-control', true)"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert ($LINE_EDIT.configs()['flow-control'] as Bool)"));
  ASSERT_TRUE(tcgetattr(this->handle.pty(), &setting) != -1);
  ASSERT_TRUE(hasFlag(setting.c_iflag, static_cast<tcflag_t>(IXON)));
  ASSERT_TRUE(hasFlag(setting.c_iflag, static_cast<tcflag_t>(IXOFF)));

  // re-enable flow control
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.config('flow-control', $false)"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert !($LINE_EDIT.configs()['flow-control'] as Bool)"));
  ASSERT_TRUE(tcgetattr(this->handle.pty(), &setting) != -1);
  ASSERT_FALSE(hasFlag(setting.c_iflag, static_cast<tcflag_t>(IXON)));
  ASSERT_TRUE(hasFlag(setting.c_iflag, static_cast<tcflag_t>(IXOFF)));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, lineEditorConfig2) { // enable/disable bracketed paste mode
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert ($LINE_EDIT.configs()['bracketed-paste'] as Bool)"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.config('bracketed-paste', $false)"));

  unsigned int enableBracketedPasteCount = 0;
  unsigned int disableBracketedPasteCount = 0;
  this->screen.setCSIListener(
      [&enableBracketedPasteCount, &disableBracketedPasteCount](StringRef seq) {
        if (seq == "\x1b[?2004h") {
          enableBracketedPasteCount++;
        } else if (seq == "\x1b[?2004l") {
          disableBracketedPasteCount++;
        }
      });

  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert !($LINE_EDIT.configs()['bracketed-paste'] as Bool)"));
  ASSERT_EQ(0, enableBracketedPasteCount);
  ASSERT_EQ(0, disableBracketedPasteCount);

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.config('bracketed-paste', $true)"));
  ASSERT_EQ(1, enableBracketedPasteCount);
  ASSERT_EQ(0, disableBracketedPasteCount);

  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert ($LINE_EDIT.configs()['bracketed-paste'] as Bool)"));
  ASSERT_EQ(2, enableBracketedPasteCount);
  ASSERT_EQ(1, disableBracketedPasteCount);

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, lineEditorEAW) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  // auto-detect width
  this->screen.setEAW(AmbiguousCharWidth::FULL);
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert $LINE_EDIT.configs()['eaw'] as Int == 0"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $EAW == 2"));
  this->screen.setEAW(AmbiguousCharWidth::HALF);
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert $LINE_EDIT.configs()['eaw'] as Int == 0"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $EAW == 1"));

  // force set width (EAW is 2, even if the actual width is 1)
  this->screen.setEAW(AmbiguousCharWidth::HALF);
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert $LINE_EDIT.configs()['eaw'] as Int == 0"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.config('eaw', 2)"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $EAW == 2"));

  // force set width (EAW is 1, even if the actual width is 1)
  this->screen.setEAW(AmbiguousCharWidth::FULL);
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("assert $LINE_EDIT.configs()['eaw'] as Int == 2"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.config('eaw', 1)"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $EAW == 1"));

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

// save/restore some global variables
TEST_F(InteractiveTest, lineEditorGlobal) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $? == 0"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $IFS == $' \\t\\n'"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $REPLY == ''"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $reply.empty()"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $PIPESTATUS.empty()"));

  // update globals
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$REPLY = $OSTYPE"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$reply['mactype'] = $MACHTYPE"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("exit 56 | exit 99"));

  // set prompt callback
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.setPrompt(function(p) => {"
                                                  "  $? = 222; $IFS='111'; $REPLY='@'; "
                                                  "  printf -v var -- 'hey';"
                                                  "  exit 123 | exit 67 | exit 5;"
                                                  "  $p;"
                                                  "})"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(""));

  // preserve old value
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $? == 99"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $REPLY == $OSTYPE"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $reply.size() == 1"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $reply['mactype'] == $MACHTYPE"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $IFS == $' \\t\\n'"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $PIPESTATUS.size() == 2"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $PIPESTATUS[0] == 56"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $PIPESTATUS[1] == 99"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(99, WaitStatus::EXITED, "\n"));
}

// test recursive api call
TEST_F(InteractiveTest, lineEditorRec) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  const char *text = "var e = new LineEditor(); $e.setPrompt(function(p)=>{"
                     "  $e.setCompletion(function(m, s) => new Candidates($s.split('')));"
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

  text = "$ex = 34; $e.setPrompt(function(p)=>{"
         "  $e.config('color', '');"
         "  $p; })";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(text));
  text = "try { $e.readLine(); assert false; } catch e { $ex = $e; }; "
         "assert $ex is InvalidOperationError";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(text));

  // recursive call readLine from other instance
  text = "$ex = 34; $e.setPrompt(function(p)=>{"
         "  $LINE_EDIT.readLine();"
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

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$a.add('aaaa').add('bbbb')", ": [String] = [1234, $a.add('aaaa').add('bbbb'), aaaa, bbbb]"));

  this->send(ALT_ENTER);
  ASSERT_NO_FATAL_FAILURE(this->expect(">>> \n    "));
  {
    auto cleanup = this->reuseScreen();

    this->send(DOWN DOWN "@@@@" ESC_("[1;3A") ESC_("[1;3A")); // ALT_UP ALT_UP
    ASSERT_NO_FATAL_FAILURE(this->expect(">>> \n    aaaa"));
    this->send(ESC_("[1;3B")); // ALT_DOWN
    ASSERT_NO_FATAL_FAILURE(this->expect(">>> \n    bbbb"));
    this->send("'" UP "'" UP);
    ASSERT_NO_FATAL_FAILURE(this->expect(">>> '\n    bbbb'"));
    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect(">>> '\n    bbbb'\n: String = \nbbbb\n>>> "));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

// test history modification
TEST_F(InteractiveTest, lineEditorInvalid) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var hist : [String]"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.setHistory($hist)"));

  // iterator invalidation
  const char *err = R"([runtime error]
InvalidOperationError: cannot modify array object during iteration
    from (stdin):3 '<toplevel>()'
)";
  this->sendLine("for a in $hist { $LINE_EDIT.readLine(); }");
  ASSERT_NO_FATAL_FAILURE(
      this->expect(PROMPT + "for a in $hist { $LINE_EDIT.readLine(); }\n\n" + PROMPT, err));

  // modify history
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$LINE_EDIT.setCompletion(function(m, s) => new Candidates($hist.add($s)))"));
  this->sendLine("$LINE_EDIT.readLine('> ')");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "$LINE_EDIT.readLine('> ')\n> "));
  this->send("$T\t");
  err = R"([runtime error]
InvalidOperationError: cannot modify array object during line editing
    from (stdin):4 'function ()'
    from (stdin):5 '<toplevel>()'
)";
  ASSERT_NO_FATAL_FAILURE(this->expect("> $T\n" + PROMPT, err));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

// test completion
TEST_F(InteractiveTest, lineEditorComp1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->changePrompt(">>> "));

  // no comp
  this->send("123\t");
  ASSERT_NO_FATAL_FAILURE(this->expect(">>> 123"));
  this->send(CTRL_C);
  ASSERT_NO_FATAL_FAILURE(this->expect("\n>>> "));

  // insert single candidates with a prefix
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$LINE_EDIT.setCompletion(function(s,m) => new Candidates(['true']))"));
  this->send("()" LEFT "$t\t");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($true)"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(">>> ($true)\n: Bool = true\n>>> "));

  // insert single candidates without a prefix
  this->send("()" LEFT "\t");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "(true)"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(">>> (true)\n>>> "));

  // insert unprintable (invalid, null,,)
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var aa = new LineEditor()"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$aa.setCompletion(function(m,s) => new Candidates([$s + $'\\t\\x00' + $'\\xFF']))"));
  const char *line = "var ret = $aa.readLine('> ')";
  this->sendLine(line);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + line + "\n> "));
  this->send("12\t");
  ASSERT_NO_FATAL_FAILURE(this->expect("> 12    ^@�"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect("\n" + PROMPT));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$ret!.quote()", ": String = 12$'\\x09'$'\\x00'$'\\xff'"));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

// completion candidate rotation
TEST_F(InteractiveTest, lineEditorComp2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->changePrompt(">>> "));

  // rotate candidates
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$LINE_EDIT.setCompletion(function(s,m) => new Candidates( @(true tee touch)) )"));
  this->changePrompt("> ");
  this->send("()" LEFT "$t");
  ASSERT_NO_FATAL_FAILURE(this->expect("> ($t)"));
  {
    auto cleanup = this->reuseScreen();

    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> ($t)\ntrue    tee     touch   \n"));

    this->sendCSIu(57344, SHIFT); // send an unrecognized escape sequence (should ignore)
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($true)\ntrue    tee     touch   \n"));
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($tee)\ntrue    tee     touch   \n"));
    this->send(UP);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($true)\ntrue    tee     touch   \n"));
    this->send("\x1b[111111111e"); // send an unrecognized escape sequence (should ignore)
    this->send(DOWN RIGHT);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($touch)\ntrue    tee     touch   \n"));
    this->send(LEFT LEFT);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($true)\ntrue    tee     touch   \n"));

    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($true)"));
    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($true)\n: Bool = true\n" + PROMPT));
  }

  // cancel rotate
  this->send("''" LEFT "$t");
  ASSERT_NO_FATAL_FAILURE(this->expect("> '$t'"));
  {
    auto cleanup = this->reuseScreen();

    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> '$t'\ntrue    tee     touch   \n"));
    this->send("@"); // cancel and insert
    ASSERT_NO_FATAL_FAILURE(this->expect("> '$t@'"));

    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> '$t@'\ntrue    tee     touch   \n"));
    this->send(CTRL_W); // cancel and edit
    ASSERT_NO_FATAL_FAILURE(this->expect("> '$t'"));

    this->send("%\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> '$t%'\ntrue    tee     touch   \n"));
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> '$t%true'\ntrue    tee     touch   \n"));
    this->send(CTRL_C); // cancel comp
    ASSERT_NO_FATAL_FAILURE(this->expect("> '$t%true'\n> "));
  }

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(""));
  this->send(";t");
  ASSERT_NO_FATAL_FAILURE(this->expect("> ;t"));
  {
    auto cleanup = this->reuseScreen();

    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> ;t\ntrue    tee     touch   \n"));
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> ;true\ntrue    tee     touch   \n"));
    this->send(UP UP);
    ASSERT_NO_FATAL_FAILURE(this->expect("> ;tee\ntrue    tee     touch   \n"));
    this->send("2"); // cancel and insert
    ASSERT_NO_FATAL_FAILURE(this->expect("> ;tee2"));

    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> ;tee2\ntrue    tee     touch   \n"));
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> ;tee2true\ntrue    tee     touch   \n"));
    this->send(UP);
    ASSERT_NO_FATAL_FAILURE(this->expect("> ;tee2touch\ntrue    tee     touch   \n"));
    this->send(CTRL_W); // cancel and edit
    ASSERT_NO_FATAL_FAILURE(this->expect("> ;"));
    this->send(CTRL_W);
    ASSERT_NO_FATAL_FAILURE(this->expect("> "));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

// completion candidate rotation without a common prefix
TEST_F(InteractiveTest, lineEditorComp3) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));

  // rotate candidates
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$LINE_EDIT.setCompletion(function(s,m) => new Candidates( @(aaaa bbb ccc)) )"));
  this->send(";");
  ASSERT_NO_FATAL_FAILURE(this->expect("> ;"));
  {
    auto cleanup = this->reuseScreen();

    this->send(ALT_ENTER);
    ASSERT_NO_FATAL_FAILURE(this->expect("> ;\n  "));
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> ;\n  \naaaa    bbb     ccc     \n"));

    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> ;\n  aaaa\naaaa    bbb     ccc     \n"));
    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect("> ;\n  aaaa"));
    this->send("\r");
    const char *err = R"([runtime error]
SystemError: execution error: aaaa: command not found
    from (stdin):4 '<toplevel>()'
)";
    ASSERT_NO_FATAL_FAILURE(this->expect("> ;\n  aaaa\n> ", err));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(127, WaitStatus::EXITED, "\n"));
}

// insert a common prefix with multi-bytes char
TEST_F(InteractiveTest, lineEditorComp4) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));

  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$LINE_EDIT.setCompletion(function(s,m) => new "
                              "Candidates(@(20230907_バイタル 20230907_ラベル)))"));
  this->send("echo 20");
  ASSERT_NO_FATAL_FAILURE(this->expect("> echo 20"));
  {
    auto cleanup = this->reuseScreen();
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(
        this->expect("> echo 20230907_\n20230907_バイタル   20230907_ラベル     \n"));

    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(
        this->expect("> echo 20230907_バイタル\n20230907_バイタル   20230907_ラベル     \n"));
    this->send(CTRL_C);
    ASSERT_NO_FATAL_FAILURE(this->expect("> echo 20230907_バイタル\n> "));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

// test suffix insertion of `(`, `()`
TEST_F(InteractiveTest, lineEditorCompSuffix) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$LINE_EDIT.setCompletion(function(m, s) => { complete -m $m -q -s -d -- $s; $COMPREPLY;})"));
  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));

  this->send("1234.\t");
  ASSERT_NO_FATAL_FAILURE(this->expect("> 1234.\n"
                                       "abs (): Int for Int                 "
                                       "compare (target: Int): Int for Int  "
                                       "equals (target: Int): Bool for Int  "
                                       "toFloat (): Float for Int           \n"));

  {
    auto cleanup = this->reuseScreen();
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> 1234.abs()\n"
                                         "abs (): Int for Int                 "
                                         "compare (target: Int): Int for Int  "
                                         "equals (target: Int): Bool for Int  "
                                         "toFloat (): Float for Int           \n"));

    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> 1234.compare(\n"
                                         "abs (): Int for Int                 "
                                         "compare (target: Int): Int for Int  "
                                         "equals (target: Int): Bool for Int  "
                                         "toFloat (): Float for Int           \n"));

    this->send(CTRL_C);
    ASSERT_NO_FATAL_FAILURE(this->expect("> 1234.compare(\n> "));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, lineEditorCompError) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));

  // insert large item
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$LINE_EDIT.setCompletion(function(m,s)=> new Candidates([$(seq 1 9999).join(' ')]))"));

  std::string err = format(R"([runtime error]
SystemError: readLine failed, caused by `%s'
)",
                           strerror(ENOMEM));

  this->send("\t");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "\n" + PROMPT, err));

  // rotate and insert large item
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$LINE_EDIT.setCompletion(function(m,s)=> new Candidates(['2', $(seq 1 9999).join(' ')]))"));
  this->send("12");
  ASSERT_NO_FATAL_FAILURE(this->expect("> 12"));

  {
    auto cleanup = this->reuseScreen();
    auto cleanup2 = this->withTimeout(300);
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(
        this->expect("> 12\n"
                     "2                                                                 "
                     "                                                                           "
                     "                                                       \n"
                     "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 "
                     "26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 "
                     "51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68 6\n"));

    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(
        this->expect("> 122\n"
                     "2                                                                 "
                     "                                                                           "
                     "                                                       \n"
                     "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 "
                     "26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 "
                     "51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68 6\n"));

    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> 12\n> ", err));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
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

TEST_F(InteractiveTest, lineEditorResize1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));

  this->send("abcdefghijklmnopqrstuvwxyz123456789");
  ASSERT_NO_FATAL_FAILURE(this->expect("> abcdefghijklmnopqrstuvwxyz123456789"));
  {
    auto cleanup = this->reuseScreen();
    this->changeWinSize({.rows = 20, .cols = 10});
    ASSERT_NO_FATAL_FAILURE(this->expect("> abcdefgh\n"
                                         "ijklmnopqr\n"
                                         "stuvwxyz12\n"
                                         "3456789"));
    this->changeWinSize({.rows = 20, .cols = 20});
    ASSERT_NO_FATAL_FAILURE(this->expect("> abcdefghijklmnopqr\n"
                                         "stuvwxyz123456789"));
    this->send(CTRL_V);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    this->changeWinSize({.rows = 20, .cols = 30});
    ASSERT_NO_FATAL_FAILURE(this->expect("> abcdefghijklmnopqrstuvwxyz12\n3456789"));
    this->send(CTRL_A);
    ASSERT_NO_FATAL_FAILURE(this->expect("> abcdefghijklmnopqrstuvwxyz12\n3456789^A"));

    this->send(CTRL_C);
    ASSERT_NO_FATAL_FAILURE(this->expect("> abcdefghijklmnopqrstuvwxyz12\n3456789^A\n> "));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, lineEditorResize2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));

  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("var can = $(for l in $(seq 1 10) { printf '%0*d\\n' 5 $l; })"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$LINE_EDIT.setCompletion(function(s,m) => new Candidates($can))"));
  this->changeWinSize({.rows = 80, .cols = 20});

  this->send("0");
  ASSERT_NO_FATAL_FAILURE(this->expect("> 0"));
  {
    auto cleanup = this->reuseScreen();
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> 000\n"
                                         "00001   00006   \n"
                                         "00002   00007   \n"
                                         "00003   00008   \n"
                                         "00004   00009   \n"
                                         "00005   00010   \n"));

    this->changeWinSize({.rows = 80, .cols = 31});
    ASSERT_NO_FATAL_FAILURE(this->expect("> 000\n"
                                         "00001   00005   00009   \n"
                                         "00002   00006   00010   \n"
                                         "00003   00007   \n"
                                         "00004   00008   \n"));

    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> 00001\n"
                                         "00001   00005   00009   \n"
                                         "00002   00006   00010   \n"
                                         "00003   00007   \n"
                                         "00004   00008   \n"));

    this->changeWinSize({.rows = 80, .cols = 35});
    ASSERT_NO_FATAL_FAILURE(this->expect("> 00001\n"
                                         "00001   00004   00007   00010   \n"
                                         "00002   00005   00008   \n"
                                         "00003   00006   00009   \n"));

    this->changeWinSize({.rows = 2, .cols = 35});
    ASSERT_NO_FATAL_FAILURE(this->expect("> 00001\n"));

    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> 00002\n"));

    this->changeWinSize({.rows = 4, .cols = 35});
    ASSERT_NO_FATAL_FAILURE(this->expect("> 00002\n"
                                         "00002   00005   00008   \n"
                                         "rows 2-2/3\n"));

    this->send(CTRL_C);
    ASSERT_NO_FATAL_FAILURE(this->expect("> 00002\n> "));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, lineEditorScroll) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));

  this->paste("11111\n22222\n33333\n44444\n55555\n66666\n77777\n88888\n99999");
  ASSERT_NO_FATAL_FAILURE(this->expect("> 11111\n  22222\n  33333\n  44444\n  55555\n"
                                       "  66666\n  77777\n  88888\n  99999"));
  {
    auto cleanup = this->reuseScreen();
    this->changeWinSize({.rows = 5, .cols = 40});

    ASSERT_NO_FATAL_FAILURE(this->expect("  55555\n  66666\n  77777\n  88888\n  99999"));

    this->send(ESC_("<"));
    ASSERT_NO_FATAL_FAILURE(this->expect("> 11111\n  22222\n  33333\n  44444\n  55555"));

    this->send(CTRL_C);
    ASSERT_NO_FATAL_FAILURE(this->expect("> "));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, lineEditorInterrupt1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));

  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$SIGHUP.trap(function(s) => { echo ${$s.name()}; exit 200; })",
                              ": (Signal) -> Void = function(SIG_DFL)"));

  this->handle.kill(SIGHUP);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(200, WaitStatus::EXITED, "HUP\n\n"));
}

TEST_F(InteractiveTest, lineEditorInterrupt2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));

  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$SIGWINCH.trap(function(s) => { echo ${$s.name()}; exit 210; })",
                              ": (Signal) -> Void = function(SIG_DFL)"));

  this->send(CTRL_V);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  this->handle.kill(SIGWINCH);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(210, WaitStatus::EXITED, "WINCH\n\n"));
}

TEST_F(InteractiveTest, lineEditorInterrupt3) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$LINE_EDIT.setCompletion(function(m,s) => { new Candidates(['AAA', 'AAB']); })"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$SIGWINCH.trap(function(s) => { echo ${$s.name()}; exit 210; })",
                              ": (Signal) -> Void = function(SIG_DFL)"));

  this->send("A");
  ASSERT_NO_FATAL_FAILURE(this->expect("> A"));
  {
    auto cleanup = this->reuseScreen();
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> AA\nAAA AAB \n"));
    this->handle.kill(SIGWINCH);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(210, WaitStatus::EXITED, "WINCH\n> AA\n"));
}

TEST_F(InteractiveTest, lineEditorInterrupt4) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "$LINE_EDIT.setCompletion(function(m,s) => { new Candidates(['AAA', 'AAB']); })"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$SIGWINCH.trap(function(s) => { echo ${$s.name()}; exit 210; })",
                              ": (Signal) -> Void = function(SIG_DFL)"));

  this->send("A");
  ASSERT_NO_FATAL_FAILURE(this->expect("> A"));
  {
    auto cleanup = this->reuseScreen();
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> AA\nAAA AAB \n"));
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> AAA\nAAA AAB \n"));
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> AAB\nAAA AAB \n"));
    this->handle.kill(SIGWINCH);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(210, WaitStatus::EXITED, "WINCH\n> AAB\n"));
}

TEST_F(InteractiveTest, lineEditorGetkey) {
  this->invoke("--quiet", "--norc");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  // normal key
  this->sendLine("$LINE_EDIT.getkey()");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "$LINE_EDIT.getkey()\n"));
  this->send("Q");
  ASSERT_NO_FATAL_FAILURE(this->expect(": (String, String) = (Q, )\n" + PROMPT));

  // recognized key event
  this->sendLine("$LINE_EDIT.getkey()");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "$LINE_EDIT.getkey()\n"));
  this->send(CTRL_A);
  ASSERT_NO_FATAL_FAILURE(this->expect(": (String, String) = (^A, ctrl+a)\n" + PROMPT));

  // unrecognized key event
  this->sendLine("$LINE_EDIT.getkey()");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "$LINE_EDIT.getkey()\n"));
  this->send(ESC_("[22222~"));
  ASSERT_NO_FATAL_FAILURE(this->expect(": (String, String) = (^[[22222~, )\n" + PROMPT));

  // bracketed paste (always ignore the following bytes of '^[[200~' sequence)
  this->sendLine("$LINE_EDIT.getkey()");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "$LINE_EDIT.getkey()\n"));
  this->paste("hello world !!!\n");
  ASSERT_NO_FATAL_FAILURE(this->expect(": (String, String) = (^[[200~, bracket_start)\n" + PROMPT));

  // error
  this->sendLine("$LINE_EDIT.getkey()");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "$LINE_EDIT.getkey()\n"));
  this->changeWinSize({.rows = 100, .cols = 200});
  std::string err = format(R"([runtime error]
SystemError: cannot read keycode, caused by `%s'
    from (stdin):5 '<toplevel>()'
)",
                           strerror(EINTR));
  ASSERT_NO_FATAL_FAILURE(this->expect("" + PROMPT, err));

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, kittyKeyboardProtocol) {
  this->invoke("--quiet", "--norc");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  unsigned int enableKittyCount = 0;
  unsigned int disableKittyCount = 0;
  this->screen.setCSIListener([&enableKittyCount, &disableKittyCount](StringRef seq) {
    if (seq == "\x1b[=5u") {
      enableKittyCount++;
    } else if (seq == "\x1b[=0u") {
      disableKittyCount++;
    }
  });

  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));
  ASSERT_EQ(0, enableKittyCount);
  ASSERT_EQ(0, disableKittyCount);
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$LINE_EDIT.config('keyboard-protocol', 'kitty')"));
  ASSERT_EQ(1, enableKittyCount);
  ASSERT_EQ(0, disableKittyCount);

  this->send("echo");
  ASSERT_NO_FATAL_FAILURE(this->expect("> echo"));
  {
    auto cleanup = this->reuseScreen();
    this->sendCSIu(' ');
    this->sendCSIu('a');
    this->sendCSIu('b');
    this->sendCSIu(57355); // unrecognized escape sequence (should ignore)
    this->sendCSIu('c');
    this->sendCSIu('a', SHIFT);
    ASSERT_NO_FATAL_FAILURE(this->expect("> echo abcA"));
    this->sendCSIu(13); // enter
    ASSERT_NO_FATAL_FAILURE(this->expect("> echo abcA\nabcA\n> "));
  }

  ASSERT_EQ(2, enableKittyCount);
  ASSERT_EQ(1, disableKittyCount);
  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

static void codes(std::vector<std::string> &) {}

template <typename... T>
static void codes(std::vector<std::string> &ret, StringRef first, T &&...remain) {
  ret.push_back(first.toString());
  codes(ret, std::forward<T>(remain)...);
}

template <typename... T>
static std::vector<std::string> codes(StringRef first, T &&...remain) {
  std::vector<std::string> ret;
  codes(ret, first, std::forward<T>(remain)...);
  return ret;
}

TEST_F(InteractiveTest, modifyOtherKeys) {
  constexpr StringRef CSI_ENABLE_KITTY = "\x1b[=5u";
  constexpr StringRef CSI_DISABLE_KITTY = "\x1b[=0u";
  constexpr StringRef CSI_ENABLE_XTERM = "\x1b[>4;1m";
  constexpr StringRef CSI_DISABLE_XTERM = "\x1b[>4;0m";

  this->invoke("--quiet", "--norc");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  std::vector<std::string> csiList;
  StrRefSet allowSet = {CSI_ENABLE_KITTY, CSI_DISABLE_KITTY, CSI_ENABLE_XTERM, CSI_DISABLE_XTERM};
  this->screen.setCSIListener([&csiList, &allowSet](StringRef seq) {
    if (allowSet.find(seq) != allowSet.end()) {
      csiList.push_back(seq.toString());
    }
  });

  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));
  ASSERT_EQ(0, csiList.size());
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$LINE_EDIT.config('keyboard-protocol', 'xterm')"));
  ASSERT_EQ(codes(CSI_ENABLE_XTERM), csiList);
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
      "assert $LINE_EDIT.configs()['keyboard-protocol'] as String == 'xterm'"));
  ASSERT_EQ(codes(CSI_ENABLE_XTERM, CSI_DISABLE_XTERM, CSI_ENABLE_XTERM), csiList);
  csiList.clear();
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$LINE_EDIT.config('keyboard-protocol', 'kitty')"));
  ASSERT_EQ(codes(CSI_DISABLE_XTERM, CSI_ENABLE_KITTY), csiList);
  csiList.clear();
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$LINE_EDIT.config('keyboard-protocol', 'xterm')"));
  ASSERT_EQ(codes(CSI_DISABLE_KITTY, CSI_ENABLE_XTERM), csiList);

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
  ASSERT_EQ(codes(CSI_DISABLE_KITTY, CSI_ENABLE_XTERM, CSI_DISABLE_XTERM), csiList);
}

TEST_F(InteractiveTest, tokenEdit1) { // lang-extension=false
  this->invoke("--quiet", "--norc");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.config('lang-extension', $false)"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^[b', 'backward-token')"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^[f', 'forward-token')"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^W', 'backward-kill-token')"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^[d', 'kill-token')"));

  // if lang-extension is disabled, fallback to word op
  this->send("aaa @@@bbb");
  ASSERT_NO_FATAL_FAILURE(this->expect("> aaa @@@bbb"));
  {
    auto cleanup = this->reuseScreen();
    this->send(ESC_("b")); // move left
    this->send("^");
    ASSERT_NO_FATAL_FAILURE(this->expect("> aaa @@@^bbb"));
    this->send(CTRL_W); // remove left
    ASSERT_NO_FATAL_FAILURE(this->expect("> aaa @@@bbb"));
    this->send(ESC_("b")); // move left
    this->send("^");
    ASSERT_NO_FATAL_FAILURE(this->expect("> aaa @@^@bbb"));
    this->send(ESC_("f")); // move right
    this->send(ESC_("d")); // remove right
    ASSERT_NO_FATAL_FAILURE(this->expect("> aaa @@^@"));

    this->send(CTRL_C);
    ASSERT_NO_FATAL_FAILURE(this->expect("> aaa @@^@\n> "));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, tokenEdit2) { // lang-extension=true
  this->invoke("--quiet", "--norc");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.config('lang-extension', $true)"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^[b', 'backward-token')"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^[f', 'forward-token')"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^W', 'backward-kill-token')"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^[d', 'kill-token')"));

  this->send("aaa @@@bbb");
  ASSERT_NO_FATAL_FAILURE(this->expect("> aaa @@@bbb"));
  {
    auto cleanup = this->reuseScreen();
    this->send(ESC_("b")); // move left
    this->send("^");
    ASSERT_NO_FATAL_FAILURE(this->expect("> aaa ^@@@bbb"));
    this->send(CTRL_W); // remove left
    ASSERT_NO_FATAL_FAILURE(this->expect("> aaa @@@bbb"));
    this->send(ESC_("b")); // move left
    this->send("^");
    ASSERT_NO_FATAL_FAILURE(this->expect("> ^aaa @@@bbb"));
    this->send(ESC_("f")); // move right
    this->send(ESC_("d")); // remove right
    ASSERT_NO_FATAL_FAILURE(this->expect("> ^aaa"));

    this->send(CTRL_C);
    ASSERT_NO_FATAL_FAILURE(this->expect("> ^aaa\n> "));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, tokenEdit3) { // invalid syntax
  this->invoke("--quiet", "--norc");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.config('lang-extension', $true)"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^[b', 'backward-token')"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^[f', 'forward-token')"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^W', 'backward-kill-token')"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^[d', 'kill-token')"));

  this->send("var $ABC");
  ASSERT_NO_FATAL_FAILURE(this->expect("> var $ABC"));
  {
    auto cleanup = this->reuseScreen();
    this->send(ESC_("b")); // move left
    this->send("q");
    ASSERT_NO_FATAL_FAILURE(this->expect("> var $qABC"));
    this->send(CTRL_W); // remove left
    ASSERT_NO_FATAL_FAILURE(this->expect("> var $ABC"));
    this->send(ESC_("b")); // move left
    this->send(ESC_("d")); // remove right
    ASSERT_NO_FATAL_FAILURE(this->expect("> var ABC"));
    this->send("$");
    ASSERT_NO_FATAL_FAILURE(this->expect("> var $ABC"));
    this->send(ESC_("f")); // move right
    ASSERT_NO_FATAL_FAILURE(this->expect("> var $ABC"));

    this->send(CTRL_C);
    ASSERT_NO_FATAL_FAILURE(this->expect("> var $ABC\n> "));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, tokenEdit4) { // edit with comment
  this->invoke("--quiet", "--norc");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->changePrompt("> "));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.config('lang-extension', $true)"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^[b', 'backward-token')"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^[f', 'forward-token')"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^W', 'backward-kill-token')"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$LINE_EDIT.bind('^[d', 'kill-token')"));

  this->send("  #  /usr/local-user/bin");
  ASSERT_NO_FATAL_FAILURE(this->expect(">   #  /usr/local-user/bin"));
  {
    auto cleanup = this->reuseScreen();
    this->send(ESC_("b") ESC_("b") ESC_("b")); // move left
    this->send("q");
    ASSERT_NO_FATAL_FAILURE(this->expect(">   #  /usr/local-quser/bin"));
    this->send(CTRL_W); // remove left
    ASSERT_NO_FATAL_FAILURE(this->expect(">   #  /usr/local-user/bin"));
    this->send(ESC_("b")); // move left
    this->send(ESC_("d")); // remove right
    ASSERT_NO_FATAL_FAILURE(this->expect(">   #  /usr/local"));
    this->send(ESC_("b") ESC_("b")); // move left
    this->send("/");
    ASSERT_NO_FATAL_FAILURE(this->expect(">   #  /usr//local"));
    this->send(ESC_("f")); // move right
    this->send("/");
    ASSERT_NO_FATAL_FAILURE(this->expect(">   #  /usr//local/"));

    this->send(CTRL_C);
    ASSERT_NO_FATAL_FAILURE(this->expect(">   #  /usr//local/\n> "));
  }

  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED, "\n"));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}