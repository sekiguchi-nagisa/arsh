#include <gtest/gtest.h>

#include "misc/result.hpp"

using namespace ydsh;

Result<std::string, int> func(int index) {
    if(index < 0) {
        return Err(index);
    }
    return Ok(std::to_string(index));
}

TEST(result, base) {
    Result<std::string, int> ret = Ok(std::string("hello"));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(ret));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("hello", ret.asOk()));

    Result<std::string, int> ret2 = Err(12);
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(ret2));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(12, ret2.asErr()));

    auto v = std::move(func(12).asOk());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("12", v));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(-1, std::move(func(-1).asErr())));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

