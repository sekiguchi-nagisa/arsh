#include <gtest/gtest.h>

#include "misc/detect.hpp"
#include "misc/result.hpp"

using namespace arsh;

TEST(result, tag) {
  static_assert(TypeTag<int, std::string, int>::value == 1, "must be 1");
  static_assert(TypeTag<unsigned int, std::string, int>::value == -1, "must be -1");
  static_assert(TypeTag<unsigned int>::value == -1, "must be -1");

  static_assert(std::is_same_v<int, typename TypeByIndex<0, int>::type>);
  static_assert(std::is_same_v<int, typename TypeByIndex<0, int, void *, std::string>::type>);
  static_assert(std::is_same_v<void *, typename TypeByIndex<1, int, void *, std::string>::type>);
  static_assert(
      std::is_same_v<std::string, typename TypeByIndex<2, int, void *, std::string>::type>);
}

TEST(result, storage1) {
  auto str = std::make_shared<std::string>("hello");
  ASSERT_EQ(1, str.use_count());

  using shared = decltype(str);
  Storage<bool, shared> raw;
  raw.obtain(shared(str));
  ASSERT_EQ(2, str.use_count());
  ASSERT_EQ(2, get<shared>(raw).use_count());
  ASSERT_EQ(str.get(), get<shared>(raw).get());

  Storage<bool, shared> raw2;
  move<shared>(raw, raw2);
  ASSERT_EQ(2, get<shared>(raw2).use_count());
  ASSERT_EQ(2, str.use_count());
  ASSERT_EQ(str.get(), get<shared>(raw2).get());

  destroy<shared>(raw2);
  ASSERT_EQ(1, str.use_count());

  raw.obtain(true);
  raw2.obtain(false);
  ASSERT_TRUE(get<bool>(raw));
  ASSERT_FALSE(get<bool>(raw2));
}

TEST(result, storage2) {
  Storage<int, std::string> value;
  value.obtain(std::string("hello"));
  ASSERT_EQ("hello", get<std::string>(value));

  decltype(value) value2;
  move<std::string>(value, value2);
  ASSERT_EQ("hello", get<std::string>(value2));

  destroy<std::string>(value2);
}

TEST(result, Union1) {
  Union<int, std::string, std::nullptr_t> value(std::string("hey"));
  ASSERT_EQ("hey", get<std::string>(value));
  ASSERT_TRUE(is<std::string>(value));
  ASSERT_FALSE(is<int>(value));

  decltype(value) value2;
  ASSERT_EQ(-1, value2.tag());
  ASSERT_FALSE(value2.hasValue());

  value2 = std::move(value);
  ASSERT_TRUE(is<std::string>(value2));
  ASSERT_EQ("hey", get<std::string>(value2));
  value = std::move(value2);
  ASSERT_EQ(-1, value2.tag());
  ASSERT_FALSE(value2.hasValue());
  ASSERT_EQ("hey", get<std::string>(value));

  value = nullptr;
  ASSERT_TRUE(is<std::nullptr_t>(value));
  ASSERT_EQ(nullptr, get<std::nullptr_t>(value));
}

TEST(result, Union2) {
  auto str = std::make_shared<std::string>("hello");
  ASSERT_EQ(1, str.use_count());

  using shared = decltype(str);
  auto v = Union<const char *, shared>(shared(str));
  ASSERT_EQ(2, str.use_count());
  ASSERT_EQ(2, get<shared>(v).use_count());
  ASSERT_EQ(str.get(), get<shared>(v).get());

  {
    auto v2 = std::move(v);
    ASSERT_EQ(-1, v.tag());
    ASSERT_FALSE(v.hasValue());
    ASSERT_EQ(2, str.use_count());
    ASSERT_EQ(2, get<shared>(v2).use_count());
    ASSERT_EQ(str.get(), get<shared>(v2).get());
  }
  ASSERT_EQ(1, str.use_count());

  auto v3 = Union<const char *, bool>((const char *)"hello");
  ASSERT_EQ("hello", get<const char *>(v3));
  ASSERT_TRUE(is<const char *>(v3));
}

