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

// for testing
static GlobPatternScanner::Status matchPatternRaw(const char *name, const char *p,
                                                  Glob::Option option = {}) {
  const StringRef ref = p;
  GlobPatternScanner scanner(ref.begin(), ref.end());
  return scanner.match(name, option);
}

static bool matchPattern(const char *name, const char *p, Glob::Option option = {}) {
  const auto s = matchPatternRaw(name, p, option);
  return s != GlobPatternScanner::Status::UNMATCHED && s != GlobPatternScanner::Status::BAD_PATTERN;
}

struct CharSetResult {
  GlobPatternScanner::CharSetStatus status{GlobPatternScanner::CharSetStatus::UNMATCH};
  std::string err;
};

static CharSetResult matchCharSet(const int codePoint, const char *pattern) {
  const StringRef p = pattern;
  GlobPatternScanner scanner(p.begin(), p.end());
  CharSetResult ret;
  ret.status = scanner.matchCharSet(codePoint, &ret.err);
  return ret;
}

class Consumer {
private:
  std::vector<std::string> &ret;

public:
  explicit Consumer(std::vector<std::string> &ret) : ret(ret) { this->ret.clear(); }

  bool operator()(std::string &&value) {
    this->ret.push_back(std::move(value));
    std::sort(this->ret.begin(), this->ret.end());
    return true;
  }
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

  unsigned int testGlobBase(const char *dir, const char *pattern, Glob::Option option = {}) {
    CancelToken cancel;
    Glob glob(pattern, option, dir);
    glob.setCancelToken(cancel);
    glob.setConsumer(Consumer(this->ret));
    glob.matchExactly();
    return glob.getMatchCount();
  }

  unsigned int testGlob(const char *pattern, Glob::Option option = {}) {
    return this->testGlobAt(nullptr, pattern, option);
  }

  unsigned int testGlobAt(const char *baseDir, const char *pattern, Glob::Option option = {}) {
    CancelToken cancel;
    Glob glob(pattern, option, baseDir);
    glob.setCancelToken(cancel);
    glob.setConsumer(Consumer(this->ret));
    glob();
    return glob.getMatchCount();
  }

  static void checkCharSet(const char *pattern) {
    const CharSetResult expect = {
        .status = GlobPatternScanner::CharSetStatus::UNMATCH,
        .err = "",
    };
    checkCharSet(pattern, expect);
  }

