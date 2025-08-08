#include "interactive_base.hpp"

TEST_F(InteractiveTest, expand_ctrlc1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  {
    auto cleanup = this->withTimeout(300);
    this->sendLine(
        "echo "
        "{/*/../*/../*/../*/../*/../*/../*/../*/../*,/*/../*/../*/../*/../*/../*/../*/../"
        "*/../*,/*/../*/../*/../*/../*/../*/../*/../*/../*}");
    ASSERT_NO_FATAL_FAILURE(this->expect(
        PROMPT + "echo "
                 "{/*/../*/../*/../*/../*/../*/../*/../*/../*,/*/../*/../*/../*/../*/../"
                 "*/../*/../*/../*,/*/../*/../*/../*/../*/../*/../*/../*/../*}\n"));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    this->send(CTRL_C);

    std::string err = format(R"([runtime error]
SystemError: glob expansion is canceled, caused by `%s'
    from (stdin):1 '<toplevel>()'
)",
                             strerror(EINTR));

    ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  }
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
  {
    auto cleanup = this->withTimeout(300);
    this->sendLine(
        "echo "
        "{/*/../*/../*/../*/../*/../*/../*/../*/../*,/*/../*/../*/../*/../*/../*/../*/../"
        "*/../*,/*/../*/../*/../*/../*/../*/../*/../*/../*}");
    ASSERT_NO_FATAL_FAILURE(this->expect(
        PROMPT + "echo "
                 "{/*/../*/../*/../*/../*/../*/../*/../*/../*,/*/../*/../*/../*/../*/../"
                 "*/../*/../*/../*,/*/../*/../*/../*/../*/../*/../*/../*/../*}\n"));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    this->send(CTRL_C);

    std::string err = format(R"([runtime error]
SystemError: glob expansion is canceled, caused by `%s'
    from (stdin):2 '<toplevel>()'
)",
                             strerror(EINTR));

    ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  }
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 1));
}

