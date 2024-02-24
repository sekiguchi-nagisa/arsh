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

static std::vector<std::string> testGlobStar(const char *pattern, Glob::Option extra = {}) {
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

TEST(GlobStarTest, globstar1) { // for globstar
  auto ret = testGlobStar("**");
  ASSERT_EQ(6, ret.size());
  ASSERT_EQ("AAA", ret[0]);
  ASSERT_EQ("bbb", ret[1]);
  ASSERT_EQ("bbb/AA21", ret[2]);
  ASSERT_EQ("bbb/b21", ret[3]);
  ASSERT_EQ("bbb/b21/A321", ret[4]);
  ASSERT_EQ("bbb/b21/D", ret[5]);

  ret = testGlobStar("**/**");
  ASSERT_EQ(6, ret.size());
  ASSERT_EQ("AAA", ret[0]);
  ASSERT_EQ("bbb", ret[1]);
  ASSERT_EQ("bbb/AA21", ret[2]);
  ASSERT_EQ("bbb/b21", ret[3]);
  ASSERT_EQ("bbb/b21/A321", ret[4]);
  ASSERT_EQ("bbb/b21/D", ret[5]);

  ret = testGlobStar("**/**/**");
  ASSERT_EQ(6, ret.size());
  ASSERT_EQ("AAA", ret[0]);
  ASSERT_EQ("bbb", ret[1]);
  ASSERT_EQ("bbb/AA21", ret[2]);
  ASSERT_EQ("bbb/b21", ret[3]);
  ASSERT_EQ("bbb/b21/A321", ret[4]);
  ASSERT_EQ("bbb/b21/D", ret[5]);

  ret = testGlobStar("./**");
  ASSERT_EQ(7, ret.size());
  ASSERT_EQ("./", ret[0]);
  ASSERT_EQ("./AAA", ret[1]);
  ASSERT_EQ("./bbb", ret[2]);
  ASSERT_EQ("./bbb/AA21", ret[3]);
  ASSERT_EQ("./bbb/b21", ret[4]);
  ASSERT_EQ("./bbb/b21/A321", ret[5]);
  ASSERT_EQ("./bbb/b21/D", ret[6]);

  ret = testGlobStar("./**/");
  ASSERT_EQ(3, ret.size());
  ASSERT_EQ("./", ret[0]);
  ASSERT_EQ("./bbb/", ret[1]);
  ASSERT_EQ("./bbb/b21/", ret[2]);

  ret = testGlobStar("./**/**");
  ASSERT_EQ(7, ret.size());
  ASSERT_EQ("./", ret[0]);
  ASSERT_EQ("./AAA", ret[1]);
  ASSERT_EQ("./bbb", ret[2]);
  ASSERT_EQ("./bbb/AA21", ret[3]);
  ASSERT_EQ("./bbb/b21", ret[4]);
  ASSERT_EQ("./bbb/b21/A321", ret[5]);
  ASSERT_EQ("./bbb/b21/D", ret[6]);

  ret = testGlobStar("./**/**/");
  ASSERT_EQ(3, ret.size());
  ASSERT_EQ("./", ret[0]);
  ASSERT_EQ("./bbb/", ret[1]);
  ASSERT_EQ("./bbb/b21/", ret[2]);

  ret = testGlobStar("./**/**/**");
  ASSERT_EQ(7, ret.size());
  ASSERT_EQ("./", ret[0]);
  ASSERT_EQ("./AAA", ret[1]);
  ASSERT_EQ("./bbb", ret[2]);
  ASSERT_EQ("./bbb/AA21", ret[3]);
  ASSERT_EQ("./bbb/b21", ret[4]);
  ASSERT_EQ("./bbb/b21/A321", ret[5]);
  ASSERT_EQ("./bbb/b21/D", ret[6]);

  // ret = testGlobStar("**/**/**/./**");
  // ASSERT_EQ(7, ret.size());

  ret = testGlobStar("**/");
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("bbb/", ret[0]);
  ASSERT_EQ("bbb/b21/", ret[1]);

  ret = testGlobStar("./**/");
  ASSERT_EQ(3, ret.size());
  ASSERT_EQ("./", ret[0]);
  ASSERT_EQ("./bbb/", ret[1]);
  ASSERT_EQ("./bbb/b21/", ret[2]);
}

TEST(GlobStarTest, globstar2) { // for globstar
  auto ret = testGlobStar("**/*");
  ASSERT_EQ(6, ret.size());
  ASSERT_EQ("AAA", ret[0]);
  ASSERT_EQ("bbb", ret[1]);
  ASSERT_EQ("bbb/AA21", ret[2]);
  ASSERT_EQ("bbb/b21", ret[3]);
  ASSERT_EQ("bbb/b21/A321", ret[4]);
  ASSERT_EQ("bbb/b21/D", ret[5]);

  ret = testGlobStar("./**/*");
  ASSERT_EQ(6, ret.size());
  ASSERT_EQ("./AAA", ret[0]);
  ASSERT_EQ("./bbb", ret[1]);
  ASSERT_EQ("./bbb/AA21", ret[2]);
  ASSERT_EQ("./bbb/b21", ret[3]);
  ASSERT_EQ("./bbb/b21/A321", ret[4]);
  ASSERT_EQ("./bbb/b21/D", ret[5]);

  ret = testGlobStar("./**/*/*");
  ASSERT_EQ(4, ret.size());
  ASSERT_EQ("./bbb/AA21", ret[0]);
  ASSERT_EQ("./bbb/b21", ret[1]);
  ASSERT_EQ("./bbb/b21/A321", ret[2]);
  ASSERT_EQ("./bbb/b21/D", ret[3]);

  ret = testGlobStar("./**/*/*/*");
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("./bbb/b21/A321", ret[0]);
  ASSERT_EQ("./bbb/b21/D", ret[1]);

  ret = testGlobStar("**/b*");
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("bbb", ret[0]);
  ASSERT_EQ("bbb/b21", ret[1]);

  ret = testGlobStar("./**/A*");
  ASSERT_EQ(3, ret.size());
  ASSERT_EQ("./AAA", ret[0]);
  ASSERT_EQ("./bbb/AA21", ret[1]);
  ASSERT_EQ("./bbb/b21/A321", ret[2]);
}

TEST(GlobStarTest, globstar3) {
  auto ret = testGlobStar("**/b*/**");
  ASSERT_EQ(4, ret.size());
  ASSERT_EQ("bbb/AA21", ret[0]);
  ASSERT_EQ("bbb/b21", ret[1]);
  ASSERT_EQ("bbb/b21/A321", ret[2]);
  ASSERT_EQ("bbb/b21/D", ret[3]);

  ret = testGlobStar("**/b*/././**");
  ASSERT_EQ(4, ret.size());
  ASSERT_EQ("bbb/././AA21", ret[0]);
  ASSERT_EQ("bbb/././b21", ret[1]);
  ASSERT_EQ("bbb/b21/././A321", ret[2]);
  ASSERT_EQ("bbb/b21/././D", ret[3]);

  ret = testGlobStar("**/b*/../**");
  ASSERT_EQ(4, ret.size());
  ASSERT_EQ("bbb/../AAA", ret[0]);
  ASSERT_EQ("bbb/../bbb", ret[1]);
  ASSERT_EQ("bbb/b21/../AA21", ret[2]);
  ASSERT_EQ("bbb/b21/../b21", ret[3]);

  ret = testGlobStar("**/b*/../../**");
  ASSERT_EQ(6, ret.size());
  ASSERT_EQ("bbb/../../CMakeLists.txt", ret[0]);
  ASSERT_EQ("bbb/../../dir", ret[1]);
  ASSERT_EQ("bbb/../../glob_test.cpp", ret[2]);
  ASSERT_EQ("bbb/../../globstar_test.cpp", ret[3]);
  ASSERT_EQ("bbb/b21/../../AAA", ret[4]);
  ASSERT_EQ("bbb/b21/../../bbb", ret[5]);

  ret = testGlobStar("**/b*/../*/../**");
  ASSERT_EQ(4, ret.size());
  ASSERT_EQ("bbb/../bbb/../AAA", ret[0]);
  ASSERT_EQ("bbb/../bbb/../bbb", ret[1]);
  ASSERT_EQ("bbb/b21/../b21/../AA21", ret[2]);
  ASSERT_EQ("bbb/b21/../b21/../b21", ret[3]);

  ret = testGlobStar("**/b*/../**/../**");
  ASSERT_EQ(4, ret.size());
  ASSERT_EQ("bbb/../bbb/../AAA", ret[0]);
  ASSERT_EQ("bbb/../bbb/../bbb", ret[1]);
  ASSERT_EQ("bbb/b21/../b21/../AA21", ret[2]);
  ASSERT_EQ("bbb/b21/../b21/../b21", ret[3]);

  ret = testGlobStar("**/b*/../**/../**/../**");
  ASSERT_EQ(4, ret.size());
  ASSERT_EQ("bbb/../bbb/../bbb/../AAA", ret[0]);
  ASSERT_EQ("bbb/../bbb/../bbb/../bbb", ret[1]);
  ASSERT_EQ("bbb/b21/../b21/../b21/../AA21", ret[2]);
  ASSERT_EQ("bbb/b21/../b21/../b21/../b21", ret[3]);

  ret = testGlobStar("../**/dir/*");
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("../dir/AAA", ret[0]);
  ASSERT_EQ("../dir/bbb", ret[1]);

  ret = testGlobStar("../**/dir/*");
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("../dir/AAA", ret[0]);
  ASSERT_EQ("../dir/bbb", ret[1]);
}

int main(int argc, char **argv) {
  if (chdir(GLOB_TEST_WORK_DIR) == -1) {
    fatal_perror("broken directory: %s", GLOB_TEST_WORK_DIR);
  }

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}