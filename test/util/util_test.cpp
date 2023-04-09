#include "gtest/gtest.h"

#include <misc/edit_distance.hpp>
#include <misc/flag_util.hpp>
#include <misc/num_util.hpp>

using namespace ydsh;

enum class Flag : unsigned int {
  AAA = 1 << 0,
  BBB = 1 << 1,
  CCC = 1 << 2,
};

namespace ydsh {

template <>
struct allow_enum_bitop<Flag> : std::true_type {};

} // namespace ydsh

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

static const char *toEnd(const char *str) { return str + strlen(str); }

TEST(NumTest, base0) {
  // error
  const char *n = "sss";
  const char *begin = n;
  const char *end = toEnd(begin);
  auto ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(0u, ret.first);
  ASSERT_EQ(n, begin);

  n = "";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(0u, ret.first);
  ASSERT_EQ(end, begin);

  n = "12s";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(12u, ret.first);
  ASSERT_EQ(n + 2, begin);

  n = "-12";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(0u, ret.first);
  ASSERT_EQ(n, begin);

  n = "0192";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(1u, ret.first);
  ASSERT_EQ(n + 2, begin);

  n = "0XS92";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(0u, ret.first);
  ASSERT_EQ(n + 2, begin);

  n = "234a92";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(234u, ret.first);
  ASSERT_EQ(n + 3, begin);

  n = "FF";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(0u, ret.first);
  ASSERT_EQ(n, begin);

  n = "00FF";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(0u, ret.first);
  ASSERT_EQ(n + 2, begin);

  n = "4294967299";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(ERANGE, errno);
  ASSERT_EQ(n + 9, begin);

  n = "5294967290";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(ERANGE, errno);
  ASSERT_EQ(n + 9, begin);

  // decimal
  n = "4294967295";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(4294967295, ret.first);
  ASSERT_EQ(n + 10, begin);

  n = "42";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(42, ret.first);
  ASSERT_EQ(end, begin);

  // octal
  n = "042";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(042, ret.first);
  ASSERT_EQ(end, begin);

  n = "0O77";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(077, ret.first);
  ASSERT_EQ(end, begin);

  n = "000706";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(0706, ret.first);
  ASSERT_EQ(end, begin);
}

TEST(NumTest, base8) {
  const char *n = "123";
  const char *begin = n;
  const char *end = toEnd(begin);
  auto ret = parseInteger<uint32_t>(begin, end, 8);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(0123u, ret.first);
  ASSERT_EQ(end, begin);

  n = "806";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 8);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(0, ret.first);
  ASSERT_EQ(n, begin);

  n = "+D06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 8);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(0, ret.first);
  ASSERT_EQ(n + 1, begin);

  n = "02D06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 8);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(2, ret.first);
  ASSERT_EQ(n + 2, begin);

  n = "0xD06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 8);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(0, ret.first);
  ASSERT_EQ(n + 1, begin);

  n = "0O406";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 8);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(0, ret.first);
  ASSERT_EQ(n + 1, begin);
}

TEST(NumTest, base10) {
  const char *n = "123";
  const char *begin = n;
  const char *end = toEnd(begin);
  auto ret = parseInteger<uint32_t>(begin, end, 10);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(123u, ret.first);
  ASSERT_EQ(end, begin);

  n = "D06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 10);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(0, ret.first);
  ASSERT_EQ(n, begin);

  n = "0";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 10);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(0u, ret.first);
  ASSERT_EQ(end, begin);

  n = "06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 10);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(6, ret.first);
  ASSERT_EQ(end, begin);

  n = "02D06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 10);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(2, ret.first);
  ASSERT_EQ(n + 2, begin);

  n = "2D06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 10);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(2, ret.first);
  ASSERT_EQ(n + 1, begin);

  n = "0xD06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 10);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(0, ret.first);
  ASSERT_EQ(n + 1, begin);

  n = "0O406";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 10);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(0, ret.first);
  ASSERT_EQ(n + 1, begin);

  n = "FO406";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 10);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(0, ret.first);
  ASSERT_EQ(n, begin);

  n = "S406";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 10);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(0, ret.first);
  ASSERT_EQ(n, begin);
}

TEST(NumTest, base16) {
  const char *n = "123";
  const char *begin = n;
  const char *end = toEnd(begin);
  auto ret = parseInteger<uint32_t>(begin, end, 16);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(0x123u, ret.first);
  ASSERT_EQ(end, begin);

  n = "+D06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 16);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(0xD06u, ret.first);
  ASSERT_EQ(end, begin);

  n = "0";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 16);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(0u, ret.first);
  ASSERT_EQ(end, begin);

  n = "+06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 16);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(0x06u, ret.first);
  ASSERT_EQ(end, begin);

  n = "02D06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 16);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(0x2D06u, ret.first);
  ASSERT_EQ(end, begin);

  n = "0xD06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 16);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(0, ret.first);
  ASSERT_EQ(n + 1, begin);

  n = "0O406";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 16);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(0, ret.first);
  ASSERT_EQ(n + 1, begin);

  n = "FO406";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 16);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(0xF, ret.first);
  ASSERT_EQ(n + 1, begin);

  n = "XO406";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 16);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);
  ASSERT_EQ(0, ret.first);
  ASSERT_EQ(n, begin);
}

