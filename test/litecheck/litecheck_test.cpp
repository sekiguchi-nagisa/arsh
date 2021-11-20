#include "gtest/gtest.h"

#include <fstream>

#include <misc/files.h>
#include <misc/num_util.hpp>
#include <misc/string_ref.hpp>

#include "../test_common.h"

#ifndef LITECHECK_PATH
#error require LITECHECK_PATH
#endif

#ifndef LITECHECK_TEST_DIR
#error require LITECHECK_TEST_DIR
#endif

#ifndef BIN_PATH
#error require BIN_PATH
#endif

using namespace ydsh;

static std::vector<std::string> getSortedFileList(const char *dir) {
  auto ret = getFileList(dir, true);
  assert(!ret.empty());
  ret.erase(std::remove_if(ret.begin(), ret.end(),
                           [](const std::string &v) { return !StringRef(v).endsWith(".test"); }),
            ret.end());
  std::sort(ret.begin(), ret.end());
  ret.erase(std::unique(ret.begin(), ret.end()), ret.end());
  return ret;
}

static void addArg(ProcBuilder &) {}

template <typename T, typename... R>
static void addArg(ProcBuilder &builder, T &&arg, R &&...rest) {
  builder.addArg(std::forward<T>(arg));
  addArg(builder, std::forward<R>(rest)...);
}

template <typename... T>
static ProcBuilder litecheck(T &&...arg) {
  ProcBuilder procBuilder{BIN_PATH, LITECHECK_PATH};
  addArg(procBuilder, std::forward<T>(arg)...);
  return procBuilder;
}

struct LitecheckTest : public ::testing::TestWithParam<std::string> {
  static std::string readAll(std::ifstream &input) {
    std::string ret;
    for (std::string line; std::getline(input, line);) {
      ret += line;
      ret += '\n';
    }
    return ret;
  }

  void doTest() {
    ASSERT_TRUE(StringRef(this->GetParam()).endsWith(".test"));

    // parse status
    int status = ({
      StringRef ref = this->GetParam();
      ref.removeSuffix(strlen(".test"));
      auto pos = ref.lastIndexOf("-");
      ASSERT_TRUE(pos != StringRef::npos);
      auto num = ref.substr(pos + 1).toString();
      ASSERT_FALSE(num.empty());
      auto pair = convertToNum<int>(num.c_str());
      ASSERT_TRUE(pair.second);
      ASSERT_TRUE(pair.first >= 0);
      pair.first;
    });

    // read expected output
    auto expected = ({
      StringRef ref = this->GetParam();
      ref.removeSuffix(strlen(".test"));
      std::string fileName = ref.toString();
      fileName += ".expected";

      std::ifstream input(fileName);
      ASSERT_TRUE(input);
      readAll(input);
    });

    auto result = litecheck(this->GetParam()).execAndGetResult(false);
    ASSERT_EQ(WaitStatus::EXITED, result.status.kind);
    ASSERT_EQ(status, result.status.value);
    ASSERT_EQ(expected, result.err);
  }
};

TEST_P(LitecheckTest, base) { ASSERT_NO_FATAL_FAILURE(this->doTest()); }

INSTANTIATE_TEST_SUITE_P(LitecheckTest, LitecheckTest,
                         ::testing::ValuesIn(getSortedFileList(LITECHECK_TEST_DIR)));

struct LitecheckCLITest : public ExpectOutput {};

TEST_F(LitecheckCLITest, option) {
  const char *err = R"(require file
usage: litecheck [-b bin] file
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(litecheck(), 2, "", err));

  err = R"(file not found: `34'
usage: litecheck [-b bin] file
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(litecheck("34"), 2, "", err));

  err = R"(require regular file: .
usage: litecheck [-b bin] file
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(litecheck("."), 2, "", err));

  err = R"(invalid option: -q
usage: litecheck [-b bin] file
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(litecheck("-q"), 2, "", err));

  err = R"(`-b' option requires argument
usage: litecheck [-b bin] file
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(litecheck("-b"), 2, "", err));

  err = R"(file not found: `123'
usage: litecheck [-b bin] file
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(litecheck("-b", "123", LITECHECK_PATH), 2, "", err));

  err = R"(must be executable: `.'
usage: litecheck [-b bin] file
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(litecheck("-b", ".", LITECHECK_PATH), 2, "", err));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}