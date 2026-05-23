#include "../test_common.h"

#include <iostream>

#include "../tools/casefold/fold_table.h"
#include "misc/split_random.hpp"

using namespace arsh::fold;

struct FOLD_TEST_ENTRY {
  char type;
  int before;
  int after;
};

inline std::ostream &operator<<(std::ostream &stream, const FOLD_TEST_ENTRY &e) {
  return stream << format("(type=%c, before=U+%04X, after=U+%04X)", e.type, e.before, e.after);
}

struct CASE_FOLD_F_ENTRY { // dummy
  template <typename... T>
  constexpr CASE_FOLD_F_ENTRY(T...) {} // NOLINT
};

#include "./case_fold.in"

static const FoldTable &getFoldTable() {
  static const FoldTable table = FoldTable::create();
  return table;
}

struct FoldTableTest : public ::testing::TestWithParam<FOLD_TEST_ENTRY> {
  static void doTest() {
    auto &param = GetParam();
    if (param.type == 'T') {
      std::cerr << "skip: " << param << '\n';
      return;
    }
    auto &table = getFoldTable();
    auto [after, type] = table.fold(param.before);
    ASSERT_TRUE(type == 'C' || type == 'S');
    ASSERT_EQ(param.after, after);
    ASSERT_EQ(param.type, type);
  }
};

TEST_P(FoldTableTest, base) { ASSERT_NO_FATAL_FAILURE(doTest()); }

INSTANTIATE_TEST_SUITE_P(FoldTableTest, FoldTableTest, ::testing::ValuesIn(fold_table));

struct NoFoldTest : public ::testing::TestWithParam<int> {
  static void doTest() {
    const int codePoint = GetParam();
    auto &table = getFoldTable();
    auto [after, type] = table.fold(codePoint);
    ASSERT_EQ(after, codePoint);
    ASSERT_EQ('C', type);
  }
};

static std::vector<int> generateNoFoldTestCases() {
  std::unordered_set<int> foldSet;
  for (auto &e : fold_table) {
    foldSet.emplace(e.before);
  }
  std::vector<int> values;
  arsh::L64X128MixRNG rng(30);
  while (values.size() < 500) {
    uint64_t v = rng.next();
    int codePoint = static_cast<int>(v & ((1 << 22) - 1));
    if (arsh::UnicodeUtil::isCodePoint(codePoint) && foldSet.find(codePoint) == foldSet.end()) {
      values.push_back(codePoint);
    }
  }
  return values;
}

TEST_P(NoFoldTest, base) { ASSERT_NO_FATAL_FAILURE(doTest()); }

INSTANTIATE_TEST_SUITE_P(NoFoldTest, NoFoldTest, ::testing::ValuesIn(generateNoFoldTestCases()));

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}