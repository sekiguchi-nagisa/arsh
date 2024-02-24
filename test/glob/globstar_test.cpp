#include <algorithm>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include <glob.h>
#include <misc/files.hpp>

#ifndef GLOB_TEST_WORK_DIR
#error "require EXEC_TEST_DIR"
#endif

using namespace arsh;

static std::vector<std::string> doGlobStar(const char *pattern, Glob::Option extra = {}) {
  CancelToken cancel;
  auto option = Glob::Option::GLOBSTAR;
  setFlag(option, extra);
  Glob glob(pattern, option, nullptr);
  glob.setCancelToken(cancel);
  std::vector<std::string> ret;
  glob.setConsumer([&ret](std::string &&value) {
    ret.push_back(std::move(value));
    return true;
  });
  glob(nullptr);
  std::sort(ret.begin(), ret.end());
  return ret;
}

template <typename... T>
static std::vector<std::string> vec(T &&...args) {
  return {std::forward<T>(args)...};
}

TEST(GlobStarTest, globstar1) { // for globstar
  ASSERT_EQ(doGlobStar("**"),
            vec("AAA", "bbb", "bbb/AA21", "bbb/b21", "bbb/b21/A321", "bbb/b21/D"));
  ASSERT_EQ(doGlobStar("**/**"),
            vec("AAA", "bbb", "bbb/AA21", "bbb/b21", "bbb/b21/A321", "bbb/b21/D"));
  ASSERT_EQ(doGlobStar("**/**/**"),
            vec("AAA", "bbb", "bbb/AA21", "bbb/b21", "bbb/b21/A321", "bbb/b21/D"));
  ASSERT_EQ(doGlobStar("./**"), vec("./", "./AAA", "./bbb", "./bbb/AA21", "./bbb/b21",
                                    "./bbb/b21/A321", "./bbb/b21/D"));
  ASSERT_EQ(doGlobStar("./**/"), vec("./", "./bbb/", "./bbb/b21/"));
  ASSERT_EQ(doGlobStar("./**/**"), vec("./", "./AAA", "./bbb", "./bbb/AA21", "./bbb/b21",
                                       "./bbb/b21/A321", "./bbb/b21/D"));
  ASSERT_EQ(doGlobStar("./**/**/"), vec("./", "./bbb/", "./bbb/b21/"));
  ASSERT_EQ(doGlobStar("./**/**/**"), vec("./", "./AAA", "./bbb", "./bbb/AA21", "./bbb/b21",
                                          "./bbb/b21/A321", "./bbb/b21/D"));
  // ASSERT_EQ(doGlobStar("**/**/**/./**"), vec(
  ASSERT_EQ(doGlobStar("**/"), vec("bbb/", "bbb/b21/"));
  ASSERT_EQ(doGlobStar("./**/"), vec("./", "./bbb/", "./bbb/b21/"));
}

TEST(GlobStarTest, globstar2) { // for globstar
  ASSERT_EQ(doGlobStar("**/*"),
            vec("AAA", "bbb", "bbb/AA21", "bbb/b21", "bbb/b21/A321", "bbb/b21/D"));
  ASSERT_EQ(doGlobStar("./**/*"),
            vec("./AAA", "./bbb", "./bbb/AA21", "./bbb/b21", "./bbb/b21/A321", "./bbb/b21/D"));
  ASSERT_EQ(doGlobStar("./**/*/*"),
            vec("./bbb/AA21", "./bbb/b21", "./bbb/b21/A321", "./bbb/b21/D"));
  ASSERT_EQ(doGlobStar("./**/*/*/*"), vec("./bbb/b21/A321", "./bbb/b21/D"));
  ASSERT_EQ(doGlobStar("**/b*"), vec("bbb", "bbb/b21"));
  ASSERT_EQ(doGlobStar("./**/A*"), vec("./AAA", "./bbb/AA21", "./bbb/b21/A321"));
}

TEST(GlobStarTest, globstar3) {
  ASSERT_EQ(doGlobStar("**/b*/**"), vec("bbb/AA21", "bbb/b21", "bbb/b21/A321", "bbb/b21/D"));
  ASSERT_EQ(doGlobStar("**/b*/././**"),
            vec("bbb/././AA21", "bbb/././b21", "bbb/b21/././A321", "bbb/b21/././D"));
  ASSERT_EQ(doGlobStar("**/b*/../**"),
            vec("bbb/../AAA", "bbb/../bbb", "bbb/b21/../AA21", "bbb/b21/../b21"));
  ASSERT_EQ(doGlobStar("**/b*/../../**"),
            vec("bbb/../../CMakeLists.txt", "bbb/../../dir", "bbb/../../glob_test.cpp",
                "bbb/../../globstar_test.cpp", "bbb/b21/../../AAA", "bbb/b21/../../bbb"));
  ASSERT_EQ(doGlobStar("**/b*/../*/../**"), vec("bbb/../bbb/../AAA", "bbb/../bbb/../bbb",
                                                "bbb/b21/../b21/../AA21", "bbb/b21/../b21/../b21"));
  ASSERT_EQ(doGlobStar("**/b*/../**/../**"),
            vec("bbb/../bbb/../AAA", "bbb/../bbb/../bbb", "bbb/b21/../b21/../AA21",
                "bbb/b21/../b21/../b21"));
  ASSERT_EQ(doGlobStar("**/b*/../**/../**/../**"),
            vec("bbb/../bbb/../bbb/../AAA", "bbb/../bbb/../bbb/../bbb",
                "bbb/b21/../b21/../b21/../AA21", "bbb/b21/../b21/../b21/../b21"));
  ASSERT_EQ(doGlobStar("../**/dir/*"), vec("../dir/AAA", "../dir/bbb"));
  ASSERT_EQ(doGlobStar("../**/dir/*"), vec("../dir/AAA", "../dir/bbb"));
}

int main(int argc, char **argv) {
  if (chdir(GLOB_TEST_WORK_DIR) == -1) {
    fatal_perror("broken directory: %s", GLOB_TEST_WORK_DIR);
  }

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}