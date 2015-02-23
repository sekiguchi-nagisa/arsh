#include <gtest/gtest.h>
#include <stdio.h>
#include <util/debug.h>

TEST(lexer_test, case1) {
    SCOPED_TRACE("failed case1");
    ASSERT_EQ(1, 1);
}

TEST(lexer_test, case2) {
    SCOPED_TRACE("failed case2");
    ASSERT_EQ(12, 12);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
