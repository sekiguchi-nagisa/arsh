#include "gtest/gtest.h"

#include "../test_common.h"
#include <misc/glob.hpp>
#include <misc/string_ref.hpp>

using namespace ydsh;

class GlobTest : public ::testing::Test, public TempFileFactory {
protected:
    std::string oldCWD;

    int visitTempRoot() {
        char *ptr = realpath(".", nullptr);
        this->oldCWD = ptr;
        free(ptr);
        errno = 0;
        return chdir(this->tmpDirName.c_str());
    }
};

static bool matchPattern(const char *name, const char *p) {
    return matchWildcard(name, p);
}

TEST_F(GlobTest, pattern) {
    ASSERT_FALSE(matchPattern("", "ff"));
    ASSERT_FALSE(matchPattern("", "?"));
    ASSERT_TRUE(matchPattern("", "*"));
    ASSERT_TRUE(matchPattern("", ""));
    ASSERT_TRUE(matchPattern("hoge", "hoge"));
    ASSERT_FALSE(matchPattern("hoge", ""));
    ASSERT_FALSE(matchPattern("hoge", "h"));
    ASSERT_FALSE(matchPattern("hoge", "wwww"));
    ASSERT_FALSE(matchPattern("hoge", "h*g"));
    ASSERT_TRUE(matchPattern("hoge", "*h*ge"));
    ASSERT_FALSE(matchPattern("hoge", "?"));
    ASSERT_TRUE(matchPattern("hoge", "????"));
    ASSERT_TRUE(matchPattern("hoge", "?*"));
    ASSERT_TRUE(matchPattern("hoge", "*"));
    ASSERT_FALSE(matchPattern("hoge", "h*gw"));
    ASSERT_FALSE(matchPattern("hoge", "g*w"));
    ASSERT_TRUE(matchPattern("hoge", "h*ge*"));
    ASSERT_TRUE(matchPattern("hoge", "h**g**e*"));
    ASSERT_TRUE(matchPattern("hoge", "h*ge**"));
    ASSERT_FALSE(matchPattern("hoge", "h*ge?"));
    ASSERT_FALSE(matchPattern("hoge", "h*ge**?"));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}