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

TEST(GlobStarTest, globstar1) { // without pattern
  ASSERT_EQ(doGlobStar("**"),
            vec("AAA", "bbb", "bbb/AA21", "bbb/b21", "bbb/b21/A321", "bbb/b21/D"));
  ASSERT_EQ(doGlobStar("**/**"),
            vec("AAA", "bbb", "bbb/AA21", "bbb/b21", "bbb/b21/A321", "bbb/b21/D"));
  ASSERT_EQ(doGlobStar("**/**/**"),
            vec("AAA", "bbb", "bbb/AA21", "bbb/b21", "bbb/b21/A321", "bbb/b21/D"));
  ASSERT_EQ(doGlobStar("**////**////**"),
            vec("AAA", "bbb", "bbb/AA21", "bbb/b21", "bbb/b21/A321", "bbb/b21/D"));

  ASSERT_EQ(doGlobStar("**", Glob::Option::DOTGLOB),
            vec("AAA", "bbb", "bbb/.hidden", "bbb/AA21", "bbb/b21", "bbb/b21/A321", "bbb/b21/D"));

  ASSERT_EQ(doGlobStar("**/"), vec("bbb/", "bbb/b21/"));
  ASSERT_EQ(doGlobStar("**/**/"), vec("bbb/", "bbb/b21/"));
  ASSERT_EQ(doGlobStar("**/**/**/"), vec("bbb/", "bbb/b21/"));
  ASSERT_EQ(doGlobStar("**////"), vec("bbb/", "bbb/b21/"));
  ASSERT_EQ(doGlobStar("**////**////**////"), vec("bbb/", "bbb/b21/"));

  ASSERT_EQ(doGlobStar("./**"), vec("./", "./AAA", "./bbb", "./bbb/AA21", "./bbb/b21",
                                    "./bbb/b21/A321", "./bbb/b21/D"));
  ASSERT_EQ(doGlobStar("./**/**"), vec("./", "./AAA", "./bbb", "./bbb/AA21", "./bbb/b21",
                                       "./bbb/b21/A321", "./bbb/b21/D"));
  ASSERT_EQ(doGlobStar("./**/**/**"), vec("./", "./AAA", "./bbb", "./bbb/AA21", "./bbb/b21",
                                          "./bbb/b21/A321", "./bbb/b21/D"));
  ASSERT_EQ(doGlobStar("./**////**////**"), vec("./", "./AAA", "./bbb", "./bbb/AA21", "./bbb/b21",
                                                "./bbb/b21/A321", "./bbb/b21/D"));

  ASSERT_EQ(doGlobStar("./**/"), vec("./", "./bbb/", "./bbb/b21/"));
  ASSERT_EQ(doGlobStar("./**/**/"), vec("./", "./bbb/", "./bbb/b21/"));
  ASSERT_EQ(doGlobStar("./**////"), vec("./", "./bbb/", "./bbb/b21/"));
  ASSERT_EQ(doGlobStar("./**////**///////"), vec("./", "./bbb/", "./bbb/b21/"));
}

TEST(GlobStarTest, globstar2) { // with pattern
  ASSERT_EQ(doGlobStar("**/."), vec(".", "bbb/.", "bbb/b21/."));
  ASSERT_EQ(doGlobStar("**/./."), vec("./.", "bbb/./.", "bbb/b21/./."));
  ASSERT_EQ(doGlobStar("**/././."), vec("././.", "bbb/././.", "bbb/b21/././."));

  ASSERT_EQ(doGlobStar("**/*"),
            vec("AAA", "bbb", "bbb/AA21", "bbb/b21", "bbb/b21/A321", "bbb/b21/D"));
  ASSERT_EQ(doGlobStar("**/b*"), vec("bbb", "bbb/b21"));
  ASSERT_EQ(doGlobStar("**/b*/*"), vec("bbb/AA21", "bbb/b21", "bbb/b21/A321", "bbb/b21/D"));

  ASSERT_EQ(doGlobStar("**/.*"), vec("bbb/.hidden"));
  ASSERT_EQ(doGlobStar("**/*h*"), vec()); // not match .hidden

  ASSERT_EQ(doGlobStar("./**/*"),
            vec("./AAA", "./bbb", "./bbb/AA21", "./bbb/b21", "./bbb/b21/A321", "./bbb/b21/D"));
  ASSERT_EQ(doGlobStar("./**/*/*"),
            vec("./bbb/AA21", "./bbb/b21", "./bbb/b21/A321", "./bbb/b21/D"));
  ASSERT_EQ(doGlobStar("./**/*/*/*"), vec("./bbb/b21/A321", "./bbb/b21/D"));

  ASSERT_EQ(doGlobStar("./**/A*"), vec("./AAA", "./bbb/AA21", "./bbb/b21/A321"));
  ASSERT_EQ(doGlobStar("../**/dir/*"), vec("../dir/AAA", "../dir/bbb"));
  ASSERT_EQ(doGlobStar("../**/dir/*"), vec("../dir/AAA", "../dir/bbb"));
}

