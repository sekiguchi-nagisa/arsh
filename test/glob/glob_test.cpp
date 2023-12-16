#include <algorithm>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include <misc/glob.hpp>
#include <misc/string_ref.hpp>

#ifndef GLOB_TEST_WORK_DIR
#error "require EXEC_TEST_DIR"
#endif

using namespace arsh;

// for testing
struct StrMetaChar {
  static bool isAny(const char *iter) { return *iter == '?'; }

  static bool isZeroOrMore(const char *iter) { return *iter == '*'; }
};

static WildMatchResult matchPatternRaw(const char *name, const char *p,
                                       GlobMatchOption option = {}) {
  return createWildCardMatcher<StrMetaChar>(p, p + strlen(p), option)(name);
}

static bool matchPattern(const char *name, const char *p, GlobMatchOption option = {}) {
  return matchPatternRaw(name, p, option) != WildMatchResult::FAILED;
}

struct Appender {
  std::vector<std::string> &ref;

  explicit Appender(std::vector<std::string> &value) : ref(value) { this->ref.clear(); }

  bool operator()(std::string &&path) {
    this->ref.push_back(path);
    std::sort(this->ref.begin(), this->ref.end());
    return true;
  }
};

struct CancelPoint {
  bool operator()() { return false; }
};

class GlobTest : public ::testing::Test {
protected:
  std::vector<std::string> ret; // result paths

public:
  GlobTest() {
    if (chdir(GLOB_TEST_WORK_DIR) == -1) {
      fatal_perror("broken directory: %s", GLOB_TEST_WORK_DIR);
    }
  }

  unsigned int testGlobBase(const char *dir, const char *pattern, GlobMatchOption option = {}) {
    Appender appender(this->ret);
    auto matcher = createGlobMatcher<StrMetaChar>(dir, pattern, pattern + strlen(pattern),
                                                  CancelPoint(), option);
    matcher.matchExactly(appender);
    return matcher.getMatchCount();
  }

  unsigned int testGlob(const char *pattern, GlobMatchOption option = {}) {
    return this->testGlobAt(nullptr, pattern, option);
  }

  unsigned int testGlobAt(const char *baseDir, const char *pattern, GlobMatchOption option = {}) {
    Appender appender(this->ret);
    auto matcher = createGlobMatcher<StrMetaChar>(baseDir, pattern, pattern + strlen(pattern),
                                                  CancelPoint(), option);
    matcher([](std::string &) { return true; }, appender);
    return matcher.getMatchCount();
  }
};

TEST_F(GlobTest, pattern1) {
  ASSERT_FALSE(matchPattern("", "ff"));
  ASSERT_FALSE(matchPattern("", "?"));
  ASSERT_TRUE(matchPattern("", "*"));
  ASSERT_TRUE(matchPattern("", "*/"));
  ASSERT_TRUE(matchPattern("", ""));
  ASSERT_TRUE(matchPattern("", "/"));
  ASSERT_TRUE(matchPattern("hoge", "hoge"));
  ASSERT_TRUE(matchPattern("hoge", "hoge/"));
  ASSERT_FALSE(matchPattern("hoge", ""));
  ASSERT_FALSE(matchPattern("hoge", "h"));
  ASSERT_FALSE(matchPattern("hoge", "wwww"));
  ASSERT_FALSE(matchPattern("hoge", "h*g"));
  ASSERT_TRUE(matchPattern("hoge", "*h*ge"));
  ASSERT_TRUE(matchPattern("hoge", "*h*ge/"));
  ASSERT_FALSE(matchPattern("hoge", "?"));
  ASSERT_TRUE(matchPattern("hoge", "????"));
  ASSERT_TRUE(matchPattern("hoge", "?*"));
  ASSERT_TRUE(matchPattern("hoge", "*"));
  ASSERT_TRUE(matchPattern("hoge", "*/"));
  ASSERT_FALSE(matchPattern("hoge", "h*gw"));
  ASSERT_FALSE(matchPattern("hoge", "g*w"));
  ASSERT_TRUE(matchPattern("hoge", "h*ge*"));
  ASSERT_TRUE(matchPattern("hoge", "h**g**e*"));
  ASSERT_TRUE(matchPattern("hoge", "h**g**e*/A/A"));
  ASSERT_TRUE(matchPattern("hoge", "h*ge**"));
  ASSERT_FALSE(matchPattern("hoge", "h*ge?"));
  ASSERT_FALSE(matchPattern("hoge", "h*ge**?"));
  ASSERT_FALSE(matchPattern("hoge", "h*ge**?/"));
  ASSERT_TRUE(matchPattern("hoge", "h*ge**/AAA"));
}

