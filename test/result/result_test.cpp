#include <gtest/gtest.h>

#include <memory>

#include "misc/result.hpp"

using namespace ydsh;

TEST(result, tag) {
    static_assert(TypeTag<int, std::string, int>::value == 1, "must be 1");
    static_assert(TypeTag<unsigned int, std::string, int>::value == -1, "must be -1");
    static_assert(TypeTag<unsigned int>::value == -1, "must be -1");

    static_assert(std::is_same<int, typename TypeByIndex<0, int>::type>::value, "");
    static_assert(std::is_same<int, typename TypeByIndex<0, int, void *, std::string>::type>::value, "");
    static_assert(std::is_same<void *, typename TypeByIndex<1, int, void *, std::string>::type>::value, "");
    static_assert(std::is_same<std::string, typename TypeByIndex<2, int, void *, std::string>::type>::value, "");
}

TEST(result, storage1) {
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

TEST(result, storage2) {
    Storage<int, std::string> value;
    value.obtain(std::string("hello"));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("hello", get<std::string>(value)));

    decltype(value) value2;
    move<std::string>(value, value2);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("hello", get<std::string>(value2)));

    destroy<std::string>(value2);
}

TEST(result, Union1) {
    Union<int, std::string> value(std::string("hey"));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("hey", get<std::string>(value)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(is<std::string>(value)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(is<int>(value)));

    decltype(value) value2;
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(-1, value2.tag()));

    value2 = std::move(value);
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(is<std::string>(value2)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("hey", get<std::string>(value2)));
    value = std::move(value2);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(-1, value2.tag()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("hey", get<std::string>(value)));
}

TEST(result, Union2) {
    auto str = std::make_shared<std::string>("hello");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, str.use_count()));

    using shared = decltype(str);
    auto v = Union<const char *, shared>(shared(str));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, str.use_count()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, get<shared>(v).use_count()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(str.get(), get<shared>(v).get()));

    {
        auto v2 = std::move(v);
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(-1, v.tag()));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, str.use_count()));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, get<shared>(v2).use_count()));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(str.get(), get<shared>(v2).get()));
    }
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, str.use_count()));

    auto v3 = Union<const char *, bool>((const char *)"hello");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("hello", get<const char *>(v3)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(is<const char *>(v3)));
}

Result<std::string, int> func(int index) {
    if(index < 0) {
        return Err(index);
    }
    return Ok(std::to_string(index));
}

//TEST(result, result) {
//    Result<std::string, int> ret = Ok(std::string("hello"));
//    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(ret));
//    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("hello", ret.asOk()));
//
//    Result<std::string, int> ret2 = Err(12);
//    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(ret2));
//    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(12, ret2.asErr()));
//
//    auto v = std::move(func(12).asOk());
//    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("12", v));
//    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(-1, std::move(func(-1).asErr())));
//}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