  static void checkCharSet(const char *pattern, const CharSetResult &expect) {
    const auto ret = matchCharSet(0, pattern);
    ASSERT_EQ(expect.err, ret.err);
    ASSERT_EQ(expect.status, ret.status);
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
  ASSERT_TRUE(matchPattern("\xFE", "?"));
  ASSERT_TRUE(matchPattern("あ", "?"));

  ASSERT_TRUE(matchPattern(
      "hoge",
      "********************************************************************************************"
      "********************************************************************************************"
      "********************************************************************************************"
      "********************************************************************************************"
      "********************************************************************************************"
      "********************************************************************************************"
      "***************************************************?*"));
}

TEST_F(GlobTest, pattern2) {
  ASSERT_EQ(GlobPatternScanner::Status::DOT, matchPatternRaw(".", "."));
  ASSERT_EQ(GlobPatternScanner::Status::DOT, matchPatternRaw(".", ".", Glob::Option::DOTGLOB));
  ASSERT_FALSE(matchPattern(".", "*"));
  ASSERT_FALSE(matchPattern(".", "*", Glob::Option::DOTGLOB));
  ASSERT_FALSE(matchPattern(".conf", "*"));
  ASSERT_TRUE(matchPattern(".conf", "*", Glob::Option::DOTGLOB));
  ASSERT_FALSE(matchPattern(".", "?"));
  ASSERT_FALSE(matchPattern(".", "?", Glob::Option::DOTGLOB));
  ASSERT_EQ(GlobPatternScanner::Status::DOTDOT, matchPatternRaw("..", ".."));
  ASSERT_EQ(GlobPatternScanner::Status::DOTDOT, matchPatternRaw("..", "..", Glob::Option::DOTGLOB));
  ASSERT_FALSE(matchPattern("..", "*"));
  ASSERT_FALSE(matchPattern("..", "*", Glob::Option::DOTGLOB));
  ASSERT_FALSE(matchPattern("..", "*?"));
  ASSERT_FALSE(matchPattern("..", "*?", Glob::Option::DOTGLOB));
  ASSERT_FALSE(matchPattern("..", "?*"));
  ASSERT_FALSE(matchPattern("..", "?*", Glob::Option::DOTGLOB));
  ASSERT_FALSE(matchPattern(".hoge", "*?"));
  ASSERT_TRUE(matchPattern(".hoge", "*?", Glob::Option::DOTGLOB));
  ASSERT_FALSE(matchPattern("..", ".*?"));
  ASSERT_FALSE(matchPattern("..", ".*?", Glob::Option::DOTGLOB));
  ASSERT_TRUE(matchPattern(".hoge", ".?*"));
  ASSERT_TRUE(matchPattern(".hoge", ".?*", Glob::Option::DOTGLOB));
  ASSERT_FALSE(matchPattern("h.log", "h."));
  ASSERT_FALSE(matchPattern("h.log", "h.."));
  ASSERT_FALSE(matchPattern("", "."));
  ASSERT_FALSE(matchPattern("", ".."));
  ASSERT_FALSE(matchPattern("hgoe", "."));
  ASSERT_FALSE(matchPattern("huga", ".."));
}

TEST_F(GlobTest, pattern3) {
  ASSERT_TRUE(matchPattern("*", "*"));
  ASSERT_TRUE(matchPattern("12", "*"));
  ASSERT_TRUE(matchPattern("*", "\\*"));
  ASSERT_FALSE(matchPattern("12", "\\*"));
  ASSERT_TRUE(matchPattern("\\*", "\\\\*"));
  ASSERT_TRUE(matchPattern("\\34", "\\\\*"));

  ASSERT_TRUE(matchPattern("?", "?"));
  ASSERT_TRUE(matchPattern("2", "?"));
  ASSERT_TRUE(matchPattern("?", "\\?"));
  ASSERT_FALSE(matchPattern("2", "\\?"));
  ASSERT_TRUE(matchPattern("\\?", "\\\\?"));
  ASSERT_TRUE(matchPattern("\\\\", "\\\\?"));
  ASSERT_TRUE(matchPattern("\\@", "\\\\?"));
}

TEST_F(GlobTest, charset1) {
  ASSERT_NO_FATAL_FAILURE(checkCharSet("", {GlobPatternScanner::CharSetStatus::SYNTAX_ERROR,
                                            "bracket expression must start with `['"}));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("1", {GlobPatternScanner::CharSetStatus::SYNTAX_ERROR,
                                             "bracket expression must start with `['"}));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[!", {GlobPatternScanner::CharSetStatus::SYNTAX_ERROR,
                                              "bracket expression must end with `]'"}));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[[", {GlobPatternScanner::CharSetStatus::SYNTAX_ERROR,
                                              "bracket expression must end with `]'"}));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[a-z", {GlobPatternScanner::CharSetStatus::SYNTAX_ERROR,
                                                "bracket expression must end with `]'"}));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[]"));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[1]"));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[-]"));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[-12]"));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[^-]", {GlobPatternScanner::CharSetStatus::MATCH, ""}));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[!-]", {GlobPatternScanner::CharSetStatus::MATCH, ""}));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[12-]"));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[---]"));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[^12-]", {GlobPatternScanner::CharSetStatus::MATCH, ""}));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[!12-]", {GlobPatternScanner::CharSetStatus::MATCH, ""}));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[^]]", {GlobPatternScanner::CharSetStatus::MATCH, ""}));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[!]]", {GlobPatternScanner::CharSetStatus::MATCH, ""}));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[^]", {GlobPatternScanner::CharSetStatus::SYNTAX_ERROR,
                                               "bracket expression must end with `]'"}));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[!]", {GlobPatternScanner::CharSetStatus::SYNTAX_ERROR,
                                               "bracket expression must end with `]'"}));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[a--]", {GlobPatternScanner::CharSetStatus::SYNTAX_ERROR,
                                                 "character range is out of order"}));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[z-a]", {GlobPatternScanner::CharSetStatus::SYNTAX_ERROR,
                                                 "character range is out of order"}));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[a-z_0-9]"));

  ASSERT_NO_FATAL_FAILURE(checkCharSet("[\\]]"));
  ASSERT_NO_FATAL_FAILURE(checkCharSet("[z\\-a]"));
  ASSERT_NO_FATAL_FAILURE(checkCharSet(
      "[12\\", {GlobPatternScanner::CharSetStatus::SYNTAX_ERROR, "need character after `\\'"}));
  ASSERT_NO_FATAL_FAILURE(checkCharSet(
      "[12\xFF", {GlobPatternScanner::CharSetStatus::SYNTAX_ERROR, "invalid utf-8 sequence"}));
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

  s = testGlobBase(".", "*/./*", Glob::Option::DOTGLOB);
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

  s = testGlob("bbb/*", Glob::Option::DOTGLOB);
  ASSERT_EQ(3, s);
  ASSERT_EQ(3, ret.size());
  ASSERT_EQ("bbb/.hidden", ret[0]);
  ASSERT_EQ("bbb/AA21", ret[1]);
  ASSERT_EQ("bbb/b21", ret[2]);

  s = testGlob("bbb///*//", Glob::Option::DOTGLOB);
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
  TempFileFactory tempFileFactory("arsh_glob");
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

  s = testGlobAt(GLOB_TEST_WORK_DIR, "bbb/*", Glob::Option::DOTGLOB);
  ASSERT_EQ(3, s);
  ASSERT_EQ(3, ret.size());
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/bbb/.hidden", ret[0]);
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/bbb/AA21", ret[1]);
  ASSERT_EQ(GLOB_TEST_WORK_DIR "/bbb/b21", ret[2]);

  s = testGlobAt(GLOB_TEST_WORK_DIR, "bbb///*//", Glob::Option::DOTGLOB);
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

