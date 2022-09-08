#include "gtest/gtest.h"

#include <brace.h>

using namespace ydsh;

TEST(BraceSeqTest, base1) {
  {
    StringRef ref = "a..z";
    auto range = toBraceRange(ref, true);
    ASSERT_EQ(BraceRange::Kind::CHAR, range.kind);
    ASSERT_EQ('a', range.begin);
    ASSERT_EQ('z', range.end);
    ASSERT_EQ(1, range.step);
    ASSERT_EQ(0, range.digits);
  }

  {
    StringRef ref = "Z..0";
    auto range = toBraceRange(ref, true);
    ASSERT_EQ(BraceRange::Kind::CHAR, range.kind);
    ASSERT_EQ('Z', range.begin);
    ASSERT_EQ('0', range.end);
    ASSERT_EQ(1, range.step);
    ASSERT_EQ(0, range.digits);
  }

  {
    StringRef ref = "Z..0..00003";
    auto range = toBraceRange(ref, true);
    ASSERT_EQ(BraceRange::Kind::CHAR, range.kind);
    ASSERT_EQ('Z', range.begin);
    ASSERT_EQ('0', range.end);
    ASSERT_EQ(3, range.step);
    ASSERT_EQ(0, range.digits);
  }

  {
    StringRef ref = "Z..0..-1";
    auto range = toBraceRange(ref, true);
    ASSERT_EQ(BraceRange::Kind::CHAR, range.kind);
    ASSERT_EQ('0', range.begin);
    ASSERT_EQ('Z', range.end);
    ASSERT_EQ(1, range.step);
    ASSERT_EQ(0, range.digits);
  }

  {
    StringRef ref = "1..9..-5";
    auto range = toBraceRange(ref, true);
    ASSERT_EQ(BraceRange::Kind::CHAR, range.kind);
    ASSERT_EQ('6', range.begin);
    ASSERT_EQ('1', range.end);
    ASSERT_EQ(5, range.step);
    ASSERT_EQ(0, range.digits);
  }

  {
    StringRef ref = "0..9..-2";
    auto range = toBraceRange(ref, true);
    ASSERT_EQ(BraceRange::Kind::CHAR, range.kind);
    ASSERT_EQ('8', range.begin);
    ASSERT_EQ('0', range.end);
    ASSERT_EQ(2, range.step);
    ASSERT_EQ(0, range.digits);
  }

  {
    StringRef ref = "Z..0..-0";
    auto range = toBraceRange(ref, true);
    ASSERT_EQ(BraceRange::Kind::CHAR, range.kind);
    ASSERT_EQ('Z', range.begin);
    ASSERT_EQ('0', range.end);
    ASSERT_EQ(1, range.step);
    ASSERT_EQ(0, range.digits);
  }

  {
    StringRef ref = "Z..0..9223372036854775807";
    auto range = toBraceRange(ref, true);
    ASSERT_EQ(BraceRange::Kind::CHAR, range.kind);
    ASSERT_EQ('Z', range.begin);
    ASSERT_EQ('0', range.end);
    ASSERT_EQ(INT64_MAX, range.step);
    ASSERT_EQ(0, range.digits);
  }

  {
    StringRef ref = "Z..0..9223372036854775808";
    auto range = toBraceRange(ref, true);
    ASSERT_EQ(BraceRange::Kind::OUT_OF_RANGE_STEP, range.kind);
  }

  {
    StringRef ref = "a..F..-9223372036854775807";
    auto range = toBraceRange(ref, true);
    ASSERT_EQ(BraceRange::Kind::CHAR, range.kind);
    ASSERT_EQ('a', range.begin);
    ASSERT_EQ('a', range.end);
    ASSERT_EQ(INT64_MAX, range.step);
    ASSERT_EQ(0, range.digits);
  }

  {
    StringRef ref = "a..F..-9223372036854775808";
    auto range = toBraceRange(ref, true);
    ASSERT_EQ(BraceRange::Kind::OUT_OF_RANGE_STEP, range.kind);
  }

  {
    StringRef ref = "a..F..-9223372036854775809";
    auto range = toBraceRange(ref, true);
    ASSERT_EQ(BraceRange::Kind::OUT_OF_RANGE_STEP, range.kind);
  }
}