TEST(GlobStarTest, globstar3) { // multiple double stars
  ASSERT_EQ(doGlobStar("**/./**"),
            vec("./", "./AAA", "./bbb", "./bbb/AA21", "./bbb/b21", "./bbb/b21/A321", "./bbb/b21/D",
                "bbb/./", "bbb/./AA21", "bbb/./b21", "bbb/./b21/A321", "bbb/./b21/D", "bbb/b21/./",
                "bbb/b21/./A321", "bbb/b21/./D"));
  ASSERT_EQ(doGlobStar("**/**/**/./**"),
            vec("./", "./AAA", "./bbb", "./bbb/AA21", "./bbb/b21", "./bbb/b21/A321", "./bbb/b21/D",
                "bbb/./", "bbb/./AA21", "bbb/./b21", "bbb/./b21/A321", "bbb/./b21/D", "bbb/b21/./",
                "bbb/b21/./A321", "bbb/b21/./D"));
  ASSERT_EQ(doGlobStar("**/./////**"),
            vec("./", "./AAA", "./bbb", "./bbb/AA21", "./bbb/b21", "./bbb/b21/A321", "./bbb/b21/D",
                "bbb/./", "bbb/./AA21", "bbb/./b21", "bbb/./b21/A321", "bbb/./b21/D", "bbb/b21/./",
                "bbb/b21/./A321", "bbb/b21/./D"));

  ASSERT_EQ(doGlobStar("**/../**/.*"),
            vec("../dir/bbb/.hidden", "../dir2/.bcd", "bbb/../bbb/.hidden", "bbb/b21/../.hidden"));

  ASSERT_EQ(doGlobStar("**/b*/**"), vec("bbb/", "bbb/AA21", "bbb/b21", "bbb/b21/", "bbb/b21/A321",
                                        "bbb/b21/A321", "bbb/b21/D", "bbb/b21/D"));
  ASSERT_EQ(doGlobStar("**/b*/////**"),
            vec("bbb/", "bbb/AA21", "bbb/b21", "bbb/b21/", "bbb/b21/A321", "bbb/b21/A321",
                "bbb/b21/D", "bbb/b21/D"));
  ASSERT_EQ(doGlobStar("**/b*/././**"),
            vec("bbb/././", "bbb/././AA21", "bbb/././b21", "bbb/././b21/A321", "bbb/././b21/D",
                "bbb/b21/././", "bbb/b21/././A321", "bbb/b21/././D"));
  ASSERT_EQ(doGlobStar("**/b*/**/**/**"),
            vec("bbb/", "bbb/AA21", "bbb/b21", "bbb/b21/", "bbb/b21/A321", "bbb/b21/A321",
                "bbb/b21/D", "bbb/b21/D"));

  ASSERT_EQ(doGlobStar("**/b*/../**"),
            vec("bbb/../", "bbb/../AAA", "bbb/../bbb", "bbb/../bbb/AA21", "bbb/../bbb/b21",
                "bbb/../bbb/b21/A321", "bbb/../bbb/b21/D", "bbb/b21/../", "bbb/b21/../AA21",
                "bbb/b21/../b21", "bbb/b21/../b21/A321", "bbb/b21/../b21/D"));

  ASSERT_EQ(doGlobStar("**/b*/../../dir/**"),
            vec("bbb/../../dir/", "bbb/../../dir/AAA", "bbb/../../dir/bbb",
                "bbb/../../dir/bbb/AA21", "bbb/../../dir/bbb/b21", "bbb/../../dir/bbb/b21/A321",
                "bbb/../../dir/bbb/b21/D"));

  ASSERT_EQ(doGlobStar("**/b*/../*/../**"),
            vec("bbb/../bbb/../", "bbb/../bbb/../AAA", "bbb/../bbb/../bbb",
                "bbb/../bbb/../bbb/AA21", "bbb/../bbb/../bbb/b21", "bbb/../bbb/../bbb/b21/A321",
                "bbb/../bbb/../bbb/b21/D", "bbb/b21/../b21/../", "bbb/b21/../b21/../AA21",
                "bbb/b21/../b21/../b21", "bbb/b21/../b21/../b21/A321", "bbb/b21/../b21/../b21/D"));

  ASSERT_EQ(doGlobStar("**/b*/../*/../**", Glob::Option::FASTGLOB),
            vec("AAA", "bbb", "bbb/", "bbb/AA21", "bbb/AA21", "bbb/b21", "bbb/b21", "bbb/b21/A321",
                "bbb/b21/A321", "bbb/b21/D", "bbb/b21/D"));

  ASSERT_EQ(doGlobStar("**/b*/../**/../**/*r/**"),
            vec("bbb/../../dir/", "bbb/../../dir/AAA", "bbb/../../dir/bbb",
                "bbb/../../dir/bbb/AA21", "bbb/../../dir/bbb/b21", "bbb/../../dir/bbb/b21/A321",
                "bbb/../../dir/bbb/b21/D"));

  ASSERT_EQ(doGlobStar("**/b*/../**/../**/*r/**", Glob::Option::FASTGLOB),
            vec("../dir/", "../dir/AAA", "../dir/bbb", "../dir/bbb/AA21", "../dir/bbb/b21",
                "../dir/bbb/b21/A321", "../dir/bbb/b21/D"));

  ASSERT_EQ(doGlobStar("**/b*/../**/../**/*r/**/**"),
            vec("bbb/../../dir/", "bbb/../../dir/AAA", "bbb/../../dir/bbb",
                "bbb/../../dir/bbb/AA21", "bbb/../../dir/bbb/b21", "bbb/../../dir/bbb/b21/A321",
                "bbb/../../dir/bbb/b21/D"));
}