TEST_F(GlobTest, escapeDir) {
  Glob glob("/hogehoge/\\*234\\?ss////*", {}, nullptr);
  ASSERT_EQ(Glob::Status::NOMATCH, glob());
  ASSERT_EQ("/hogehoge/*234?ss/", glob.getBaseDir());
}

TEST_F(GlobTest, fast) {
  if (chdir(GLOB_TEST_WORK_DIR) == -1) {
    fatal_perror("broken directory: %s", GLOB_TEST_WORK_DIR);
  }
  ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(isSameFile(getCWD().get(), GLOB_TEST_WORK_DIR)));

  auto s = testGlob("*", Glob::Option::FASTGLOB);
  ASSERT_EQ(2, s);
  ASSERT_EQ(2, ret.size());
  ASSERT_EQ("AAA", ret[0]);
  ASSERT_EQ("bbb", ret[1]);

  s = testGlob("*/*1/../", Glob::Option::FASTGLOB);
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("bbb/b21/../", ret[0]);

  s = testGlob("*/*1/..", Glob::Option::FASTGLOB);
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("bbb/b21/..", ret[0]);

  s = testGlob("*/../A*", Glob::Option::FASTGLOB);
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("AAA", ret[0]);

  s = testGlob("./*/../A*", Glob::Option::FASTGLOB);
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("./AAA", ret[0]);

  s = testGlob("*/../*/", Glob::Option::FASTGLOB);
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("bbb/", ret[0]);

  s = testGlob("*/../*/*", Glob::Option::FASTGLOB | Glob::Option::DOTGLOB);
  ASSERT_EQ(3, s);
  ASSERT_EQ(3, ret.size());
  ASSERT_EQ("bbb/.hidden", ret[0]);
  ASSERT_EQ("bbb/AA21", ret[1]);
  ASSERT_EQ("bbb/b21", ret[2]);

  s = testGlob("../*/", Glob::Option::FASTGLOB);
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("../dir/", ret[0]);

  s = testGlob("*/../../d*", Glob::Option::FASTGLOB);
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("../dir", ret[0]);

  s = testGlob("*/../../../g*/d*/", Glob::Option::FASTGLOB);
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("../../glob/dir/", ret[0]);

  s = testGlob("*/../../../../t*/g*/d*", Glob::Option::FASTGLOB);
  ASSERT_EQ(1, s);
  ASSERT_EQ(1, ret.size());
  ASSERT_EQ("../../../test/glob/dir", ret[0]);
}

TEST_F(GlobTest, fail) {
  const char *pattern = "bbb/*";
  Glob glob(pattern, Glob::Option::DOTGLOB, GLOB_TEST_WORK_DIR);
  glob.setConsumer([&](std::string &&value) {
    if (this->ret.size() == 2) {
      return false;
    }
    this->ret.push_back(std::move(value));
    return true;
  });
  auto s = glob.matchExactly();
  ASSERT_EQ(Glob::Status::LIMIT, s);
}

struct AlwaysCancel : CancelToken {
  bool operator()() override { return true; }
};

TEST_F(GlobTest, cancel) {
  const char *pattern = "bbb/*";
  Glob glob(pattern, Glob::Option::DOTGLOB, GLOB_TEST_WORK_DIR);
  AlwaysCancel cancel;
  glob.setCancelToken(cancel);
  glob.setConsumer([&](std::string &&value) {
    this->ret.push_back(std::move(value));
    return true;
  });
  auto s = glob.matchExactly();
  ASSERT_EQ(Glob::Status::CANCELED, s);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}