TEST(BraceSeqTest, base2) {
  {
    StringRef ref = "+1..10";
    auto range = toBraceRange(ref, false);
    ASSERT_EQ(BraceRange::Kind::INT, range.kind);
    ASSERT_EQ(1, range.begin);
    ASSERT_EQ(10, range.end);
    ASSERT_EQ(1, range.step);
    ASSERT_EQ(0, range.digits);
  }

  {
    StringRef ref = "-001..+10..1";
    auto range = toBraceRange(ref, false);
    ASSERT_EQ(BraceRange::Kind::INT, range.kind);
    ASSERT_EQ(-1, range.begin);
    ASSERT_EQ(10, range.end);
    ASSERT_EQ(1, range.step);
    ASSERT_EQ(4, range.digits);
  }

  {
    StringRef ref = "-001..+10..00022";
    auto range = toBraceRange(ref, false);
    ASSERT_EQ(BraceRange::Kind::INT, range.kind);
    ASSERT_EQ(-1, range.begin);
    ASSERT_EQ(10, range.end);
    ASSERT_EQ(22, range.step);
    ASSERT_EQ(4, range.digits);
  }

  {
    StringRef ref = "-000..+00011..-22";
    auto range = toBraceRange(ref, false);
    ASSERT_EQ(BraceRange::Kind::INT, range.kind);
    ASSERT_EQ(0, range.begin);
    ASSERT_EQ(0, range.end);
    ASSERT_EQ(22, range.step);
    ASSERT_EQ(5, range.digits);
  }

  {
    StringRef ref = "-0..-234..-22";
    auto range = toBraceRange(ref, false);
    ASSERT_EQ(BraceRange::Kind::INT, range.kind);
    ASSERT_EQ(-220, range.begin);
    ASSERT_EQ(0, range.end);
    ASSERT_EQ(22, range.step);
    ASSERT_EQ(0, range.digits);
  }

  {
    StringRef ref = "+0..-234..0";
    auto range = toBraceRange(ref, false);
    ASSERT_EQ(BraceRange::Kind::INT, range.kind);
    ASSERT_EQ(0, range.begin);
    ASSERT_EQ(-234, range.end);
    ASSERT_EQ(1, range.step);
    ASSERT_EQ(0, range.digits);
  }

  {
    StringRef ref = "-009223372036854775808..9223372036854775807..-1";
    auto range = toBraceRange(ref, false);
    ASSERT_EQ(BraceRange::Kind::INT, range.kind);
    ASSERT_EQ(INT64_MAX, range.begin);
    ASSERT_EQ(INT64_MIN, range.end);
    ASSERT_EQ(1, range.step);
    ASSERT_EQ(strlen("-009223372036854775807"), range.digits);
  }

  {
    StringRef ref = "-009223372036854775808..9223372036854775807..-7";
    auto range = toBraceRange(ref, false);
    ASSERT_EQ(BraceRange::Kind::INT, range.kind);
    ASSERT_EQ(9223372036854775806, range.begin);
    ASSERT_EQ(INT64_MIN, range.end);
    ASSERT_EQ(7, range.step);
    ASSERT_EQ(strlen("-009223372036854775807"), range.digits);
  }

  {
    StringRef ref = "009223372036854775807..-9223372036854775808..-7";
    auto range = toBraceRange(ref, false);
    ASSERT_EQ(BraceRange::Kind::INT, range.kind);
    ASSERT_EQ(-9223372036854775807, range.begin);
    ASSERT_EQ(INT64_MAX, range.end);
    ASSERT_EQ(7, range.step);
    ASSERT_EQ(strlen("009223372036854775807"), range.digits);
  }

  {
    StringRef ref = "-009223372036854775807..9223372036854775807..100";
    auto range = toBraceRange(ref, false);
    ASSERT_EQ(BraceRange::Kind::INT, range.kind);
    ASSERT_EQ(INT64_MIN + 1, range.begin);
    ASSERT_EQ(INT64_MAX, range.end);
    ASSERT_EQ(100, range.step);
    ASSERT_EQ(strlen("-009223372036854775807"), range.digits);
  }

  {
    StringRef ref = "088..-009223372036854775808";
    auto range = toBraceRange(ref, false);
    ASSERT_EQ(BraceRange::Kind::INT, range.kind);
    ASSERT_EQ(88, range.begin);
    ASSERT_EQ(INT64_MIN, range.end);
    ASSERT_EQ(1, range.step);
    ASSERT_EQ(strlen("-009223372036854775808"), range.digits);
  }

  {
    StringRef ref = "088..000009223372036854775807";
    auto range = toBraceRange(ref, false);
    ASSERT_EQ(BraceRange::Kind::INT, range.kind);
    ASSERT_EQ(88, range.begin);
    ASSERT_EQ(INT64_MAX, range.end);
    ASSERT_EQ(1, range.step);
    ASSERT_EQ(strlen("000009223372036854775807"), range.digits);
  }

  {
    StringRef ref = "-009223372036854775809..+92233720..100";
    auto range = toBraceRange(ref, false);
    ASSERT_EQ(BraceRange::Kind::OUT_OF_RANGE, range.kind);
  }

  {
    StringRef ref = "-0099..9223372036854775808..100";
    auto range = toBraceRange(ref, false);
    ASSERT_EQ(BraceRange::Kind::OUT_OF_RANGE, range.kind);
  }

  {
    StringRef ref = "-0099..9223372036854775809..100";
    auto range = toBraceRange(ref, false);
    ASSERT_EQ(BraceRange::Kind::OUT_OF_RANGE, range.kind);
  }

  {
    StringRef ref = "-0099..922339..99999999999999999999999999999";
    auto range = toBraceRange(ref, false);
    ASSERT_EQ(BraceRange::Kind::OUT_OF_RANGE_STEP, range.kind);
  }
}