TEST(GlobStarTest, globstar4) { // symlink
  ASSERT_EQ(doGlobStar("../dir2/*"), vec("../dir2/ABC", "../dir2/link"));
  ASSERT_EQ(doGlobStar("../dir2/*", Glob::Option::DOTGLOB),
            vec("../dir2/.bcd", "../dir2/ABC", "../dir2/link"));

  ASSERT_EQ(doGlobStar("../dir2/*/*"), vec("../dir2/link/AAA", "../dir2/link/bbb"));
  ASSERT_EQ(doGlobStar("../dir2/*/*", Glob::Option::DOTGLOB),
            vec("../dir2/.bcd/A123", "../dir2/.bcd/b56", "../dir2/link/AAA", "../dir2/link/bbb"));

  ASSERT_EQ(doGlobStar("../dir2/**"), vec("../dir2/", "../dir2/ABC", "../dir2/link"));
  ASSERT_EQ(doGlobStar("../dir2/**", Glob::Option::DOTGLOB),
            vec("../dir2/", "../dir2/.bcd", "../dir2/.bcd/A123", "../dir2/.bcd/b56",
                "../dir2/.bcd/b56/DDD", "../dir2/ABC", "../dir2/link"));
  ASSERT_EQ(doGlobStar("../dir2/**/**", Glob::Option::DOTGLOB),
            vec("../dir2/", "../dir2/.bcd", "../dir2/.bcd/A123", "../dir2/.bcd/b56",
                "../dir2/.bcd/b56/DDD", "../dir2/ABC", "../dir2/link"));
  ASSERT_EQ(doGlobStar("../dir2/**/*", Glob::Option::DOTGLOB),
            vec("../dir2/.bcd", "../dir2/.bcd/A123", "../dir2/.bcd/b56", "../dir2/.bcd/b56/DDD",
                "../dir2/ABC", "../dir2/link"));
}

int main(int argc, char **argv) {
  if (chdir(GLOB_TEST_WORK_DIR) == -1) {
    fatal_perror("broken directory: %s", GLOB_TEST_WORK_DIR);
  }

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}