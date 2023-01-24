
#include "../test_common.h"

#ifndef ANALYZER_TEST_DIR
#error require ANALYZER_TEST_DIR
#endif

#ifndef ANALYZER_PATH
#error require ANALYZ_PATH
#endif

#ifndef YDSH_OPEN_TEST_HPP
#define YDSH_OPEN_TEST_HPP

using namespace ydsh;

struct OpenTest : public ::testing::TestWithParam<std::string> {
  static void doTest() {
    auto file = createFilePtr(fopen, "/dev/null", "w");
    auto result = ProcBuilder{ANALYZER_PATH, "--test-open"}
                      .addArg(GetParam())
                      .setOut(fileno(file.get()))
                      .exec();
    ASSERT_EQ(WaitStatus::EXITED, result.kind);
    ASSERT_EQ(0, result.value);
  }
};

static inline std::vector<std::string> getSortedFileList(const char *dir,
                                                         const char *ignored = nullptr) {
  auto ret = getFileList(dir, true);
  assert(!ret.empty());
  ret.erase(std::remove_if(ret.begin(), ret.end(),
                           [ignored](const std::string &v) {
                             if (ignored && StringRef(v).startsWith(ignored)) {
                               return true;
                             }
                             return !StringRef(v).endsWith(".ds");
                           }),
            ret.end());
  std::sort(ret.begin(), ret.end());
  ret.erase(std::unique(ret.begin(), ret.end()), ret.end());
  return ret;
}

#endif // YDSH_OPEN_TEST_HPP
