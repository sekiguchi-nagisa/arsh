#include "gtest/gtest.h"

#include <misc/edit_distance.hpp>
#include <misc/files.hpp>
#include <misc/flag_util.hpp>
#include <misc/format.hpp>
#include <misc/num_util.hpp>
#include <misc/time_util.hpp>

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
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(0u, ret.value);
  ASSERT_EQ(0, ret.consumedSize);
  ASSERT_EQ(n, begin);

  n = "";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::ILLEGAL_CHAR, ret.kind);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(0, ret.consumedSize);
  ASSERT_EQ(end, begin);

  n = "12s";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(12, ret.value);
  ASSERT_EQ(2, ret.consumedSize);
  ASSERT_EQ(n + 2, begin);

  n = "-12";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::ILLEGAL_CHAR, ret.kind);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(0, ret.consumedSize);
  ASSERT_EQ(n, begin);

  n = "0192";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(1, ret.value);
  ASSERT_EQ(2, ret.consumedSize);
  ASSERT_EQ(n + 2, begin);

  n = "0XS92";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(2, ret.consumedSize);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(n + 2, begin);

  n = "234a92";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(3, ret.consumedSize);
  ASSERT_EQ(234, ret.value);
  ASSERT_EQ(n + 3, begin);

  n = "FF";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(0, ret.consumedSize);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(n, begin);

  n = "00FF";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(2, ret.consumedSize);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(n + 2, begin);

  n = "4294967299";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::OUT_OF_RANGE, ret.kind);
  ASSERT_EQ(9, ret.consumedSize);
  ASSERT_EQ(3, ret.value);
  ASSERT_EQ(n + 9, begin);

  n = "5294967290";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::OUT_OF_RANGE, ret.kind);
  ASSERT_EQ(9, ret.consumedSize);
  ASSERT_EQ(n + 9, begin);

  // decimal
  n = "4294967295";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_TRUE(ret);
  ASSERT_EQ(4294967295, ret.value);
  ASSERT_EQ(10, ret.consumedSize);
  ASSERT_EQ(n + 10, begin);

  n = "42";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_TRUE(ret);
  ASSERT_EQ(42, ret.value);
  ASSERT_EQ(2, ret.consumedSize);
  ASSERT_EQ(end, begin);

  // octal
  n = "042";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_TRUE(ret);
  ASSERT_EQ(042, ret.value);
  ASSERT_EQ(3, ret.consumedSize);
  ASSERT_EQ(end, begin);

  n = "0O77";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_TRUE(ret);
  ASSERT_EQ(077, ret.value);
  ASSERT_EQ(4, ret.consumedSize);
  ASSERT_EQ(end, begin);

  n = "000706";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 0);
  ASSERT_TRUE(ret);
  ASSERT_EQ(0706, ret.value);
  ASSERT_EQ(6, ret.consumedSize);
  ASSERT_EQ(end, begin);
}

TEST(NumTest, base8) {
  const char *n = "123";
  const char *begin = n;
  const char *end = toEnd(begin);
  auto ret = parseInteger<uint32_t>(begin, end, 8);
  ASSERT_TRUE(ret);
  ASSERT_EQ(0123u, ret.value);
  ASSERT_EQ(3, ret.consumedSize);
  ASSERT_EQ(end, begin);

  n = "806";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 8);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(0, ret.consumedSize);
  ASSERT_EQ(n, begin);

  n = "+D06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 8);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(1, ret.consumedSize);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(n + 1, begin);

  n = "02D06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 8);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(2, ret.value);
  ASSERT_EQ(2, ret.consumedSize);
  ASSERT_EQ(n + 2, begin);

  n = "0xD06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 8);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(1, ret.consumedSize);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(n + 1, begin);

  n = "0O406";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 8);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(1, ret.consumedSize);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(n + 1, begin);
}