TEST(result, Union3) {
  auto v = Union<unsigned int>();
  ASSERT_FALSE(v.hasValue());
  v = 34;
  ASSERT_TRUE(v.hasValue());
  ASSERT_EQ(34, get<unsigned int>(v));
}

TEST(result, Union4) {
  auto str = std::make_shared<std::string>("hello");
  ASSERT_EQ(1, str.use_count());

  using shared = decltype(str);
  auto v = Union<const char *, shared>(shared(str));
  ASSERT_EQ(2, str.use_count());
  ASSERT_EQ(2, get<shared>(v).use_count());
  ASSERT_EQ(str.get(), get<shared>(v).get());

  auto v2 = v;
  ASSERT_EQ(3, str.use_count());
  ASSERT_EQ(3, get<shared>(v).use_count());
  ASSERT_EQ(3, get<shared>(v2).use_count());
  ASSERT_EQ(str.get(), get<shared>(v).get());
  ASSERT_EQ(str.get(), get<shared>(v2).get());

  v = "34";
  ASSERT_STREQ("34", get<const char *>(v));
  ASSERT_EQ(2, str.use_count());
  ASSERT_EQ(2, get<shared>(v2).use_count());

  v2 = v;
  ASSERT_STREQ("34", get<const char *>(v2));
  ASSERT_EQ(1, str.use_count());
  ASSERT_EQ(get<const char *>(v), get<const char *>(v2));
}

Result<std::string, int> func(int index) {
  if (index < 0) {
    return Err(index);
  }
  return Ok(std::to_string(index));
}

TEST(result, result) {
  Result<std::string, int> ret = Ok("hello");
  ASSERT_TRUE(ret);
  ASSERT_EQ("hello", ret.asOk());

  Result<std::string, int> ret2 = Err(12);
  ASSERT_FALSE(ret2);
  ASSERT_EQ(12, ret2.asErr());

  auto v = std::move(func(12).asOk());
  ASSERT_EQ("12", v);
  ASSERT_EQ(-1, func(-1).asErr());
}

TEST(result, optional1) {
  Optional<int> ret;
  ASSERT_FALSE(ret.hasValue());
  ret = 12;
  ASSERT_TRUE(ret.hasValue());
  ASSERT_EQ(12, ret.unwrap());
  ret = Optional<int>(34);
  ASSERT_TRUE(ret.hasValue());
  ASSERT_EQ(34, ret.unwrap());
}

TEST(result, optional2) {
  ASSERT_EQ(sizeof(Union<int, double>), sizeof(Optional<Union<int, double>>));
  Optional<Union<int, std::string>> ret;
  ASSERT_FALSE(ret.hasValue());
  ret = 12;
  ASSERT_TRUE(ret.hasValue());
  ASSERT_EQ(12, get<int>(ret));
  ret = "hello";
  ASSERT_TRUE(ret.hasValue());
  ASSERT_EQ("hello", get<std::string>(ret));

  ASSERT_EQ(sizeof(Optional<int>), sizeof(Optional<Optional<int>>));
}

template <typename T>
using hasValue_member = decltype(&T::hasValue);

TEST(result, optional3) {
  static_assert(is_detected_v<hasValue_member, Optional<int>>);
  static_assert(!is_detected_v<hasValue_member, std::string>);

  static_assert(std::is_same_v<Optional<float>, Optional<Optional<float>>>);
  ASSERT_EQ(sizeof(Optional<int>), sizeof(Optional<Optional<int>>));
  ASSERT_EQ(sizeof(Optional<int>), sizeof(Optional<Optional<Optional<int>>>));
  Optional<Optional<int>> ret;
  ASSERT_FALSE(ret.hasValue());
  ret = 12;
  ASSERT_TRUE(ret.hasValue());
  ASSERT_EQ(12, ret.unwrap());
  ret = Optional<int>(34);
  ASSERT_TRUE(ret.hasValue());
  ASSERT_EQ(34, ret.unwrap());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
