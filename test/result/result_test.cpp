#include <gtest/gtest.h>

#include <memory>

#include "misc/result.hpp"

using namespace ydsh;

TEST(result, storage) {
    auto str = std::make_shared<std::string>("hello");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, str.use_count()));

    using shared = decltype(str);
    Storage<bool, shared > raw;
    raw.obtain(shared(str));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, str.use_count()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, get<shared>(raw).use_count()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(str.get(), get<shared>(raw).get()));

    Storage<bool, shared> raw2;
    move<shared>(raw, raw2);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, get<shared>(raw2).use_count()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, str.use_count()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(str.get(), get<shared>(raw2).get()));

    destroy<shared>(raw2);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, str.use_count()));

    raw.obtain(true);
    raw2.obtain(false);
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(get<bool>(raw)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(get<bool>(raw2)));
}


Result<std::string, int> func(int index) {
    if(index < 0) {
        return Err(index);
    }
    return Ok(std::to_string(index));
}

TEST(result, result) {
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