TEST(NumTest, base10) {
  const char *n = "123";
  const char *begin = n;
  const char *end = toEnd(begin);
  auto ret = parseInteger<uint32_t>(begin, end, 10);
  ASSERT_TRUE(ret);
  ASSERT_EQ(123u, ret.value);
  ASSERT_EQ(3, ret.consumedSize);
  ASSERT_EQ(end, begin);

  n = "D06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 10);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(0, ret.consumedSize);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(n, begin);

  n = "0";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 10);
  ASSERT_TRUE(ret);
  ASSERT_EQ(0u, ret.value);
  ASSERT_EQ(1, ret.consumedSize);
  ASSERT_EQ(end, begin);

  n = "06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 10);
  ASSERT_TRUE(ret);
  ASSERT_EQ(6, ret.value);
  ASSERT_EQ(2, ret.consumedSize);
  ASSERT_EQ(end, begin);

  n = "02D06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 10);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(2, ret.consumedSize);
  ASSERT_EQ(2, ret.value);
  ASSERT_EQ(n + 2, begin);

  n = "2D06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 10);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(1, ret.consumedSize);
  ASSERT_EQ(2, ret.value);
  ASSERT_EQ(n + 1, begin);

  n = "0xD06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 10);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(1, ret.consumedSize);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(n + 1, begin);

  n = "0O406";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 10);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(1, ret.consumedSize);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(n + 1, begin);

  n = "FO406";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 10);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(0, ret.consumedSize);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(n, begin);

  n = "S406";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 10);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(0, ret.consumedSize);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(n, begin);
}

TEST(NumTest, base16) {
  const char *n = "123";
  const char *begin = n;
  const char *end = toEnd(begin);
  auto ret = parseInteger<uint32_t>(begin, end, 16);
  ASSERT_TRUE(ret);
  ASSERT_EQ(0x123u, ret.value);
  ASSERT_EQ(3, ret.consumedSize);
  ASSERT_EQ(end, begin);

  n = "+D06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 16);
  ASSERT_TRUE(ret);
  ASSERT_EQ(0xD06u, ret.value);
  ASSERT_EQ(4, ret.consumedSize);
  ASSERT_EQ(end, begin);

  n = "0";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 16);
  ASSERT_TRUE(ret);
  ASSERT_EQ(0u, ret.value);
  ASSERT_EQ(1, ret.consumedSize);
  ASSERT_EQ(end, begin);

  n = "+06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 16);
  ASSERT_TRUE(ret);
  ASSERT_EQ(0x06u, ret.value);
  ASSERT_EQ(3, ret.consumedSize);
  ASSERT_EQ(end, begin);

  n = "02D06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 16);
  ASSERT_TRUE(ret);
  ASSERT_EQ(0x2D06u, ret.value);
  ASSERT_EQ(5, ret.consumedSize);
  ASSERT_EQ(end, begin);

  n = "0xD06";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 16);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(1, ret.consumedSize);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(n + 1, begin);

  n = "0O406";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 16);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(1, ret.consumedSize);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(n + 1, begin);

  n = "FO406";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 16);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(1, ret.consumedSize);
  ASSERT_EQ(15, ret.value);
  ASSERT_EQ(n + 1, begin);

  n = "XO406";
  begin = n;
  end = toEnd(begin);
  ret = parseInteger<uint32_t>(begin, end, 16);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(0, ret.consumedSize);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(n, begin);
}

TEST(NumTest, int32) {
  // decimal
  const char *n = "12345";
  auto ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret);
  ASSERT_EQ(12345, ret.value);
  ASSERT_EQ(5, ret.consumedSize);

  n = "0";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(1, ret.consumedSize);

  n = "111111111111111111";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::OUT_OF_RANGE, ret.kind);
  ASSERT_EQ(10, ret.consumedSize);

  n = "+2147483647";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret);
  ASSERT_EQ(2147483647, ret.value);
  ASSERT_EQ(11, ret.consumedSize);

  n = "2147483648";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::OUT_OF_RANGE, ret.kind);
  ASSERT_EQ(10, ret.consumedSize);

  n = "-0";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(2, ret.consumedSize);

  n = "-10";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret);
  ASSERT_EQ(-10, ret.value);
  ASSERT_EQ(3, ret.consumedSize);

  n = "+10";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret);
  ASSERT_EQ(10, ret.value);
  ASSERT_EQ(3, ret.consumedSize);

  n = "-2147483647";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret);
  ASSERT_EQ(-2147483647, ret.value);
  ASSERT_EQ(11, ret.consumedSize);

  n = "-2147483648";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret);
  ASSERT_EQ(-2147483648, ret.value);
  ASSERT_EQ(11, ret.consumedSize);

  // octal
  n = "00000";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(5, ret.consumedSize);

  n = "070";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret);
  ASSERT_EQ(070, ret.value);
  ASSERT_EQ(3, ret.consumedSize);

  n = "080";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(1, ret.consumedSize);

  n = "0o074";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret);
  ASSERT_EQ(074, ret.value);
  ASSERT_EQ(5, ret.consumedSize);

  n = "0o00000000000";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(13, ret.consumedSize);

  n = "0O74";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret);
  ASSERT_EQ(074, ret.value);
  ASSERT_EQ(4, ret.consumedSize);

  n = "+0O8";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(3, ret.consumedSize);
  ASSERT_EQ(0, ret.value);

  n = "-0O8";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_FALSE(ret);
  ASSERT_EQ(3, ret.consumedSize);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(0, ret.value);

  n = "-0O74";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret);
  ASSERT_EQ(-074, ret.value);
  ASSERT_EQ(5, ret.consumedSize);

  n = "-00000";
  ret = convertToNum<int32_t>(n, toEnd(n), 0);
  ASSERT_TRUE(ret);
  ASSERT_EQ(0, ret.value);
  ASSERT_EQ(6, ret.consumedSize);
}