TEST(NumTest, int32) {
  // decimal
  const char *n = "12345";
  auto ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(12345, ret.first);

  n = "0";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(0, ret.first);

  n = "111111111111111111";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_FALSE(ret.second);

  n = "+2147483647";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(2147483647, ret.first);

  n = "2147483648";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_FALSE(ret.second);

  n = "-0";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(0, ret.first);

  n = "-10";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(-10, ret.first);

  n = "-10";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(-10, ret.first);

  n = "-2147483647";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(-2147483647, ret.first);

  n = "-2147483648";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(-2147483648, ret.first);

  // octal
  n = "00000";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(0, ret.first);

  n = "070";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(070, ret.first);

  n = "080";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_FALSE(ret.second);

  n = "0o074";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(074, ret.first);

  n = "0o00000000000";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(0, ret.first);

  n = "0O74";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(074, ret.first);

  n = "+0O8";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_FALSE(ret.second);

  n = "-0O8";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_FALSE(ret.second);

  n = "-0O74";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(-074, ret.first);

  n = "-00000";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(0, ret.first);
}

TEST(NumTest, radix) {
  const char *n = "GE";
  auto ret = convertToNum<int32_t>(n, toEnd(n), 17);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(17 * ('G' - 'A' + 10) + ('E' - 'A' + 10), ret.first);

  n = "0";
  ret = convertToNum<int32_t>(n, toEnd(n), 1);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);

  n = "FF";
  ret = convertToNum<int32_t>(n, toEnd(n), 15);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);

  n = "gE";
  ret = convertToNum<int32_t>(n, toEnd(n), 16);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);

  n = "ge";
  ret = convertToNum<int32_t>(n, toEnd(n), 37);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);

  n = "@";
  ret = convertToNum<int32_t>(n, toEnd(n), 33);
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(EINVAL, errno);

  n = "zZ";
  ret = convertToNum<int32_t>(n, toEnd(n), 36);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(36 * ('Z' - 'A' + 10) + ('Z' - 'A' + 10), ret.first);

  n = "GE";
  ret = convertToNum<int32_t>(n, toEnd(n), 35);
  ASSERT_TRUE(ret.second);
  ASSERT_EQ(35 * ('G' - 'A' + 10) + ('E' - 'A' + 10), ret.first);
}

TEST(NumTest, double1) {
  setlocale(LC_NUMERIC, "German"); // decimal_point is different in German locale

  const char *n = "3.14";
  auto ret = convertToDouble(n);
  ASSERT_EQ(0, ret.second);
  ASSERT_EQ(3.14, ret.first);

  n = "3.14###";
  ret = convertToDouble(n, false); // disallow illegal suffix
  ASSERT_EQ(-2, ret.second);
  ASSERT_EQ(3.14, ret.first);

  n = "3.14###";
  ret = convertToDouble(n, true); // allow illegal suffix
  ASSERT_EQ(0, ret.second);
  ASSERT_EQ(3.14, ret.first);

  n = " 3.14";
  ret = convertToDouble(n); // disallow prefix spaces
  ASSERT_EQ(-1, ret.second);
  ASSERT_EQ(0, ret.first);

  n = "@3.14";
  ret = convertToDouble(n); // disallow prefix spaces
  ASSERT_EQ(-1, ret.second);
  ASSERT_EQ(0, ret.first);

  n = "3.14e9999999999999999999999";
  ret = convertToDouble(n); // huge value
  ASSERT_EQ(1, ret.second);
  ASSERT_EQ(HUGE_VAL, ret.first);
}

TEST(EditDistanceTest, base) {
  EditDistance editDistance;

  ASSERT_EQ(1, editDistance("sitting", "setting"));
  ASSERT_EQ(3, editDistance("sitting", "kitten"));
  ASSERT_EQ(3, editDistance("kitten", "sitting"));
  ASSERT_EQ(3, editDistance("Sunday", "Saturday"));
  ASSERT_EQ(0, editDistance("12", "12"));
  ASSERT_EQ(1, editDistance("12", "124"));
  ASSERT_EQ(2, editDistance("12", ""));
  ASSERT_EQ(0, editDistance("", ""));
  ASSERT_EQ(5, editDistance("corporate", "cooperation"));
  ASSERT_EQ(5, editDistance("cooperation", "corporate"));
  ASSERT_EQ(4, editDistance("TRUE", "true"));
  ASSERT_EQ(1, editDistance("True", "true"));
  ASSERT_EQ(8, editDistance("jfierjft", "1234"));
  ASSERT_EQ(1, editDistance("_0", "_s"));
  ASSERT_EQ(1, editDistance("_", "_s"));
  ASSERT_EQ(2, editDistance("b", "abs"));
  ASSERT_EQ(2, editDistance("os", "?"));
}

TEST(EditDistanceTest, cost) {
  EditDistance editDistance(2);

  ASSERT_EQ(2, editDistance("sitting", "setting"));
  ASSERT_EQ(1, editDistance("sitting", "sittin"));
  ASSERT_EQ(1, editDistance("sitting", "stting"));
  ASSERT_EQ(3, editDistance("os", "?"));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
