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

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
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

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
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

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
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

TEST_F(InteractiveTest, lineEditor) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  const char *text = "var e = new LineEditor(); $e.setPrompt(function(p)=>{"
                     "  $e.setCompletion(function(m, s) => $s.split(''));"
                     "  $p; "
                     "})";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(text));
  text = "var ex = 34 as Any;"
         "try { $e.read(); assert false; } catch e { $ex = $e; }; "
         "assert $ex is InvalidOperationError";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(text));

  text = "$ex = 34; $e.setPrompt(function(p)=>{"
         "  $e.read()!; })";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(text));
  text = "try { $e.read(); assert false; } catch e { $ex = $e; }; "
         "assert $ex is InvalidOperationError";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(text));

  text = "$ex = 34; $e.setPrompt(function(p)=>{"
         "  $e.setPrompt(function(pp)=> $pp);"
         "  $p; })";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(text));
  text = "try { $e.read(); assert false; } catch e { $ex = $e; }; "
         "assert $ex is InvalidOperationError";
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(text));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}