TEST(NumTest, radix) {
  const char *n = "GE";
  auto ret = convertToNum<int32_t>(n, toEnd(n), 17);
  ASSERT_TRUE(ret);
  ASSERT_EQ(17 * ('G' - 'A' + 10) + ('E' - 'A' + 10), ret.value);
  ASSERT_EQ(2, ret.consumedSize);

  n = "0";
  ret = convertToNum<int32_t>(n, toEnd(n), 1);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::ILLEGAL_RADIX, ret.kind);
  ASSERT_EQ(0, ret.consumedSize);
  ASSERT_EQ(0, ret.value);

  n = "FF";
  ret = convertToNum<int32_t>(n, toEnd(n), 15);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(0, ret.consumedSize);
  ASSERT_EQ(0, ret.value);

  n = "gE";
  ret = convertToNum<int32_t>(n, toEnd(n), 16);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::RADIX_OVERFLOW, ret.kind);
  ASSERT_EQ(0, ret.consumedSize);
  ASSERT_EQ(0, ret.value);

  n = "ge";
  ret = convertToNum<int32_t>(n, toEnd(n), 37);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::ILLEGAL_RADIX, ret.kind);
  ASSERT_EQ(0, ret.consumedSize);
  ASSERT_EQ(0, ret.value);

  n = "@";
  ret = convertToNum<int32_t>(n, toEnd(n), 33);
  ASSERT_FALSE(ret);
  ASSERT_EQ(IntConversionStatus::ILLEGAL_CHAR, ret.kind);
  ASSERT_EQ(0, ret.consumedSize);
  ASSERT_EQ(0, ret.value);

  n = "zZ";
  ret = convertToNum<int32_t>(n, toEnd(n), 36);
  ASSERT_TRUE(ret);
  ASSERT_EQ(36 * ('Z' - 'A' + 10) + ('Z' - 'A' + 10), ret.value);
  ASSERT_EQ(2, ret.consumedSize);

  n = "GE";
  ret = convertToNum<int32_t>(n, toEnd(n), 35);
  ASSERT_TRUE(ret);
  ASSERT_EQ(35 * ('G' - 'A' + 10) + ('E' - 'A' + 10), ret.value);
  ASSERT_EQ(2, ret.consumedSize);
}

