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

// test completion
TEST_F(InteractiveTest, lineEditorComp) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->changePrompt(">>> "));

  // no comp
  this->send("123\t");
  ASSERT_NO_FATAL_FAILURE(this->expect(">>> 123"));
  this->send(CTRL_C);
  ASSERT_NO_FATAL_FAILURE(this->expect("\n>>> "));

  // insert single candidates with prefix
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$LINE_EDIT.setCompletion(function(s,m) => ['true'])"));
  this->send("()" LEFT "$t\t");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($true)"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(">>> ($true)\n: Bool = true\n>>> "));

  // insert single candidates without prefix
  this->send("()" LEFT "\t");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "(true)"));
  this->send("\r");
  ASSERT_NO_FATAL_FAILURE(this->expect(">>> (true)\n>>> "));

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
      this->sendLineAndExpect("$LINE_EDIT.setCompletion(function(s,m) => @(true tee touch))"));
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
    this->send(UP);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($true)\ntrue    tee     touch   \n"));
    this->send(DOWN RIGHT);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($touch)\ntrue    tee     touch   \n"));
    this->send(LEFT LEFT);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($true)\ntrue    tee     touch   \n"));

    this->send("\r");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "($true)\n\n"));
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
    ASSERT_NO_FATAL_FAILURE(this->expect("> '$t@'\n\n"));

    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> '$t@'\ntrue    tee     touch   \n"));
    this->send(CTRL_W); // cancel and edit
    ASSERT_NO_FATAL_FAILURE(this->expect("> '$t'\n\n"));

    this->send("%\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> '$t%'\ntrue    tee     touch   \n"));
    this->send("\t");
    ASSERT_NO_FATAL_FAILURE(this->expect("> '$t%true'\ntrue    tee     touch   \n"));
    this->send(CTRL_C); // cancel comp
    ASSERT_NO_FATAL_FAILURE(this->expect("> '$t%true'\n> \n"));
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
    ASSERT_NO_FATAL_FAILURE(this->expect("> ;tee2\n\n"));

    this->send("\t\t" UP);
    ASSERT_NO_FATAL_FAILURE(this->expect("> ;tee2\ntrue    tee     touch   \n"));
    this->send(CTRL_W); // cancel and edit
    ASSERT_NO_FATAL_FAILURE(this->expect("> ;\n\n"));
    this->send(CTRL_W);
    ASSERT_NO_FATAL_FAILURE(this->expect("> \n\n"));
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