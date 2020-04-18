#include <string>
#include <algorithm>

#include "gtest/gtest.h"

#include <misc/glob.hpp>
#include <misc/string_ref.hpp>

#ifndef GLOB_TEST_WORK_DIR
#error "require EXEC_TEST_DIR"
#endif

using namespace ydsh;

// for testing
struct StrMetaChar {
    static bool isAny(const char *iter) {
        return *iter == '?';
    }

    static bool isZeroOrMore(const char *iter) {
        return *iter == '*';
    }
};

static bool matchPattern(const char *name, const char *p, WildMatchOption option = {}) {
    auto matcher = createWildCardMatcher<StrMetaChar>(p, p + strlen(p), option);
    return matcher(name) != WildMatchResult::FAILED;
}

class GlobTest : public ::testing::Test {
public:
    GlobTest() {
        if(chdir(GLOB_TEST_WORK_DIR) == -1) {
            fatal_perror("broken directory: %s", GLOB_TEST_WORK_DIR);
        }
    }
};

class Appender {
private:
    std::vector<std::string> paths;

public:
    void operator()(std::string &&path) {
        this->paths.push_back(std::move(path));
    }

    std::vector<std::string> take() {
        std::sort(this->paths.begin(), this->paths.end());
        return std::move(this->paths);
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
    ASSERT_TRUE(matchPattern(".", "."));
    ASSERT_TRUE(matchPattern(".", ".", WildMatchOption::DOTGLOB));
    ASSERT_FALSE(matchPattern(".", "*"));
    ASSERT_TRUE(matchPattern(".", "*", WildMatchOption::DOTGLOB));
    ASSERT_FALSE(matchPattern(".", "?"));
    ASSERT_TRUE(matchPattern(".", "?", WildMatchOption::DOTGLOB));
    ASSERT_FALSE(matchPattern("..", "*"));
    ASSERT_TRUE(matchPattern("..", "*", WildMatchOption::DOTGLOB));
    ASSERT_FALSE(matchPattern("..", "*?"));
    ASSERT_TRUE(matchPattern("..", "*?", WildMatchOption::DOTGLOB));
    ASSERT_FALSE(matchPattern("..", "?*"));
    ASSERT_TRUE(matchPattern("..", "?*", WildMatchOption::DOTGLOB));
    ASSERT_FALSE(matchPattern(".hoge", "*?"));
    ASSERT_TRUE(matchPattern(".hoge", "*?", WildMatchOption::DOTGLOB));
    ASSERT_TRUE(matchPattern("..", ".*?"));
    ASSERT_TRUE(matchPattern("..", ".*?", WildMatchOption::DOTGLOB));
    ASSERT_TRUE(matchPattern(".hoge", ".?*"));
    ASSERT_TRUE(matchPattern(".hoge", ".?*", WildMatchOption::DOTGLOB));
}

static std::vector<std::string> testGlobBase(const char *dir,
                                const char *pattern, WildMatchOption option = {}) {
    Appender appender;
    globBase<StrMetaChar>(dir, pattern, pattern + strlen(pattern), appender, option);
    return appender.take();
}

// test `globBase' api

TEST_F(GlobTest, base_invalid) {    // invalid base dir
    auto ret = testGlobBase("", "");
    ASSERT_TRUE(ret.empty());

    ret = testGlobBase("", "*");
    ASSERT_TRUE(ret.empty());

    ret = testGlobBase("", "AAA");
    ASSERT_TRUE(ret.empty());

    ret = testGlobBase("hfuierhtfnv", "*");
    ASSERT_TRUE(ret.empty());
}

TEST_F(GlobTest, base_fileOrDir1) {    // match file or dir
    auto ret = testGlobBase(".", "*");
    ASSERT_EQ(2, ret.size());
    ASSERT_EQ("AAA", ret[0]);
    ASSERT_EQ("bbb", ret[1]);

    ret = testGlobBase("./", "*");
    ASSERT_EQ(2, ret.size());
    ASSERT_EQ("./AAA", ret[0]);
    ASSERT_EQ("./bbb", ret[1]);

    ret = testGlobBase(".//", "*");
    ASSERT_EQ(2, ret.size());
    ASSERT_EQ(".//AAA", ret[0]);
    ASSERT_EQ(".//bbb", ret[1]);

    ret = testGlobBase(".///", "*");
    ASSERT_EQ(2, ret.size());
    ASSERT_EQ(".///AAA", ret[0]);
    ASSERT_EQ(".///bbb", ret[1]);

    ret = testGlobBase("././/.//", "*");
    ASSERT_EQ(2, ret.size());
    ASSERT_EQ("././/.//AAA", ret[0]);
    ASSERT_EQ("././/.//bbb", ret[1]);

    ret = testGlobBase("././/.//", "*");
    ASSERT_EQ(2, ret.size());
    ASSERT_EQ("././/.//AAA", ret[0]);
    ASSERT_EQ("././/.//bbb", ret[1]);

    ret = testGlobBase(".", "*/../*");
    ASSERT_EQ(2, ret.size());
    ASSERT_EQ("bbb/../AAA", ret[0]);
    ASSERT_EQ("bbb/../bbb", ret[1]);

    ret = testGlobBase(".", "*/./*");
    ASSERT_EQ(2, ret.size());
    ASSERT_EQ("bbb/./AA21", ret[0]);
    ASSERT_EQ("bbb/./b21", ret[1]);

    ret = testGlobBase(".", "*/./.?*");
    ASSERT_EQ(1, ret.size());
    ASSERT_EQ("bbb/./.hidden", ret[0]);

    ret = testGlobBase(".", "*/./*", WildMatchOption::DOTGLOB);
    ASSERT_EQ(3, ret.size());
    ASSERT_EQ("bbb/./.hidden", ret[0]);
    ASSERT_EQ("bbb/./AA21", ret[1]);
    ASSERT_EQ("bbb/./b21", ret[2]);
}

TEST_F(GlobTest, base_fileOrDir2) {    // match file or dir
    auto ret = testGlobBase(".", "bbb/*");
    ASSERT_EQ(2, ret.size());
    ASSERT_EQ("bbb/AA21", ret[0]);
    ASSERT_EQ("bbb/b21", ret[1]);

    ret = testGlobBase(".", "./././bbb/?*");
    ASSERT_EQ(2, ret.size());
    ASSERT_EQ("./././bbb/AA21", ret[0]);
    ASSERT_EQ("./././bbb/b21", ret[1]);

    ret = testGlobBase("./", "*//*/*");
    ASSERT_EQ(2, ret.size());
    ASSERT_EQ("./bbb/b21/A321", ret[0]);
    ASSERT_EQ("./bbb/b21/D", ret[1]);
}

TEST_F(GlobTest, base_onlyDir) {    // match only dir
    auto ret = testGlobBase("./", "*/");
    ASSERT_EQ(1, ret.size());
    ASSERT_EQ("./bbb/", ret[0]);

    ret = testGlobBase(".", "*//");
    ASSERT_EQ(1, ret.size());
    ASSERT_EQ("bbb/", ret[0]);

    ret = testGlobBase(".", "*////");
    ASSERT_EQ(1, ret.size());
    ASSERT_EQ("bbb/", ret[0]);

    ret = testGlobBase(".", "*/.");
    ASSERT_EQ(1, ret.size());
    ASSERT_EQ("bbb/.", ret[0]);

    ret = testGlobBase(".", "*/..");
    ASSERT_EQ(1, ret.size());
    ASSERT_EQ("bbb/..", ret[0]);

    ret = testGlobBase("./", "*//.//..");
    ASSERT_EQ(1, ret.size());
    ASSERT_EQ("./bbb/./..", ret[0]);

    ret = testGlobBase(".", "*//*/");
    ASSERT_EQ(1, ret.size());
    ASSERT_EQ("bbb/b21/", ret[0]);

    ret = testGlobBase(".", "*//*/*/");
    ASSERT_TRUE(ret.empty());

    ret = testGlobBase(".", "*//*./");
    ASSERT_TRUE(ret.empty());
}

TEST_F(GlobTest, base_fullpath) {
    fprintf(stderr, "%s\n", GLOB_TEST_WORK_DIR);
    fprintf(stdout, "%s\n", GLOB_TEST_WORK_DIR);

    auto ret = testGlobBase(GLOB_TEST_WORK_DIR, "bbb/*");
    ASSERT_EQ(2, ret.size());
    ASSERT_EQ(GLOB_TEST_WORK_DIR "/bbb/AA21", ret[0]);
    ASSERT_EQ(GLOB_TEST_WORK_DIR "/bbb/b21", ret[1]);

    ret = testGlobBase(GLOB_TEST_WORK_DIR, "../*/bbb/*");
    ASSERT_EQ(2, ret.size());
    ASSERT_EQ(GLOB_TEST_WORK_DIR "/../dir/bbb/AA21", ret[0]);
    ASSERT_EQ(GLOB_TEST_WORK_DIR "/../dir/bbb/b21", ret[1]);

    ret = testGlobBase(GLOB_TEST_WORK_DIR, "../*/*/*");
    ASSERT_EQ(2, ret.size());
    ASSERT_EQ(GLOB_TEST_WORK_DIR "/../dir/bbb/AA21", ret[0]);
    ASSERT_EQ(GLOB_TEST_WORK_DIR "/../dir/bbb/b21", ret[1]);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}