TEST(NumTest, double1) {
  setlocale(LC_NUMERIC, "German"); // decimal_point is different in German locale

  const char *n = "3.14";
  auto ret = convertToDouble(n);
  ASSERT_TRUE(ret);
  ASSERT_EQ(3.14, ret.value);
  ASSERT_EQ(4, ret.consumedSize);

  n = "3.14###";
  ret = convertToDouble(n, false); // disallow illegal suffix
  ASSERT_FALSE(ret);
  ASSERT_EQ(DoubleConversionResult::ILLEGAL_CHAR, ret.kind);
  ASSERT_EQ(3.14, ret.value);
  ASSERT_EQ(4, ret.consumedSize);

  n = "3.14###";
  ret = convertToDouble(n, true); // allow illegal suffix
  ASSERT_TRUE(ret);
  ASSERT_EQ(3.14, ret.value);
  ASSERT_EQ(4, ret.consumedSize);

  n = " 3.14";
  ret = convertToDouble(n); // disallow prefix spaces
  ASSERT_FALSE(ret);
  ASSERT_EQ(0, ret.consumedSize);
  ASSERT_EQ(0.0, ret.value);
  ASSERT_EQ(DoubleConversionResult::ILLEGAL_CHAR, ret.kind);

  n = "@3.14";
  ret = convertToDouble(n); // disallow prefix spaces
                            //    ASSERT_EQ(-1, ret.second);
                            //    ASSERT_EQ(0, ret.first);
  ASSERT_FALSE(ret);
  ASSERT_EQ(DoubleConversionResult::ILLEGAL_CHAR, ret.kind);
  ASSERT_EQ(0, ret.consumedSize);
  ASSERT_EQ(0, ret.value);

  n = "3.14e9999999999999999999999";
  ret = convertToDouble(n); // huge value
  ASSERT_FALSE(ret);
  ASSERT_EQ(DoubleConversionResult::OUT_OF_RANGE, ret.kind);
  ASSERT_EQ(HUGE_VAL, ret.value);
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

static std::string toUTC(time_t time) {
  struct tm utc {};
  gmtime_r(&time, &utc);
  char data[1024];
  strftime(data, std::size(data), "%Y/%m/%d %H:%M:%S", &utc);
  return {data};
}

TEST(TimestampTest, parse) {
  StringRef input = "1689511935";
  timespec time{};
  auto s = parseUnixTimeWithNanoSec(input.begin(), input.end(), time);
  ASSERT_EQ(ParseTimespecStatus::OK, s);
  ASSERT_EQ(1689511935, time.tv_sec);
  ASSERT_EQ(0, time.tv_nsec);
  ASSERT_EQ("2023/07/16 12:52:15", toUTC(time.tv_sec));

  // with nano sec
  input = "1689511935.00023";
  time = {};
  s = parseUnixTimeWithNanoSec(input.begin(), input.end(), time);
  ASSERT_EQ(ParseTimespecStatus::OK, s);
  ASSERT_EQ(1689511935, time.tv_sec);
  ASSERT_EQ(230000, time.tv_nsec);
  ASSERT_EQ("2023/07/16 12:52:15", toUTC(time.tv_sec));

  input = "1689511935.23";
  time = {};
  s = parseUnixTimeWithNanoSec(input.begin(), input.end(), time);
  ASSERT_EQ(ParseTimespecStatus::OK, s);
  ASSERT_EQ(1689511935, time.tv_sec);
  ASSERT_EQ(230000000, time.tv_nsec);
  ASSERT_EQ("2023/07/16 12:52:15", toUTC(time.tv_sec));

  // too large time
  input = "214748364799.987654321";
  time = {};
  s = parseUnixTimeWithNanoSec(input.begin(), input.end(), time);
  ASSERT_EQ(ParseTimespecStatus::OK, s);
  ASSERT_EQ(214748364799, time.tv_sec);
  ASSERT_EQ(987654321, time.tv_nsec);
  ASSERT_EQ("8775/02/08 11:33:19", toUTC(time.tv_sec));

  // negative
  input = "-214748364799.00";
  time = {};
  s = parseUnixTimeWithNanoSec(input.begin(), input.end(), time);
  ASSERT_EQ(ParseTimespecStatus::OK, s);
  ASSERT_EQ(-214748364799, time.tv_sec);
  ASSERT_EQ(0, time.tv_nsec);
  ASSERT_EQ("-4836/11/23 12:26:41", toUTC(time.tv_sec));

  input = "-214748364799.888000000";
  time = {};
  s = parseUnixTimeWithNanoSec(input.begin(), input.end(), time);
  ASSERT_EQ(ParseTimespecStatus::OK, s);
  ASSERT_EQ(-214748364800, time.tv_sec);
  ASSERT_EQ(112000000, time.tv_nsec);
  ASSERT_EQ("-4836/11/23 12:26:40", toUTC(time.tv_sec));

  // error
  input = "999999999999999214748364799.987654321";
  time = {};
  s = parseUnixTimeWithNanoSec(input.begin(), input.end(), time);
  ASSERT_EQ(ParseTimespecStatus::INVALID_UNIX_TIME, s);
  ASSERT_EQ(0, time.tv_sec);
  ASSERT_EQ(0, time.tv_nsec);

  input = "AAAAAdddd";
  time = {};
  s = parseUnixTimeWithNanoSec(input.begin(), input.end(), time);
  ASSERT_EQ(ParseTimespecStatus::INVALID_UNIX_TIME, s);
  ASSERT_EQ(0, time.tv_sec);
  ASSERT_EQ(0, time.tv_nsec);

  input = "214748364799@";
  time = {};
  s = parseUnixTimeWithNanoSec(input.begin(), input.end(), time);
  ASSERT_EQ(ParseTimespecStatus::INVALID_UNIX_TIME, s);
  ASSERT_EQ(0, time.tv_sec);
  ASSERT_EQ(0, time.tv_nsec);

  input = "214748364799.";
  time = {};
  s = parseUnixTimeWithNanoSec(input.begin(), input.end(), time);
  ASSERT_EQ(ParseTimespecStatus::INVALID_NANO_SEC, s);
  ASSERT_EQ(214748364799, time.tv_sec);
  ASSERT_EQ(0, time.tv_nsec);

  input = "214748364799.a";
  time = {};
  s = parseUnixTimeWithNanoSec(input.begin(), input.end(), time);
  ASSERT_EQ(ParseTimespecStatus::INVALID_NANO_SEC, s);
  ASSERT_EQ(214748364799, time.tv_sec);
  ASSERT_EQ(0, time.tv_nsec);

  input = "214748364799.9999999999";
  time = {};
  s = parseUnixTimeWithNanoSec(input.begin(), input.end(), time);
  ASSERT_EQ(ParseTimespecStatus::INVALID_NANO_SEC, s);
  ASSERT_EQ(214748364799, time.tv_sec);
  ASSERT_EQ(0, time.tv_nsec);

  input = "214748364799..6";
  time = {};
  s = parseUnixTimeWithNanoSec(input.begin(), input.end(), time);
  ASSERT_EQ(ParseTimespecStatus::INVALID_NANO_SEC, s);
  ASSERT_EQ(214748364799, time.tv_sec);
  ASSERT_EQ(0, time.tv_nsec);

  input = "";
  time = {};
  s = parseUnixTimeWithNanoSec(input.begin(), input.end(), time);
  ASSERT_EQ(ParseTimespecStatus::INVALID_UNIX_TIME, s);
  ASSERT_EQ(0, time.tv_sec);
  ASSERT_EQ(0, time.tv_nsec);

  input = ".12";
  time = {};
  s = parseUnixTimeWithNanoSec(input.begin(), input.end(), time);
  ASSERT_EQ(ParseTimespecStatus::INVALID_UNIX_TIME, s);
  ASSERT_EQ(0, time.tv_sec);
  ASSERT_EQ(0, time.tv_nsec);
}

TEST(PathTest, base) {
  struct {
    std::string path;
    std::string dirName;
    std::string baseName;
  } table[] = {
      {"", ".", ""},
      {"/", "/", "/"},
      {"////", "/", "/"},
      {"///home///work//", "///home", "work"},
      {"/usr/bin/zip", "/usr/bin", "zip"},
      {"/etc/passwd///", "/etc", "passwd"},
      {"/etc////passwd", "/etc", "passwd"},
      {"etc/passwd///", "etc", "passwd"},
      {"passwd", ".", "passwd"},
      {"passwd/", ".", "passwd"},
      {".", ".", "."},
      {"..", ".", ".."},
  };

  for (auto &e : table) {
    ASSERT_EQ(e.dirName, getDirname(e.path).toString());
    ASSERT_EQ(e.baseName, getBasename(e.path).toString());
  }
}

TEST(CamelSplitTest, base) {
  struct {
    std::string identifier;
    std::vector<std::string> words;
  } table[] = {
      {"", {}},
      {"a", {"a"}},
      {"ab", {"ab"}},
      {"A", {"A"}},
      {"AA", {"AA"}},
      {"_", {}},
      {"__", {}},
      {"___", {}},
      {"ASCIICode", {"ASCII", "Code"}},
      {"camelCase", {"camel", "Case"}},
      {"UpperCamelCase", {"Upper", "Camel", "Case"}},
      {"_kebab__case__", {"kebab", "case"}},
      {"utf8CodePoint", {"utf8", "Code", "Point"}},
      {"P2PProtocol", {"P2", "P", "Protocol"}},
      {"HTTPResponseCodeXYZ", {"HTTP", "Response", "Code", "XYZ"}},
      {"Upper0C1Amel0Case", {"Upper0", "C1", "Amel0", "Case"}},
  };

  for (auto &e : table) {
    SCOPED_TRACE("for: " + e.identifier);
    std::vector<std::string> words;
    splitCamelCaseIdentifier(e.identifier,
                             [&words](StringRef ref) { words.push_back(ref.toString()); });
    ASSERT_EQ(e.words, words);
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
