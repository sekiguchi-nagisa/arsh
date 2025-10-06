
#ifndef ARSH_TEST_INTERACTIVE_BASE_HPP
#define ARSH_TEST_INTERACTIVE_BASE_HPP

#include "gtest/gtest.h"

#include "../test_common.h"
#include <config.h>
#include <constant.h>

#ifndef INTERACTIVE_TEST_WORK_DIR
#error "require INTERACTIVE_TEST_WORK_DIR"
#endif

#ifndef BIN_PATH
#error "require BIN_PATH"
#endif

using namespace arsh;

#define CTRL_A "\x01"
#define CTRL_B "\x02"
#define CTRL_C "\x03"
#define CTRL_D "\x04"
#define CTRL_E "\x05"
#define CTRL_F "\x06"
#define CTRL_H "\x08"
#define CTRL_I "\x09"
#define CTRL_J "\x0A"
#define CTRL_K "\x0B"
#define CTRL_L "\x0C"
#define CTRL_M "\x0D"
#define CTRL_N "\x0E"
#define CTRL_P "\x10"
#define CTRL_R "\x12"
#define CTRL_T "\x14"
#define CTRL_U "\x15"
#define CTRL_V "\x16"
#define CTRL_W "\x17"
#define CTRL_X "\x18"
#define CTRL_Y "\x19"
#define CTRL_Z "\x1A"

#define ESC_(E) "\x1b" E

#define UP "\x1b[A"
#define DOWN "\x1b[B"
#define RIGHT "\x1b[C"
#define LEFT "\x1b[D"
#define HOME "\x1b[H"
#define END "\x1b[F"

#define SHIFT_TAB "\x1b[Z"
#define ALT_ENTER "\x1b" CTRL_M

#define XSTR(v) #v
#define STR(v) XSTR(v)

#define PROMPT this->prompt

inline std::string initPrompt() {
  std::string v = "arsh-" STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION);
  v += (getuid() == 0 ? "# " : "$ ");
  return v;
}

struct InteractiveTest : public InteractiveShellBase {
  InteractiveTest() : InteractiveShellBase(BIN_PATH, INTERACTIVE_TEST_WORK_DIR) {
    this->timeoutMSec = 120;
    this->setPrompt(initPrompt());
  }

  void changePrompt(const char *newPrompt) {
    auto oldPrompt = this->prompt;
    std::string line = "$LINE_EDIT.setPrompter(function(p) => '";
    line += newPrompt;
    line += "')";
    ASSERT_NO_FATAL_FAILURE(this->sendLine(line.c_str()));
    this->prompt = newPrompt;
    ASSERT_NO_FATAL_FAILURE(this->expect(oldPrompt + line + "\n" + newPrompt));
  }
};

inline std::string promptAfterCtrlC(const std::string &prompt) {
  std::string value;
  if (!platform::isCygwinOrMsys(platform::platform())) {
    value += "^C%\n";
  }
  value += prompt;
  return value;
}

inline std::string promptAfterCtrlZ(const std::string &prompt) {
  std::string value;
  if (!platform::isCygwinOrMsys(platform::platform())) {
    value += "^Z%\n";
  }
  value += prompt;
  return value;
}

inline const char *ctrlCChar() {
  return !platform::isCygwinOrMsys(platform::platform()) ? "^C" : "";
}

inline const char *ctrlZChar() {
  return !platform::isCygwinOrMsys(platform::platform()) ? "^Z" : "";
}

#endif // ARSH_TEST_INTERACTIVE_BASE_HPP
