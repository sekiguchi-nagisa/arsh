#include "interactive_base.hpp"

TEST_F(InteractiveTest, expand_ctrlc1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->sendLine("echo "
                 "{/*/../*/../*/../*/../*/../*/../*/../*/../*,/*/../*/../*/../*/../*/../*/../*/../"
                 "*/../*,/*/../*/../*/../*/../*/../*/../*/../*/../*}");
  ASSERT_NO_FATAL_FAILURE(
      this->expect(PROMPT + "echo "
                            "{/*/../*/../*/../*/../*/../*/../*/../*/../*,/*/../*/../*/../*/../*/../"
                            "*/../*/../*/../*,/*/../*/../*/../*/../*/../*/../*/../*/../*}\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  this->send(CTRL_C);

  std::string err = format(R"([runtime error]
SystemError: glob expansion is canceled, caused by `%s'
    from (stdin):1 '<toplevel>()'
)",
                           strerror(EINTR));

  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 1));
}

TEST_F(InteractiveTest, expand_ctrlc2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->sendLine("echo {1..9999999999}");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "echo {1..9999999999}\n"));
  this->send(CTRL_C);

  std::string err = format(R"([runtime error]
SystemError: brace expansion is canceled, caused by `%s'
    from (stdin):1 '<toplevel>()'
)",
                           strerror(EINTR));

  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 1));
}

TEST_F(InteractiveTest, expand_ctrlc3) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("shctl set NULL_GLOB"));
  this->sendLine("echo "
                 "{/*/../*/../*/../*/../*/../*/../*/../*/../*,/*/../*/../*/../*/../*/../*/../*/../"
                 "*/../*,/*/../*/../*/../*/../*/../*/../*/../*/../*}");
  ASSERT_NO_FATAL_FAILURE(
      this->expect(PROMPT + "echo "
                            "{/*/../*/../*/../*/../*/../*/../*/../*/../*,/*/../*/../*/../*/../*/../"
                            "*/../*/../*/../*,/*/../*/../*/../*/../*/../*/../*/../*/../*}\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  this->send(CTRL_C);

  std::string err = format(R"([runtime error]
SystemError: glob expansion is canceled, caused by `%s'
    from (stdin):2 '<toplevel>()'
)",
                           strerror(EINTR));

  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 1));
}

TEST_F(InteractiveTest, expand_ctrlc4) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  if (getenv("ALPINE_WORKAROUND")) {
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 0));
    return;
  }

  // cancel glob expansion
  this->sendLine("source /*//*//*/*//*/*//*/*/*//**/?!/%/*/*/*/s*/../*/../*");
  ASSERT_NO_FATAL_FAILURE(
      this->expect(PROMPT + "source /*//*//*/*//*/*//*/*/*//**/?!/%/*/*/*/s*/../*/../*\n"));
  this->send(CTRL_C);

  std::string err = R"([semantic error] glob expansion is canceled
 --> (stdin):1:8
source /*//*//*/*//*/*//*/*/*//**/?!/%/*/*/*/s*/../*/../*
       ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
)";

  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));

  // last exit status is 0 (does not update $?)
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 0));
}

TEST_F(InteractiveTest, wait_ctrlc1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var j = while(true){} &"));
  this->sendLine("$j.wait()");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "$j.wait()\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  this->send(CTRL_C);

  std::string err = format(R"([runtime error]
SystemError: wait failed, caused by `%s'
    from (stdin):2 '<toplevel>()'
)",
                           strerror(EINTR));

  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 1));
}

TEST_F(InteractiveTest, wait_ctrlc2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("while(true){} &", ": Job = %1"));
  this->sendLine("fg");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "fg\nwhile(true){}\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  this->send(CTRL_C);

  std::string err = strsignal(SIGINT);
  err += "\n";
  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 128 + SIGINT));
}

TEST_F(InteractiveTest, ctrlz1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->sendLine("sh -c 'while true; do true; done'");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "sh -c 'while true; do true; done'\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  this->send(CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(
      this->expect(promptAfterCtrlZ(PROMPT), "[1] + Stopped  sh -c while true; do true; done\n"));

  // send CTRL_C, but already stopped.
  this->send(CTRL_C);
  ASSERT_NO_FATAL_FAILURE(this->expect("\n" + PROMPT));

  // resume and kill
  this->sendLine("fg");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "fg\nsh -c while true; do true; done\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  this->send(CTRL_C);

  std::string err = strsignal(SIGINT);
  err += "\n";
  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 128 + SIGINT));
}