TEST_F(GlobTest, pattern2) {
  ASSERT_EQ(WildMatchResult::DOT, matchPatternRaw(".", "."));
  ASSERT_EQ(WildMatchResult::DOT, matchPatternRaw(".", ".", GlobMatchOption::DOTGLOB));
  ASSERT_FALSE(matchPattern(".", "*"));
  ASSERT_FALSE(matchPattern(".", "*", GlobMatchOption::DOTGLOB));
  ASSERT_FALSE(matchPattern(".conf", "*"));
  ASSERT_TRUE(matchPattern(".conf", "*", GlobMatchOption::DOTGLOB));
  ASSERT_FALSE(matchPattern(".", "?"));
  ASSERT_FALSE(matchPattern(".", "?", GlobMatchOption::DOTGLOB));
  ASSERT_EQ(WildMatchResult::DOTDOT, matchPatternRaw("..", ".."));
  ASSERT_EQ(WildMatchResult::DOTDOT, matchPatternRaw("..", "..", GlobMatchOption::DOTGLOB));
  ASSERT_FALSE(matchPattern("..", "*"));
  ASSERT_FALSE(matchPattern("..", "*", GlobMatchOption::DOTGLOB));
  ASSERT_FALSE(matchPattern("..", "*?"));
  ASSERT_FALSE(matchPattern("..", "*?", GlobMatchOption::DOTGLOB));
  ASSERT_FALSE(matchPattern("..", "?*"));
  ASSERT_FALSE(matchPattern("..", "?*", GlobMatchOption::DOTGLOB));
  ASSERT_FALSE(matchPattern(".hoge", "*?"));
  ASSERT_TRUE(matchPattern(".hoge", "*?", GlobMatchOption::DOTGLOB));
  ASSERT_FALSE(matchPattern("..", ".*?"));
  ASSERT_FALSE(matchPattern("..", ".*?", GlobMatchOption::DOTGLOB));
  ASSERT_TRUE(matchPattern(".hoge", ".?*"));
  ASSERT_TRUE(matchPattern(".hoge", ".?*", GlobMatchOption::DOTGLOB));
  ASSERT_FALSE(matchPattern("h.log", "h."));
  ASSERT_FALSE(matchPattern("h.log", "h.."));
  ASSERT_FALSE(matchPattern("", "."));
  ASSERT_FALSE(matchPattern("", ".."));
  ASSERT_FALSE(matchPattern("hgoe", "."));
  ASSERT_FALSE(matchPattern("huga", ".."));
}

// test `globBase' api

TEST_F(GlobTest, base_invalid) { // invalid base dir
  auto s = testGlobBase("", "");
  ASSERT_EQ(0, s);
  ASSERT_TRUE(ret.empty());

  s = testGlobBase("", "*");
  ASSERT_EQ(0, s);
  ASSERT_TRUE(ret.empty());

  s = testGlobBase("", "AAA");
  ASSERT_EQ(0, s);
  ASSERT_TRUE(ret.empty());

  s = testGlobBase("hfuierhtfnv", "*");
  ASSERT_EQ(0, s);
  ASSERT_TRUE(ret.empty());
}