TEST_F(InteractiveTest, wait_ctrlc1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var j = while(true){} &"));
  {
    auto cleanup = this->withTimeout(300);
    this->sendLine("$j.wait()");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "$j.wait()\n"));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    this->send(CTRL_C);

    std::string err = format(R"([runtime error]
SystemError: wait failed, caused by `%s'
    from (stdin):2 '<toplevel>()'
)",
                             strerror(EINTR));

    ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  }
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 1));
}

TEST_F(InteractiveTest, wait_ctrlc2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("while(true){} &", ": Job = %1"));
  {
    auto cleanup = this->withTimeout(300);
    this->sendLine("fg");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "fg\nwhile(true){}\n"));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    this->send(CTRL_C);

    std::string err = strsignal(SIGINT);
    err += "\n";
    ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  }
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 128 + SIGINT));
}

TEST_F(InteractiveTest, lastpipe_ctrlc1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->sendLine("true | while($true){}");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "true | while($true){}\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  this->send(CTRL_C);
  std::string err = format(R"([runtime error]
SystemError: %s
    from (builtin):8 'function _DEF_SIGINT()'
    from (stdin):1 '<toplevel>()'
)",
                           strsignal(SIGINT));
  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, lastpipe_ctrlc2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->sendLine("var aa = sleep 1000 | sleep 2000 | { sleep 3000; }");
  ASSERT_NO_FATAL_FAILURE(
      this->expect(PROMPT + "var aa = sleep 1000 | sleep 2000 | { sleep 3000; }\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  this->send(CTRL_C);
  std::string err = strsignal(SIGINT);
  err += "\n";
  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert ! $aa"));
  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(130, WaitStatus::EXITED, "\n"));
}

TEST_F(InteractiveTest, lastpipe_ctrlc3) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->sendLine("sleep 3000");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "sleep 3000\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  this->send(CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlZ(PROMPT), "[1] + Stopped  sleep 3000\n"));

  this->sendLine("var aa = sleep 1000 | sleep 2000 | { jobs; fg; }");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT +
                                       "var aa = sleep 1000 | sleep 2000 | { jobs; fg; }\n"
                                       "[1] + Stopped  sleep 3000\n"
                                       "[2]   Running  sleep 1000 | sleep 2000 | { jobs; fg; }\n"
                                       "sleep 3000\n"));

  this->send(CTRL_C);
  std::string err = strsignal(SIGINT);
  err += "\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  ASSERT_NO_FATAL_FAILURE(this->expect(ctrlCChar(), err));
  this->send(CTRL_C);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT)));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert ! $aa"));
  this->send(CTRL_D);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(130, WaitStatus::EXITED, "\n"));
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

TEST_F(InteractiveTest, subshell_ctrlz1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->sendLine("var a = &(while(true){})");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "var a = &(while(true){})\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  this->send(CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlZ(PROMPT), "[1] + Stopped  while(true){}\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$a", ": Bool = false"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$?", format(": Int = %d", 128 + SIGTSTP).c_str()));

  // send CTRL_C, but already stopped.
  this->send(CTRL_C);
  ASSERT_NO_FATAL_FAILURE(this->expect("\n" + PROMPT));

  // resume and kill
  this->sendLine("fg");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "fg\nwhile(true){}\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  this->send(CTRL_C);

  std::string err = strsignal(SIGINT);
  err += "\n";
  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 128 + SIGINT));
}

TEST_F(InteractiveTest, subshell_ctrlz2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  this->sendLine("var s = &(while(true){} | while(true){ ; })");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "var s = &(while(true){} | while(true){ ; })\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  this->send(CTRL_Z);
  ASSERT_NO_FATAL_FAILURE(
      this->expect(promptAfterCtrlZ(PROMPT), "[1] + Stopped  while(true){} | while(true){ ; }\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$s", ": Bool = false"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("$?", format(": Int = %d", 128 + SIGTSTP).c_str()));

  // send CTRL_C, but already stopped.
  this->send(CTRL_C);
  ASSERT_NO_FATAL_FAILURE(this->expect("\n" + PROMPT));

  // resume and kill
  this->sendLine("fg");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "fg\nwhile(true){} | while(true){ ; }\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(400));
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

  {
    auto cleanup = this->withTimeout(400);

    // launch new arsh (new process group)
    this->sendLine("call $BIN_NAME --quiet --norc");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "call $BIN_NAME --quiet --norc\n" + PROMPT));

    const char *line = "var a = $(while(true){})";
    this->sendLine(line);
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + line + "\n"));

    // send CTRL_Z, but not stopped (due to ignore SIGTSTP)
    this->send(CTRL_Z);
    ASSERT_NO_FATAL_FAILURE(this->expect(ctrlZChar()));

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
  }

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 1));
}

TEST_F(InteractiveTest, cmdsub_interactive) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  // launch new arsh with force interactive
  const char *line = "var aa = $(call $BIN_NAME -i --quiet --norc)";
  this->sendLine(line);
  std::this_thread::sleep_for(std::chrono::milliseconds(400));
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

TEST_F(InteractiveTest, procsub_job) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("var aa = >(__gets); assert ($aa as String).startsWith('/dev/fd/')"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert ! $JOB.get($aa.job() as String)"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("jobs"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$aa.job().kill($SIGTERM)"));
  std::string out = format(": Int = %d", 128 + SIGTERM);
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$aa.job().wait()", out.c_str()));
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
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("{ sleep 0.5; exit 45; } &", ": Job = %1"));
  std::this_thread::sleep_for(std::chrono::seconds(1));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpectRegex(
      "1230 + 4", ": Int = 1234", "^\\[1\\] \\+ [0-9]+ Exit 45  \\{ sleep 0.5; exit 45; \\}\n"));
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
                                                  "(stdin):3: fg: no job control in this shell\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$(bg $j); assert $? == 1", ": [String] = []",
                                                  "(stdin):4: bg: no job control in this shell\n"));

  std::string err = format("^\\[1\\] \\+ [0-9]+ %s  while\\(true\\)\\{\\}\n", strsignal(SIGKILL));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpectRegex("$j.kill($SIGKILL); wait $j", "", err));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 137));
}

TEST_F(InteractiveTest, wait4) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var j = while(true){} &!"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("jobs")); // disowned job is not printed
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("jobs $j", "", "(stdin):3: jobs: %1: no such job\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$(jobs $j)", ": [String] = []",
                                                  "(stdin):4: jobs: %1: no such job\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("wait $j; assert $? == 127", "",
                                                  "(stdin):5: wait: %1: no such job\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$j.kill($SIGKILL); $j.wait()", ": Int = 137"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 127));
}

TEST_F(InteractiveTest, bg1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("read &", ": Job = %1"));
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

  // disable monitor option
  {
    auto cleanup = this->withTimeout(400);
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("call $BIN_NAME -c 'read &'"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $? == 0"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("call $BIN_NAME -c 'read | __gets &'"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $? == 0"));
  }
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 0));
}

TEST_F(InteractiveTest, bg2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("read &|", ": Job = %1"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("fg", "", "(stdin):2: fg: current: no such job\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $? == 1"));

  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("read | __gets &!", ": Job = %2"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("fg", "", "(stdin):5: fg: current: no such job\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $? == 1"));

  // disable monitor option
  {
    auto cleanup = this->withTimeout(400);
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("call $BIN_NAME -c 'read &!'"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $? == 0"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("call $BIN_NAME -c 'read | __gets &|'"));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $? == 0"));
  }
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 0));
}

TEST_F(InteractiveTest, disown1) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("disown", "", "(stdin):1: disown: current: no such job\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var j1 = while(true 1){} &"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var j2 = while(true 2){} &"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("disown hoge %1", "", "(stdin):4: disown: hoge: no such job\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("jobs", "[1] - Running  while(true 1){}\n"
                                                          "[2] + Running  while(true 2){}"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("disown %1 %2; assert $? == 0"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("jobs"));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("disown %1", "", "(stdin):8: disown: %1: no such job\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("wait $j1; assert $? == 127", "",
                                                  "(stdin):9: wait: %1: no such job\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$j1.kill($SIGKILL); $j1.wait()", ": Int = 137"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$j2.kill($SIGTERM); $j2.wait()", ": Int = 143"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 127));
}

TEST_F(InteractiveTest, disown2) {
  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  ASSERT_NO_FATAL_FAILURE(
      this->sendLineAndExpect("disown", "", "(stdin):1: disown: current: no such job\n"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("var j1 = while(true 1){} &"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("jobs", "[1] + Running  while(true 1){}"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("disown; assert $? == 0"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("jobs"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("$j1.kill($SIGTERM); $j1.wait()", ": Int = 143"));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 0));
}

TEST_F(InteractiveTest, changeTCPGRPInChild) {
  if (platform::isCygwinOrMsys(platform::platform())) {
    return;
  }

  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
  {
    auto cleanup = this->withTimeout(500);
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect(
        "call $BIN_NAME -c 'shctl set monitor; ls > /dev/null;' &", ": Job = %1"));
    std::string err = format("[warn] retry readLine, caused by `%s'\n", strerror(EIO));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("", "", err.c_str()));
  }
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 0));
}

TEST_F(InteractiveTest, changeFDSetting) {
  if (platform::isCygwinOrMsys(platform::platform())) {
    return; // skip since cygwin does not correctly set O_NONBLOCK
  }

  this->invoke("--quiet", "--norc");

  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));

  {
    auto cleanup = this->withTimeout(500);

    std::string err = format("cat: .*: %s\n", strerror(EAGAIN));
    ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpectRegex(
        HELPER_PATH " nonblock-in && LANG=C cat - ", ": Bool = false", err));
  }
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert $? == 1"));

  // change stdin to non-blocking
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndExpect("assert " HELPER_PATH " nonblock-in"));

  // cat command still blocking mode even if previous command change to non-blocking mode
  this->sendLine("cat");
  ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT + "cat\n"));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  this->send(CTRL_C);
  std::string err = strsignal(SIGINT);
  err += "\n";
  ASSERT_NO_FATAL_FAILURE(this->expect(promptAfterCtrlC(PROMPT), err));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("exit", 128 + SIGINT));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}