TEST_F(InteractiveTest, ctrlz2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("while(true 1){} &", ": Job = %1"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("while(true 2){} &", ": Job = %2"));

  // foreground and suspend
  this->sendLine("fg");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "fg\nwhile(true 2){}\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  this->send(CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(
      this->expect(promptAfterCtrlZ(PROMPT), "[2] + Stopped  while(true 2){}\n"));

  // resume all jobs
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("bg %1 %2", "[1]  while(true 1){}\n"
                                                              "[2]  while(true 2){}"));

  // show jobs list
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("jobs", "[1] - Running  while(true 1){}\n"
                                                          "[2] + Running  while(true 2){}"));

  // foreground [2] and interrupt
  this->sendLine("fg");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "fg\nwhile(true 2){}\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  this->send(CTRL_C);

  std::string err = strsignal(SIGINT);
  err += "\n";
  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));

  // foreground [1] and interrupt
  this->sendLine("fg");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "fg\nwhile(true 1){}\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  this->send(CTRL_C);

  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));

  // show empty jobs
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("jobs"));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 0));
}

TEST_F(InteractiveTest, ctrlz3) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("loop() { while(true $@){} }"));
  this->sendLine("while(true 1){}  |  loop 2");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "while(true 1){}  |  loop 2\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  this->send(CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(
      this->expect(promptAfterCtrlZ(PROMPT), "[1] + Stopped  while(true 1){} | loop 2\n"));

  // resume and kill
  this->sendLine("fg");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "fg\nwhile(true 1){} | loop 2\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  this->send(CTRL_C);

  std::string err = strsignal(SIGINT);
  err += "\n";
  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 128 + SIGINT));
}

TEST_F(InteractiveTest, cmdsub_ctrlz1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  const char *line = "var a = \"$(while(true){})\"";
  this->sendLine(line);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + line + "\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // send CTRL_Z, but not stopped (due to ignore SIGTSTP)
  this->send(CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(this->expect(ctrlZChar()));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // send 'echo hello', but still wait output (not stopped)
  this->sendLine("echo hello");
  ASSERT_NO_FATAL_FAILURE(this->expect("echo hello\n"));

  // send CTRL_C and terminated
  this->send(CTRL_C);

  std::string err = format(R"([runtime error]
SystemError: command substitution failed, caused by `%s'
    from (stdin):1 '<toplevel>()'
)",
                           strerror(EINTR));
  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  err = format("^\\[1\\] \\+ [0-9]+ %s  while\\(true\\)\\{\\}\n", strsignal(SIGINT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpectRegex("1", ": Int = 1", err));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 1));
}

TEST_F(InteractiveTest, cmdsub_ctrlz2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  // launch new ydsh (new process group)
  this->sendLine("call $YDSH_BIN --quiet --norc");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "call $YDSH_BIN --quiet --norc\n" + PROMPT));

  const char *line = "var a = $(while(true){})";
  this->sendLine(line);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + line + "\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // send CTRL_Z, but not stopped (due to ignore SIGTSTP)
  this->send(CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(this->expect(ctrlZChar()));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // send 'echo hello', but still wait output (not stopped)
  this->sendLine("echo hello");
  ASSERT_NO_FATAL_FAILURE(this->expect("echo hello\n"));

  // send CTRL_C and terminated
  this->send(CTRL_C);

  std::string err = format(R"([runtime error]
SystemError: command substitution failed, caused by `%s'
    from (stdin):1 '<toplevel>()'
)",
                           strerror(EINTR));
  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  err = format("^\\[1\\] \\+ [0-9]+ %s  while\\(true\\)\\{\\}\n", strsignal(SIGINT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpectRegex("2", ": Int = 2", err));
  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->expect("\n" + PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 1));
}

