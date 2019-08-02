#include "gtest/gtest.h"

#ifndef USE_FIXED_TIME
#define USE_FIXED_TIME
#endif

#include <time_util.h>
#include <misc/flag_util.hpp>

using namespace ydsh;

class TimeTest : public ::testing::Test {
public:
    TimeTest() = default;

    virtual void dotest(struct tm *t) {
        ASSERT_TRUE(t != nullptr);

        ASSERT_EQ(2012, t->tm_year + 1900);
        ASSERT_EQ(1, t->tm_mon + 1);
        ASSERT_EQ(12, t->tm_mday);
        ASSERT_EQ(23, t->tm_hour);
        ASSERT_EQ(45, t->tm_min);
        ASSERT_EQ(56, t->tm_sec);
    }
};

TEST_F(TimeTest, case1) {
    unsetenv("TZ");
    setenv("TIME_SOURCE", "2012-1-12T23:45:56Z", 1);
    struct tm *t = getLocalTime();
    ASSERT_TRUE(getenv("TZ") == nullptr);

    ASSERT_NO_FATAL_FAILURE(this->dotest(t));
}

TEST_F(TimeTest, case2) {
    unsetenv("TZ");
    setenv("TIME_SOURCE", "2012-1-12T23:45:56Z", 1);
    setenv("TZ", "JP", 1);
    struct tm *t = getLocalTime();
    ASSERT_STREQ("JP", getenv("TZ"));

    ASSERT_NO_FATAL_FAILURE(this->dotest(t));
}

TEST_F(TimeTest, case3) {
    unsetenv("TZ");
    setenv("TIME_SOURCE", "2012-1-12T23:45:", 1);   // bad format
    ASSERT_EXIT(getLocalTime(), ::testing::KilledBySignal(SIGABRT), "broken time source\n");
}

enum class Flag : unsigned int {
    AAA = 1 << 0,
    BBB = 1 << 1,
    CCC = 1 << 2,
};

namespace ydsh {

template <> struct allow_enum_bitop<Flag> : std::true_type {};

} // namespace

TEST(EnumTest, base) {
    Flag f = Flag::AAA | Flag::BBB;
    ASSERT_EQ(3, static_cast<unsigned int>(f));
    setFlag(f, Flag::CCC);
    ASSERT_TRUE(hasFlag(f, Flag::AAA));
    ASSERT_TRUE(hasFlag(f, Flag::BBB));
    ASSERT_TRUE(hasFlag(f, Flag::CCC));

    unsetFlag(f, Flag::BBB);
    ASSERT_TRUE(hasFlag(f, Flag::AAA));
    ASSERT_FALSE(hasFlag(f, Flag::BBB));
    ASSERT_TRUE(hasFlag(f, Flag::CCC));
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

