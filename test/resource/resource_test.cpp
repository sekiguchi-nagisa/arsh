#include <gtest/gtest.h>

#include <sstream>

#include <misc/resource.hpp>

using namespace ydsh;

TEST(resource, base) {
    std::stringstream s;

    {
        auto r = makeScopedResource(
                12, [&s](unsigned int i) {
                    s << "delete" << i;
                });
    }

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("delete12", s.str()));
}

TEST(resource, move) {
    std::stringstream s;

    {
        auto r = makeScopedResource(
                12, [&s](unsigned int i) {
                    s << "delete" << i;
                });
        auto r2(std::move(r));
    }

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("delete12", s.str()));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