TEST_F(InteractiveTest, cmdsub_interactive) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  // launch new ydsh with force interactive
  const char *line = "var aa = $(call $YDSH_BIN -i --quiet --norc < /dev/null)";
  this->sendLine(line);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + line + "\n" + PROMPT));

  line = "echo hello world";
  this->sendLine(line);
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + line + "\n" + PROMPT));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // exit shell
  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->expect("\n" + PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$aa", ": [String] = [hello, world]"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 0));
}

TEST_F(InteractiveTest, wait1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("sleep 1 &", ": Job = %1"));
  std::this_thread::sleep_for(std::chrono::seconds(1));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpectRegex("wait", "", "^\\[1\\] \\+ [0-9]+ Done  sleep 1\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 0));
}

TEST_F(InteractiveTest, wait2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("{ sleep 1; exit 45; } &", ": Job = %1"));
  std::this_thread::sleep_for(std::chrono::seconds(1));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpectRegex(
      "1230 + 4", ": Int = 1234", "^\\[1\\] \\+ [0-9]+ Exit 45  \\{ sleep 1; exit 45; \\}\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 0));
}

TEST_F(InteractiveTest, wait3) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var j = while(true){} &"));

  // fg/bg/wait in subshell
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$(wait $j); assert $? == 255", ": [String] = []"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$(fg $j); assert $? == 1", ": [String] = []",
                                                  "ydsh: fg: no job control in this shell\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$(bg $j); assert $? == 1", ": [String] = []",
                                                  "ydsh: bg: no job control in this shell\n"));

  std::string err = format("^\\[1\\] \\+ [0-9]+ %s  while\\(true\\)\\{\\}\n", strsignal(SIGKILL));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpectRegex("$j.raise($SIGKILL); wait $j", "", err));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 137));
}

TEST_F(InteractiveTest, wait4) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var j = while(true){} &!"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("jobs")); // disowned job is not printed
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("jobs $j", "", "ydsh: jobs: %1: no such job\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$(jobs $j)", ": [String] = []", "ydsh: jobs: %1: no such job\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("wait $j; assert $? == 127", "", "ydsh: wait: %1: no such job\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$j.raise($SIGKILL); $j.wait()", ": Int = 137"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 127));
}

TEST_F(InteractiveTest, bg1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("read &", ": Job = %1"));

  if (platform::platform() == platform::PlatformType::LINUX) {
    this->sendLine("fg");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "fg\nread\n"));
    this->sendLine("true");
    ASSERT_NO_FATAL_FAILURE(this->expect("true\n" + PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $REPLY.empty()")); // REPLY is empty

    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("read | __gets &", ": Job = %1"));
    this->sendLine("fg");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "fg\nread | __gets\n"));
    this->sendLine("false");
    ASSERT_NO_FATAL_FAILURE(this->expect("false\n" + PROMPT));
  } else if (platform::platform() == platform::PlatformType::DARWIN) {
    std::string err = "ydsh: read: 0: ";
    err += strerror(EINTR);
    err += "\n";
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("fg", "read", err.c_str()));

    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("read | __gets &", ": Job = %1"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("fg", "read | __gets", err.c_str()));
  }

  // disable monitor option
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("call $YDSH_BIN -c 'read &'"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $? == 0"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("call $YDSH_BIN -c 'read | __gets &'"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $? == 0"));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 0));
}

TEST_F(InteractiveTest, disown1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("disown", "", "ydsh: disown: current: no such job\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var j1 = while(true 1){} &"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var j2 = while(true 2){} &"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("disown hoge %1", "", "ydsh: disown: hoge: no such job\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("jobs", "[1] - Running  while(true 1){}\n"
                                                          "[2] + Running  while(true 2){}"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("disown %1 %2; assert $? == 0"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("jobs"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("disown %1", "", "ydsh: disown: %1: no such job\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("wait $j1; assert $? == 127", "", "ydsh: wait: %1: no such job\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$j1.raise($SIGKILL); $j1.wait()", ": Int = 137"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$j2.raise($SIGTERM); $j2.wait()", ": Int = 143"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 127));
}

TEST_F(InteractiveTest, disown2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("disown", "", "ydsh: disown: current: no such job\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var j1 = while(true 1){} &"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("jobs", "[1] + Running  while(true 1){}"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("disown; assert $? == 0"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("jobs"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$j1.raise($SIGTERM); $j1.wait()", ": Int = 143"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 0));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}