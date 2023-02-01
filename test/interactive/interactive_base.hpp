
#ifndef YDSH_TEST_INTERACTIVE_BASE_HPP
#define YDSH_TEST_INTERACTIVE_BASE_HPP

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

using namespace ydsh;

#define CTRL_A "\x01"
#define CTRL_B "\x02"
#define CTRL_C "\x03"
#define CTRL_D "\x04"
#define CTRL_E "\x05"
#define CTRL_F "\x06"
#define CTRL_H "\x08"
#define CTRL_K "\x0B"
#define CTRL_N "\x0E"
#define CTRL_P "\x10"
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

#define XSTR(v) #v
#define STR(v) XSTR(v)

#define PROMPT this->prompt

inline std::string initPrompt() {
  std::string v = "ydsh-" STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION);
  v += (getuid() == 0 ? "# " : "$ ");
  return v;
}

struct InteractiveTest : public InteractiveShellBase {
  InteractiveTest() : InteractiveShellBase(BIN_PATH, INTERACTIVE_TEST_WORK_DIR) {
    this->timeout = 120;
    this->setPrompt(initPrompt());
  }
};

inline std::string promptAfterCtrlC(const std::string &prompt) {
  std::string value;
  if (platform::platform() != platform::PlatformType::CYGWIN) {
    value += "^C%\n";
  }
  value += prompt;
  return value;
}

inline std::string promptAfterCtrlZ(const std::string &prompt) {
  std::string value;
  if (platform::platform() != platform::PlatformType::CYGWIN) {
    value += "^Z%\n";
  }
  value += prompt;
  return value;
}

inline const char *ctrlZChar() {
  return platform::platform() != platform::PlatformType::CYGWIN ? "^Z" : "";
}

#endif // YDSH_TEST_INTERACTIVE_BASE_HPP
