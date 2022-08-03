
#ifndef YDSH_TEST_INTERATIVE_BASE_HPP
#define YDSH_TEST_INTERATIVE_BASE_HPP

#include "gtest/gtest.h"

#include "../../tools/platform/platform.h"
#include "../test_common.h"
#include <config.h>

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
#define CTRL_F "\x06"
#define CTRL_Z "\x1A"

#define UP "\x1b[A"
#define DOWN "\x1b[B"

#define XSTR(v) #v
#define STR(v) XSTR(v)

#define PROMPT this->prompt

static inline std::string initPrompt() {
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

#endif // YDSH_TEST_INTERATIVE_BASE_HPP
