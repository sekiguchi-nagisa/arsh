#include "gtest/gtest.h"

#include <fstream>

#include <misc/files.hpp>
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

using namespace arsh;

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

  static void doTest() {
    ASSERT_TRUE(StringRef(GetParam()).endsWith(".test"));

    // parse status
    int status = ({
      StringRef ref = GetParam();
      ref.removeSuffix(strlen(".test"));
      auto pos = ref.lastIndexOf("-");
      ASSERT_TRUE(pos != StringRef::npos);
      auto num = ref.substr(pos + 1).toString();
      ASSERT_FALSE(num.empty());
      auto pair = convertToNum10<int>(num.c_str());
      ASSERT_TRUE(pair);
      ASSERT_TRUE(pair.value >= 0);
      pair.value;
    });

    // read expected output
    auto expected = ({
      StringRef ref = GetParam();
      ref.removeSuffix(strlen(".test"));
      std::string fileName = ref.toString();
      fileName += ".expected";

      std::ifstream input(fileName);
      std::string err;
      if (!input) {
        err = "expected output is not found: `";
        err += fileName;
        err += "'";
      }
      ASSERT_EQ("", err);
      readAll(input);
    });

    auto result = litecheck(GetParam()).execAndGetResult(false);
    ASSERT_EQ(WaitStatus::EXITED, result.status.kind);
    ASSERT_EQ(status, result.status.value);
    ASSERT_EQ(expected, result.err);
  }
};

TEST_P(LitecheckTest, base) { ASSERT_NO_FATAL_FAILURE(doTest()); }

INSTANTIATE_TEST_SUITE_P(LitecheckTest, LitecheckTest,
                         ::testing::ValuesIn(getSortedFileList(LITECHECK_TEST_DIR)));

struct LitecheckCLITest : public ExpectOutput {};

TEST_F(LitecheckCLITest, option) {
  const char *err = R"(litecheck: require `FILE' argument
Usage: litecheck [OPTIONS] FILE

Arguments:
  <FILE>  target test script (must include litecheck directives)

Options:
  -b BIN      target executable file
  -h, --help  show this help message
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(litecheck(), 1, "", err));

  err = R"(litecheck: file not found: `34'
Usage: litecheck [OPTIONS] FILE

Arguments:
  <FILE>  target test script (must include litecheck directives)

Options:
  -b BIN      target executable file
  -h, --help  show this help message
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(litecheck("34"), 2, "", err));

  err = R"(litecheck: require regular file: .
Usage: litecheck [OPTIONS] FILE

Arguments:
  <FILE>  target test script (must include litecheck directives)

Options:
  -b BIN      target executable file
  -h, --help  show this help message
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(litecheck("."), 2, "", err));

  err = R"(litecheck: invalid option: -q
Usage: litecheck [OPTIONS] FILE

Arguments:
  <FILE>  target test script (must include litecheck directives)

Options:
  -b BIN      target executable file
  -h, --help  show this help message
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(litecheck("-q"), 2, "", err));

  err = R"(litecheck: -b option needs argument
Usage: litecheck [OPTIONS] FILE

Arguments:
  <FILE>  target test script (must include litecheck directives)

Options:
  -b BIN      target executable file
  -h, --help  show this help message
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(litecheck("-b"), 2, "", err));

  err = R"(litecheck: file not found: `123'
Usage: litecheck [OPTIONS] FILE

Arguments:
  <FILE>  target test script (must include litecheck directives)

Options:
  -b BIN      target executable file
  -h, --help  show this help message
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(litecheck("-b", "123", LITECHECK_PATH), 2, "", err));

  err = R"(litecheck: must be executable: `.'
Usage: litecheck [OPTIONS] FILE

Arguments:
  <FILE>  target test script (must include litecheck directives)

Options:
  -b BIN      target executable file
  -h, --help  show this help message
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(litecheck("-b", ".", LITECHECK_PATH), 2, "", err));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}