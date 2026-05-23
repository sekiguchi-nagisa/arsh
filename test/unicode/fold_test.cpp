#include "../test_common.h"

#include "misc/split_random.hpp"
#include "unicode/case_fold.h"

struct FOLD_TEST_ENTRY {
  char type;
  int before;
  int after;
};

inline std::ostream &operator<<(std::ostream &stream, const FOLD_TEST_ENTRY &e) {
  return stream << format("(type=%c, before=U+%04X, after=U+%04X)", e.type, e.before, e.after);
}

struct CASE_FOLD_F_ENTRY {
  int code;
  std::array<uint16_t, arsh::CaseFoldingResult::FULL_FOLD_ENTRY_SIZE> values;

  constexpr CASE_FOLD_F_ENTRY(uint16_t v0, uint16_t v1, uint16_t v2, uint16_t v3)
      : code(v0), values{v1, v2, v3} {}
};

inline std::ostream &operator<<(std::ostream &stream, const CASE_FOLD_F_ENTRY &e) {
  return stream << format("(before=U+%04X, after=[U+%04X, U+%04X, U+%04X])", e.code, e.values[0],
                          e.values[1], e.values[2]);
}

#include "./case_fold.in"

struct FoldTest : public ::testing::TestWithParam<FOLD_TEST_ENTRY> {
  static void doTest() {
    auto &param = GetParam();
    auto ret = arsh::doCaseFolding(param.before, arsh::CaseFoldOp::NONE);
    auto retTurkic = arsh::doCaseFolding(param.before, arsh::CaseFoldOp::TURKIC);
    auto retFull = arsh::doCaseFolding(param.before, arsh::CaseFoldOp::FULL_FOLD);
    auto code = arsh::doSimpleCaseFolding(param.before);

    switch (param.type) {
    case 'C':
      ASSERT_FALSE(ret.isFullFolding());
      ASSERT_EQ(param.after, ret.getSimpleFolding());
      ASSERT_FALSE(retFull.isFullFolding());
      ASSERT_EQ(param.after, retFull.getSimpleFolding());
      ASSERT_EQ(param.after, code);
      break;
    case 'S':
      ASSERT_FALSE(ret.isFullFolding());
      ASSERT_EQ(param.after, ret.getSimpleFolding());
      ASSERT_FALSE(retTurkic.isFullFolding());
      ASSERT_EQ(param.after, retTurkic.getSimpleFolding());
      ASSERT_FALSE(retFull.equals(param.after));
      ASSERT_EQ(param.after, code);
      break;
    case 'T':
      ASSERT_FALSE(ret.isFullFolding());
      ASSERT_NE(param.after, ret.getSimpleFolding());
      ASSERT_FALSE(retTurkic.isFullFolding());
      ASSERT_EQ(param.after, retTurkic.getSimpleFolding());
      ASSERT_FALSE(retFull.equals(param.after));
      break;
    default:
      ASSERT_EQ('C', param.type);
      break;
    }
  }
};

TEST_P(FoldTest, base) { ASSERT_NO_FATAL_FAILURE(doTest()); }

INSTANTIATE_TEST_SUITE_P(FoldTest, FoldTest, ::testing::ValuesIn(fold_table));

struct FullFoldTest : public ::testing::TestWithParam<CASE_FOLD_F_ENTRY> {
  static void doTest() {
    auto &param = GetParam();
    auto ret = arsh::doCaseFolding(param.code, arsh::CaseFoldOp::NONE);
    ASSERT_FALSE(ret.isFullFolding());
    ret = arsh::doCaseFolding(param.code, arsh::CaseFoldOp::FULL_FOLD);
    ASSERT_TRUE(ret.isFullFolding());
    ASSERT_EQ(param.values, ret.getFullFolding());
  }
};

TEST_P(FullFoldTest, base) { ASSERT_NO_FATAL_FAILURE(doTest()); }

INSTANTIATE_TEST_SUITE_P(FullFoldTest, FullFoldTest, ::testing::ValuesIn(case_fold_F_table));

static std::vector<int> generateNoFoldCases() {
  std::unordered_set<int> foldSet;
  for (auto &e : fold_table) {
    foldSet.emplace(e.before);
  }
  for (auto &e : case_fold_F_table) {
    foldSet.emplace(e.code);
  }
  std::vector<int> values;
  arsh::L64X128MixRNG rng(42);
  while (values.size() < 500) {
    uint64_t v = rng.next();
    int codePoint = static_cast<int>(v & ((1 << 22) - 1));
    if (arsh::UnicodeUtil::isCodePoint(codePoint) && foldSet.find(codePoint) == foldSet.end()) {
      values.push_back(codePoint);
    }
  }
  return values;
}

struct NoFoldTest : public ::testing::TestWithParam<int> {
  static void doTest() {
    auto &param = GetParam();
    auto ret = arsh::doCaseFolding(param, arsh::CaseFoldOp::NONE);
    ASSERT_FALSE(ret.isFullFolding());
    ASSERT_EQ(param, ret.getSimpleFolding());
    ret = arsh::doCaseFolding(param, arsh::CaseFoldOp::FULL_FOLD | arsh::CaseFoldOp::TURKIC);
    ASSERT_FALSE(ret.isFullFolding());
    ASSERT_EQ(param, ret.getSimpleFolding());
    ASSERT_EQ(param, arsh::doSimpleCaseFolding(param));
  }
};

TEST_P(NoFoldTest, base) { ASSERT_NO_FATAL_FAILURE(doTest()); }

INSTANTIATE_TEST_SUITE_P(NoFoldTest, NoFoldTest, ::testing::ValuesIn(generateNoFoldCases()));

TEST(TurkicFoldTest, base) {
  auto ret = arsh::doCaseFolding(0x0049, arsh::CaseFoldOp::NONE);
  ASSERT_EQ(0x0069, ret.getSimpleFolding());
  ret = arsh::doCaseFolding(0x0049, arsh::CaseFoldOp::TURKIC);
  ASSERT_EQ(0x0131, ret.getSimpleFolding());

  ret = arsh::doCaseFolding(0x0130, arsh::CaseFoldOp::NONE);
  ASSERT_FALSE(ret.isFullFolding());
  ASSERT_EQ(0x0130, ret.getSimpleFolding());

  ASSERT_EQ(0x0069, arsh::doSimpleCaseFolding(0x0069));

  ret = arsh::doCaseFolding(0x0130, arsh::CaseFoldOp::TURKIC);
  ASSERT_FALSE(ret.isFullFolding());
  ASSERT_EQ(0x0069, ret.getSimpleFolding());

  ret = arsh::doCaseFolding(0x0130, arsh::CaseFoldOp::FULL_FOLD);
  ASSERT_TRUE(ret.isFullFolding());
  ASSERT_EQ(0x0069, ret.getFullFolding()[0]);
  ASSERT_EQ(0x0307, ret.getFullFolding()[1]);
  ASSERT_EQ(0x0000, ret.getFullFolding()[2]);

  ret = arsh::doCaseFolding(0x0130, arsh::CaseFoldOp::TURKIC | arsh::CaseFoldOp::FULL_FOLD);
  ASSERT_FALSE(ret.isFullFolding());
  ASSERT_EQ(0x0069, ret.getSimpleFolding());

  ASSERT_EQ(0x0130, arsh::doSimpleCaseFolding(0x0130));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}