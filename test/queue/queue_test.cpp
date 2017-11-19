
#include "gtest/gtest.h"

#include <misc/queue.hpp>

using namespace ydsh;

TEST(QueueTest, base) {
    auto queue = FixedQueue<int, 4>();

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, queue.size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(queue.empty()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(queue.full()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(queue.push(1)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(queue.push(2)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, queue.size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(queue.empty()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(queue.full()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, queue.pop()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1u, queue.size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, queue.pop()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, queue.size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(queue.empty()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(queue.push(1)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(queue.push(2)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(queue.push(3)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(queue.push(4)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, queue.size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(queue.empty()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(queue.full()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(queue.push(5)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, queue.size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(queue.empty()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(queue.full()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, queue.pop()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, queue.pop()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3, queue.pop()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4, queue.pop()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, queue.size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(queue.empty()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(queue.full()));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
