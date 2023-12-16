#include "gtest/gtest.h"

#include <misc/string_ref.hpp>

using namespace arsh;

struct StringRefTest : public ::testing::Test {
  template <size_t N>
  void equals(const char (&expect)[N], const StringRef &ref) {
    ASSERT_EQ(N - 1, ref.size());
    ASSERT_TRUE(memcmp(expect, ref.data(), N - 1) == 0);
  }
};

TEST_F(StringRefTest, base1) {
  StringRef ref = "hello";
  ASSERT_EQ(5UL, ref.size());
  ASSERT_NO_FATAL_FAILURE(this->equals("hello", ref));

  ref = StringRef("world!!", 4);
  ASSERT_EQ(4UL, ref.size());
  ASSERT_NO_FATAL_FAILURE(this->equals("worl", ref));
}

TEST_F(StringRefTest, base2) {
  StringRef ref;
  ASSERT_EQ(0UL, ref.size());
  ASSERT_TRUE(ref.empty());
  ASSERT_NO_FATAL_FAILURE(this->equals("", ref));

  StringRef ref2 = ref;
  ASSERT_TRUE(ref2.empty());
  ASSERT_NO_FATAL_FAILURE(this->equals("", ref2));

  ref2 = "!!!";
  ASSERT_EQ(3UL, ref2.size());
  ASSERT_NO_FATAL_FAILURE(this->equals("!!!", ref2));
}

TEST_F(StringRefTest, base3) {
  std::string value = "12";
  value += '\0';
  value += "34";

  StringRef ref = value;
  ASSERT_EQ(5UL, ref.size());
  char data[] = {'1', '2', '\0', '3', '4', '\0'};
  ASSERT_NO_FATAL_FAILURE(this->equals(data, ref));
  ASSERT_EQ(value.c_str(), ref.data());

  auto *ptr = ref.take();
  ASSERT_EQ(value.c_str(), ptr);
  ASSERT_EQ(0UL, ref.size());
  ASSERT_EQ(nullptr, ref.data());
}

TEST_F(StringRefTest, get) {
  StringRef ref = "ABCD";
  ASSERT_EQ('A', ref[0]);
  ASSERT_EQ('B', ref[1]);
  ASSERT_EQ('C', ref[2]);
  ASSERT_EQ('D', ref[3]);

  ASSERT_EQ('A', ref.front());
  ASSERT_EQ('D', ref.back());

  ASSERT_EQ('A', *ref.begin());
  ASSERT_EQ('B', *(ref.begin() + 1));
  ASSERT_EQ('C', *(ref.begin() + 2));
  ASSERT_EQ('D', *(ref.begin() + 3));
  ASSERT_EQ(ref.end(), ref.begin() + 4);

  ASSERT_EQ('D', *(ref.end() - 1));
  ASSERT_EQ('C', *(ref.end() - 2));
  ASSERT_EQ('B', *(ref.end() - 3));
  ASSERT_EQ('A', *(ref.end() - 4));
}

TEST_F(StringRefTest, comp) {
  StringRef ref1 = "abc";
  std::string value = "a";
  value += "b";
  value += "c";
  StringRef ref2 = value;

  ASSERT_TRUE(ref1 == ref2);
  ASSERT_TRUE(ref2 == ref1);
  ASSERT_TRUE(ref1.compare(ref2) == 0);
  ASSERT_TRUE(ref2.compare(ref1) == 0);

  ref2 = "f";
  ASSERT_TRUE(ref1 != ref2);
  ASSERT_TRUE(ref2 != ref1);
  ASSERT_TRUE(ref1.compare(ref2) < 0);
  ASSERT_TRUE(ref2.compare(ref1) > 0);

  ref1 = "zzz";
  ref2 = "dcfef";
  ASSERT_TRUE(ref1.compare(ref2) > 0);
  ASSERT_TRUE(ref2.compare(ref1) < 0);

  ref1 = "123";
  ref2 = "456";
  ASSERT_TRUE(ref1.compare(ref2) < 0);
  ASSERT_TRUE(ref2.compare(ref1) != 0);
  ASSERT_TRUE(ref2.compare(ref1) > 0);

  ref1 = "";
  ref2 = "ADC";
  ASSERT_TRUE(ref1.compare(ref2) < 0);
  ASSERT_TRUE(ref2.compare(ref1) > 0);

  ref1 = "";
  ref2 = std::string();
  ASSERT_TRUE(ref1 == ref2);
  ASSERT_TRUE(ref1.compare(ref2) == 0);
}

TEST_F(StringRefTest, slice) {
  StringRef ref = "hello world!!";
  ASSERT_EQ("he", ref.slice(0, 2));
  ASSERT_EQ("", ref.slice(0, 0));
  ASSERT_EQ("world", ref.slice(6, 11));
  ASSERT_EQ("world!!", ref.slice(6, 13));

  ASSERT_TRUE(ref.startsWith(ref.slice(0, 2)));
  ASSERT_FALSE(ref.startsWith(ref.slice(1, 2)));
  ASSERT_TRUE(ref.endsWith(ref.slice(7, 13)));
  ASSERT_FALSE(ref.endsWith(ref.slice(1, 2)));
}

TEST_F(StringRefTest, find) {
  StringRef ref = "hello world!!";
  ASSERT_EQ(0, ref.indexOf("h"));
  ASSERT_EQ(2, ref.indexOf("ll"));
  ASSERT_EQ(0, ref.indexOf(""));
  ASSERT_EQ(StringRef::npos, ref.indexOf("hello world!! hey"));
  ASSERT_EQ(StringRef::npos, StringRef("").indexOf("h"));
  ASSERT_EQ(0, StringRef("").indexOf(""));

  ASSERT_EQ(ref.size() - 1, ref.lastIndexOf("!"));
  ASSERT_EQ(StringRef::npos, ref.lastIndexOf("?"));
  ASSERT_EQ(StringRef::npos, ref.lastIndexOf("hello world!! hey"));
  ASSERT_EQ(ref.size() - 1, ref.lastIndexOf(""));
  ASSERT_EQ(0, StringRef("").lastIndexOf(""));
  ASSERT_EQ(StringRef::npos, StringRef("").lastIndexOf("l"));

  ASSERT_EQ(4, ref.find("o"));
  ASSERT_EQ(4, ref.find("o", 4));
  ASSERT_EQ(7, ref.find("o", 5));

  ASSERT_EQ(2, ref.find('l'));
  ASSERT_EQ(3, ref.find('l', 3));
  ASSERT_EQ(9, ref.find('l', 4));
}

TEST_F(StringRefTest, remove) {
  StringRef ref = "hello world!!";
  ref.removeSuffix(2);
  ASSERT_NO_FATAL_FAILURE(this->equals("hello world", ref));
  ref.removePrefix(6);
  ASSERT_NO_FATAL_FAILURE(this->equals("world", ref));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
