#include "gtest/gtest.h"

#ifndef USE_FIXED_TIME
#define USE_FIXED_TIME
#endif

#include <time_util.h>

using namespace ydsh;

class BaseTest : public ::testing::Test {
public:
    BaseTest() = default;
    virtual ~BaseTest() = default;

    virtual void dotest(struct tm *t) {
        SCOPED_TRACE("");

        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(t != nullptr));

        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2012, t->tm_year + 1900));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, t->tm_mon + 1));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(12, t->tm_mday));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(23, t->tm_hour));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(45, t->tm_min));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(56, t->tm_sec));
    }
};

TEST_F(BaseTest, case1) {
    SCOPED_TRACE("");

    unsetenv("TZ");
    setenv("TIME_SOURCE", "2012-1-12T23:45:56Z", 1);
    struct tm *t = getLocalTime();
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(getenv("TZ") == nullptr));

    this->dotest(t);
}

TEST_F(BaseTest, case2) {
    SCOPED_TRACE("");

    unsetenv("TZ");
    setenv("TIME_SOURCE", "2012-1-12T23:45:56Z", 1);
    setenv("TZ", "JP", 1);
    struct tm *t = getLocalTime();
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("JP", getenv("TZ")));

    this->dotest(t);
}

TEST_F(BaseTest, case3) {
    SCOPED_TRACE("");

    unsetenv("TZ");
    setenv("TIME_SOURCE", "2012-1-12T23:45:", 1);   // bad format
    ASSERT_NO_FATAL_FAILURE(ASSERT_EXIT(getLocalTime(), ::testing::KilledBySignal(SIGABRT), "broken time source\n"));
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