TEST(BraceSeqTest, digits) {
  auto v = formatSeqValue(12, 0, false);
  ASSERT_EQ("12", v);

  v = formatSeqValue(12, 5, false);
  ASSERT_EQ("00012", v);

  v = formatSeqValue('a', 0, true);
  ASSERT_EQ("a", v);

  v = formatSeqValue('a', 5, true);
  ASSERT_EQ("a", v);

  v = formatSeqValue(-12, 0, false);
  ASSERT_EQ("-12", v);

  v = formatSeqValue(-12, 3, false);
  ASSERT_EQ("-12", v);

  v = formatSeqValue(-12, 7, false);
  ASSERT_EQ("-000012", v);
}

TEST(BraceSeqTest, update_inc) {
  int64_t v = 12;
  BraceRange range = {
      .begin = 12,
      .end = 15,
      .step = 1,
      .digits = 0,
      .kind = BraceRange::Kind::INT,
  };

  ASSERT_TRUE(tryUpdateSeqValue(v, range));
  ASSERT_EQ(13, v);

  ASSERT_TRUE(tryUpdateSeqValue(v, range));
  ASSERT_EQ(14, v);

  ASSERT_TRUE(tryUpdateSeqValue(v, range));
  ASSERT_EQ(15, v);

  ASSERT_FALSE(tryUpdateSeqValue(v, range));
  ASSERT_EQ(15, v);

  v = 12;
  range = {
      .begin = 12,
      .end = 17,
      .step = 2,
      .digits = 0,
      .kind = BraceRange::Kind::INT,
  };

  ASSERT_TRUE(tryUpdateSeqValue(v, range));
  ASSERT_EQ(14, v);

  ASSERT_TRUE(tryUpdateSeqValue(v, range));
  ASSERT_EQ(16, v);

  ASSERT_FALSE(tryUpdateSeqValue(v, range));
  ASSERT_EQ(16, v);

  v = INT64_MAX - 10;
  range = {
      .begin = INT64_MAX - 10,
      .end = INT64_MAX,
      .step = 20,
      .digits = 0,
      .kind = BraceRange::Kind::INT,
  };

  ASSERT_FALSE(tryUpdateSeqValue(v, range));
  ASSERT_EQ(INT64_MAX - 10, v);
}

TEST(BraceSeqTest, update_dec) {
  int64_t v = 18;
  BraceRange range = {
      .begin = 18,
      .end = 15,
      .step = 1,
      .digits = 0,
      .kind = BraceRange::Kind::INT,
  };

  ASSERT_TRUE(tryUpdateSeqValue(v, range));
  ASSERT_EQ(17, v);

  ASSERT_TRUE(tryUpdateSeqValue(v, range));
  ASSERT_EQ(16, v);

  ASSERT_TRUE(tryUpdateSeqValue(v, range));
  ASSERT_EQ(15, v);

  ASSERT_FALSE(tryUpdateSeqValue(v, range));
  ASSERT_EQ(15, v);

  v = 18;
  range = {
      .begin = 18,
      .end = 13,
      .step = 2,
      .digits = 0,
      .kind = BraceRange::Kind::INT,
  };
  ASSERT_TRUE(tryUpdateSeqValue(v, range));
  ASSERT_EQ(16, v);

  ASSERT_TRUE(tryUpdateSeqValue(v, range));
  ASSERT_EQ(14, v);

  ASSERT_FALSE(tryUpdateSeqValue(v, range));
  ASSERT_EQ(14, v);

  v = INT64_MIN + 100;
  range = {
      .begin = INT64_MIN + 100,
      .end = INT64_MIN,
      .step = INT64_MAX,
      .digits = 0,
      .kind = BraceRange::Kind::INT,
  };

  ASSERT_FALSE(tryUpdateSeqValue(v, range));
  ASSERT_EQ(INT64_MIN + 100, v);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