TEST_F(GlobTest, base_fileOrDir1) { // match file or dir
  auto s = testGlobBase(".", "*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("AAA", ret[0]);
  ASSERT_EQ("bbb", ret[1]);

  s = testGlobBase("./", "*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("./AAA", ret[0]);
  ASSERT_EQ("./bbb", ret[1]);

  s = testGlobBase(".//", "*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ(".//AAA", ret[0]);
  ASSERT_EQ(".//bbb", ret[1]);

  s = testGlobBase(".///", "*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ(".///AAA", ret[0]);
  ASSERT_EQ(".///bbb", ret[1]);

  s = testGlobBase("././/.//", "*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("././/.//AAA", ret[0]);
  ASSERT_EQ("././/.//bbb", ret[1]);

  s = testGlobBase("././/.//", "*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("././/.//AAA", ret[0]);
  ASSERT_EQ("././/.//bbb", ret[1]);

  s = testGlobBase(".", "*/../*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("bbb/../AAA", ret[0]);
  ASSERT_EQ("bbb/../bbb", ret[1]);

  s = testGlobBase(".", "*/./*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("bbb/./AA21", ret[0]);
  ASSERT_EQ("bbb/./b21", ret[1]);

  s = testGlobBase(".", "*/./.?*");
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("bbb/./.hidden", ret[0]);

  s = testGlobBase(".", "*/./*", GlobMatchOption::DOTGLOB);
  ASSERT_EQ(3, s);
  ASSERT_EQ(3, ret.size());
  ASSERT_EQ("bbb/./.hidden", ret[0]);
  ASSERT_EQ("bbb/./AA21", ret[1]);
  ASSERT_EQ("bbb/./b21", ret[2]);
}

TEST_F(GlobTest, base_fileOrDir2) { // match file or dir
  auto s = testGlobBase(".", "bbb/*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("bbb/AA21", ret[0]);
  ASSERT_EQ("bbb/b21", ret[1]);

  s = testGlobBase(".", "./././bbb/?*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("./././bbb/AA21", ret[0]);
  ASSERT_EQ("./././bbb/b21", ret[1]);

  s = testGlobBase("./", "*//*/*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("./bbb/b21/A321", ret[0]);
  ASSERT_EQ("./bbb/b21/D", ret[1]);
}

TEST_F(GlobTest, base_fileOrDir3) { // match file or dir
  auto s = testGlobBase("bbb", "*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("bbb/AA21", ret[0]);
  ASSERT_EQ("bbb/b21", ret[1]);

  s = testGlobBase("bbb", "*/");
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("bbb/b21/", ret[0]);

  s = testGlobBase("bbb/", "*/");
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("bbb/b21/", ret[0]);
}

TEST_F(GlobTest, base_onlyDir) { // match only dir
  auto s = testGlobBase("./", "*/");
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("./bbb/", ret[0]);

  s = testGlobBase(".", "*//");
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("bbb/", ret[0]);

  s = testGlobBase(".", "*////");
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("bbb/", ret[0]);

  s = testGlobBase(".", "*/.");
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("bbb/.", ret[0]);

  s = testGlobBase(".", "*/..");
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("bbb/..", ret[0]);

  s = testGlobBase("./", "*//.//..");
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("./bbb/./..", ret[0]);

  s = testGlobBase(".", "*//*/");
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("bbb/b21/", ret[0]);

  s = testGlobBase(".", "*//*/*/");
  ASSERT_EQ(0, s);
  ASSERT_TRUE(ret.empty());

  s = testGlobBase(".", "*//*./");
  ASSERT_EQ(0, s);
  ASSERT_TRUE(ret.empty());
}

TEST_F(GlobTest, base_fullpath) {
  auto s = testGlobBase(GLOB_TEST_WORK_DIR, "bbb/*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/bbb/AA21", ret[0]);
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/bbb/b21", ret[1]);

  s = testGlobBase(GLOB_TEST_WORK_DIR, "../*/bbb/*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/../dir/bbb/AA21", ret[0]);
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/../dir/bbb/b21", ret[1]);

  s = testGlobBase(GLOB_TEST_WORK_DIR, "..//d*///*//*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/../dir/bbb/AA21", ret[0]);
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/../dir/bbb/b21", ret[1]);
}

TEST_F(GlobTest, glob) {
  auto s = testGlob("*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("AAA", ret[0]);
  ASSERT_EQ("bbb", ret[1]);

  s = testGlob("b*");
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("bbb", ret[0]);

  s = testGlob("./././*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("./././AAA", ret[0]);
  ASSERT_EQ("./././bbb", ret[1]);

  s = testGlob("bbb/*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("bbb/AA21", ret[0]);
  ASSERT_EQ("bbb/b21", ret[1]);

  s = testGlob("bbb/*", GlobMatchOption::DOTGLOB);
  ASSERT_EQ(3, s);
  ASSERT_EQ(3, ret.size());
  ASSERT_EQ("bbb/.hidden", ret[0]);
  ASSERT_EQ("bbb/AA21", ret[1]);
  ASSERT_EQ("bbb/b21", ret[2]);

  s = testGlob("bbb///*//", GlobMatchOption::DOTGLOB);
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("bbb/b21/", ret[0]);

  s = testGlob("bbb/././/./*/");
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("bbb/./././b21/", ret[0]);

  s = testGlob("bbb/*2?");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("bbb/AA21", ret[0]);
  ASSERT_EQ("bbb/b21", ret[1]);

  s = testGlob("bbb/A*");
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("bbb/AA21", ret[0]);

  s = testGlob("bbb/A*/");
  ASSERT_EQ(0, s);
  ASSERT_EQ(0, ret.size());

  // empty directory
  TempFileFactory tempFileFactory("ydsh_glob");
  std::string path = tempFileFactory.getTempDirName();
  path += "/hogehoge";
  mkdir(path.c_str(), 0666);
  std::string p = tempFileFactory.getTempDirName();
  p += "/*/..";
  s = testGlob(p.c_str());
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ(path + "/..", ret[0]);
}

TEST_F(GlobTest, globAt) {
  if (chdir(GLOB_TEST_WORK_DIR "/../../") == -1) {
    fatal_perror("broken directory: %s", GLOB_TEST_WORK_DIR);
  }
  ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(isSameFile(getCWD().get(), GLOB_TEST_WORK_DIR)));

  auto s = testGlobAt(GLOB_TEST_WORK_DIR, "*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/./AAA", ret[0]);
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/./bbb", ret[1]);

  s = testGlobAt(GLOB_TEST_WORK_DIR, "b*");
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/./bbb", ret[0]);

  s = testGlobAt(GLOB_TEST_WORK_DIR, "./././*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/./././AAA", ret[0]);
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/./././bbb", ret[1]);

  s = testGlobAt(GLOB_TEST_WORK_DIR, "bbb/*");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/bbb/AA21", ret[0]);
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/bbb/b21", ret[1]);

  s = testGlobAt(GLOB_TEST_WORK_DIR, "bbb/*", GlobMatchOption::DOTGLOB);
  ASSERT_EQ(3, s);
  ASSERT_EQ(3, ret.size());
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/bbb/.hidden", ret[0]);
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/bbb/AA21", ret[1]);
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/bbb/b21", ret[2]);

  s = testGlobAt(GLOB_TEST_WORK_DIR, "bbb///*//", GlobMatchOption::DOTGLOB);
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/bbb/b21/", ret[0]);

  s = testGlobAt(GLOB_TEST_WORK_DIR, "bbb/././/./*/");
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/bbb/./././b21/", ret[0]);

  s = testGlobAt(GLOB_TEST_WORK_DIR, "bbb/*2?");
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/bbb/AA21", ret[0]);
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/bbb/b21", ret[1]);

  s = testGlobAt(GLOB_TEST_WORK_DIR, "bbb/A*");
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/bbb/AA21", ret[0]);

  s = testGlobAt(GLOB_TEST_WORK_DIR, "bbb/A*/");
  ASSERT_EQ(0, s);
  ASSERT_EQ(0, ret.size());
}

TEST_F(GlobTest, fast) {
  if (chdir(GLOB_TEST_WORK_DIR) == -1) {
    fatal_perror("broken directory: %s", GLOB_TEST_WORK_DIR);
  }
  ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(isSameFile(getCWD().get(), GLOB_TEST_WORK_DIR)));

  auto s = testGlob("*", GlobMatchOption::FASTGLOB);
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("AAA", ret[0]);
  ASSERT_EQ("bbb", ret[1]);

  s = testGlob("*/*1/../", GlobMatchOption::FASTGLOB);
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("bbb/b21/../", ret[0]);

  s = testGlob("*/*1/..", GlobMatchOption::FASTGLOB);
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("bbb/b21/..", ret[0]);

  s = testGlob("*/../A*", GlobMatchOption::FASTGLOB);
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("AAA", ret[0]);

  s = testGlob("./*/../A*", GlobMatchOption::FASTGLOB);
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("./AAA", ret[0]);

  s = testGlob("*/../*/", GlobMatchOption::FASTGLOB);
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("bbb/", ret[0]);

  s = testGlob("*/../*/*", GlobMatchOption::FASTGLOB | GlobMatchOption::DOTGLOB);
  ASSERT_EQ(3, s);
  ASSERT_EQ(3, ret.size());
  ASSERT_EQ("bbb/.hidden", ret[0]);
  ASSERT_EQ("bbb/AA21", ret[1]);
  ASSERT_EQ("bbb/b21", ret[2]);

  s = testGlob("../*/", GlobMatchOption::FASTGLOB);
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("../dir/", ret[0]);

  s = testGlob("*/../../d*", GlobMatchOption::FASTGLOB);
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("../dir", ret[0]);

  s = testGlob("*/../../../g*/d*/", GlobMatchOption::FASTGLOB);
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("../../glob/dir/", ret[0]);

  s = testGlob("*/../../../../t*/g*/d*", GlobMatchOption::FASTGLOB);
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("../../../test/glob/dir", ret[0]);
}

TEST_F(GlobTest, fail) {
  const char *pattern = "bbb/*";
  auto matcher =
      createGlobMatcher<StrMetaChar>(GLOB_TEST_WORK_DIR, pattern, pattern + strlen(pattern),
                                     CancelPoint(), GlobMatchOption::DOTGLOB);
  auto appender = [&](std::string &&path) {
    if (this->ret.size() == 2) {
      return false;
    }
    this->ret.push_back(std::move(path));
    return true;
  };
  auto s = matcher.matchExactly(appender);
  ASSERT_EQ(GlobMatchResult::LIMIT, s);
}

TEST_F(GlobTest, cancel) {
  const char *pattern = "bbb/*";
  auto matcher = createGlobMatcher<StrMetaChar>(
      GLOB_TEST_WORK_DIR, pattern, pattern + strlen(pattern), [] { return true; },
      GlobMatchOption::DOTGLOB);
  auto appender = [&](std::string &&path) {
    this->ret.push_back(std::move(path));
    return true;
  };
  auto s = matcher.matchExactly(appender);
  ASSERT_EQ(GlobMatchResult::CANCELED, s);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}