#include "misc/unicode.hpp"

#include "gtest/gtest.h"

#include <misc/codepoint_set.hpp>
#include <misc/unicode.hpp>
#include <unicode/property.h>
#include <unicode/set_builder.h>

using namespace arsh;

TEST(CodePointSetRefTest, base) {
  // unicode17, `cat:Nl`
  static constexpr std::pair<uint16_t, uint16_t> orgBMP[] = {
      {0x16EE, 0x16F0}, {0x2160, 0x2182}, {0x2185, 0x2188}, {0x3007, 0x3007},
      {0x3021, 0x3029}, {0x3038, 0x303A}, {0xA6E6, 0xA6EF},
  };
  static constexpr std::pair<int, int> orgNonBMP[] = {
      {0x10140, 0x10174}, {0x10341, 0x10341}, {0x1034A, 0x1034A},
      {0x103D1, 0x103D5}, {0x12400, 0x1246E},
  };

  static constexpr BMPCodePointRange table[] = {
      // clang-format off
      // BMP
      {0x16EE, 0x16F0},
      {0x2160, 0x2182},
      {0x2185, 0x2188},
      {0x3007, 0x3007},
      {0x3021, 0x3029},
      {0x3038, 0x303A},
      {0xA6E6, 0xA6EF},
      // NON-BMP
      {0x10140},{0x10174},
      {0x10341},{0x10341},
      {0x1034A},{0x1034A},
      {0x103D1},{0x103D5},
      {0x12400},{0x1246E},
      // clang-format on
  };
  CodePointSetRef setRef(7, table, std::size(table));

  // BMP
  const auto bmpRanges = setRef.getBMPRanges();
  ASSERT_EQ(std::size(orgBMP), bmpRanges.size());
  for (size_t i = 0; i < std::size(orgBMP); i++) {
    SCOPED_TRACE("index=" + std::to_string(i));
    ASSERT_EQ(orgBMP[i].first, bmpRanges[i].firstBMP());
    ASSERT_EQ(orgBMP[i].second, bmpRanges[i].lastBMP());
  }

  // NonBMP
  const auto nonBmpRanges = setRef.getNonBMPRanges();
  ASSERT_EQ(std::size(orgNonBMP), nonBmpRanges.size());
  for (size_t i = 0; i < std::size(orgNonBMP); i++) {
    SCOPED_TRACE("index=" + std::to_string(i));
    ASSERT_EQ(orgNonBMP[i].first, nonBmpRanges[i].firstNonBMP());
    ASSERT_EQ(orgNonBMP[i].second, nonBmpRanges[i].lastNonBMP());
  }

  // lookup
  for (unsigned int i = 0; i < std::size(orgBMP); i++) {
    SCOPED_TRACE("index=" + std::to_string(i));
    auto [first, last] = orgBMP[i];
    for (; first <= last; first++) {
      SCOPED_TRACE("codePoint=" + std::to_string(first));
      ASSERT_TRUE(setRef.contains(first));
    }
  }
  for (unsigned int i = 0; i < std::size(orgNonBMP); i++) {
    SCOPED_TRACE("index=" + std::to_string(i));
    auto [first, last] = orgNonBMP[i];
    for (; first <= last; first++) {
      SCOPED_TRACE("codePoint=" + std::to_string(first));
      ASSERT_TRUE(setRef.contains(first));
    }
  }

  static constexpr int notfound[] = {
      // clang-format off
      0, -1, -2,-3, 1, 2,3, 0xFF, 0xFFF,
      0x16EE - 1, 0x16F0 + 1, 0x2160 - 14, 0x2182 + 1, 0x2182 + 2,
      0x96E6, 0xFFFF, 0xBBB, 0x10139, 0x10175,
      0x10341 + 1, 0x10341 + 2, 0x10341 + 3, 0x103D1 - 1,
      0x12400 - 1, 0x12400 - 2, 0x12400 - 33, 0x1246E + 1,
      0x1246E + 2, 0x1246E + 999,
      // clang-format on
  };
  for (auto &e : notfound) {
    ASSERT_FALSE(setRef.contains(e));
  }
}

TEST(CodePointSetRefTest, bmpOnly) {
  // unicode17, `cat:Me`
  static constexpr std::pair<uint16_t, uint16_t> orgBMP[] = {
      {0x0488, 0x0489}, {0x1ABE, 0x1ABE}, {0x20DD, 0x20E0}, {0x20E2, 0x20E4}, {0xA670, 0xA672},
  };

  static constexpr BMPCodePointRange table[] = {
      // clang-format off
      { 0x0488, 0x0489 },
      { 0x1ABE, 0x1ABE },
      { 0x20DD, 0x20E0 },
      { 0x20E2, 0x20E4 },
      { 0xA670, 0xA672 },
      // clang-format on
  };
  CodePointSetRef setRef(5, table, std::size(table));

  // BMP
  const auto bmpRanges = setRef.getBMPRanges();
  ASSERT_EQ(std::size(orgBMP), bmpRanges.size());
  for (size_t i = 0; i < std::size(orgBMP); i++) {
    SCOPED_TRACE("index=" + std::to_string(i));
    ASSERT_EQ(orgBMP[i].first, bmpRanges[i].firstBMP());
    ASSERT_EQ(orgBMP[i].second, bmpRanges[i].lastBMP());
  }

  // NonBMP
  const auto nonBmpRanges = setRef.getNonBMPRanges();
  ASSERT_EQ(0, nonBmpRanges.size());

  // lookup
  for (unsigned int i = 0; i < std::size(orgBMP); i++) {
    SCOPED_TRACE("index=" + std::to_string(i));
    auto [first, last] = orgBMP[i];
    for (; first <= last; first++) {
      SCOPED_TRACE("codePoint=" + std::to_string(first));
      ASSERT_TRUE(setRef.contains(first));
    }
  }
}

TEST(CodePointSetRefTest, nonBmpOnly) {
  // unicode17, eaw:N (non-bmp only)
  static constexpr std::pair<int, int> orgNonBMP[] = {
      {0x10000, 0x16FDF},   {0x16FE5, 0x16FEF}, {0x16FF2, 0x16FFF}, {0x187F8, 0x187FF},
      {0x18CD6, 0x18CFE},   {0x18D09, 0x1AFEF}, {0x1AFF4, 0x1AFF4}, {0x1AFFC, 0x1AFFC},
      {0x1AFFF, 0x1AFFF},   {0x1B123, 0x1B131}, {0x1B133, 0x1B14F}, {0x1B153, 0x1B154},
      {0x1B156, 0x1B163},   {0x1B168, 0x1B16F}, {0x1B2FC, 0x1D2FF}, {0x1D357, 0x1D35F},
      {0x1D377, 0x1F003},   {0x1F005, 0x1F0CE}, {0x1F0D0, 0x1F0FF}, {0x1F10B, 0x1F10F},
      {0x1F12E, 0x1F12F},   {0x1F16A, 0x1F16F}, {0x1F1AD, 0x1F1FF}, {0x1F203, 0x1F20F},
      {0x1F23C, 0x1F23F},   {0x1F249, 0x1F24F}, {0x1F252, 0x1F25F}, {0x1F266, 0x1F2FF},
      {0x1F321, 0x1F32C},   {0x1F336, 0x1F336}, {0x1F37D, 0x1F37D}, {0x1F394, 0x1F39F},
      {0x1F3CB, 0x1F3CE},   {0x1F3D4, 0x1F3DF}, {0x1F3F1, 0x1F3F3}, {0x1F3F5, 0x1F3F7},
      {0x1F43F, 0x1F43F},   {0x1F441, 0x1F441}, {0x1F4FD, 0x1F4FE}, {0x1F53E, 0x1F54A},
      {0x1F54F, 0x1F54F},   {0x1F568, 0x1F579}, {0x1F57B, 0x1F594}, {0x1F597, 0x1F5A3},
      {0x1F5A5, 0x1F5FA},   {0x1F650, 0x1F67F}, {0x1F6C6, 0x1F6CB}, {0x1F6CD, 0x1F6CF},
      {0x1F6D3, 0x1F6D4},   {0x1F6D8, 0x1F6DB}, {0x1F6E0, 0x1F6EA}, {0x1F6ED, 0x1F6F3},
      {0x1F6FD, 0x1F7DF},   {0x1F7EC, 0x1F7EF}, {0x1F7F1, 0x1F90B}, {0x1F93B, 0x1F93B},
      {0x1F946, 0x1F946},   {0x1FA00, 0x1FA6F}, {0x1FA7D, 0x1FA7F}, {0x1FA8A, 0x1FA8E},
      {0x1FAC7, 0x1FACD},   {0x1FADD, 0x1FADE}, {0x1FAEA, 0x1FAEF}, {0x1FAF9, 0x1FFFF},
      {0x2FFFE, 0x2FFFF},   {0x3FFFE, 0xE00FF}, {0xE01F0, 0xEFFFF}, {0xFFFFE, 0xFFFFF},
      {0x10FFFE, 0x10FFFF},
  };

  static constexpr BMPCodePointRange table[] = {
      // clang-format off
      {0x10000}, {0x16FDF},   {0x16FE5}, {0x16FEF}, {0x16FF2}, {0x16FFF}, {0x187F8}, {0x187FF},
      {0x18CD6}, {0x18CFE},   {0x18D09}, {0x1AFEF}, {0x1AFF4}, {0x1AFF4}, {0x1AFFC}, {0x1AFFC},
      {0x1AFFF}, {0x1AFFF},   {0x1B123}, {0x1B131}, {0x1B133}, {0x1B14F}, {0x1B153}, {0x1B154},
      {0x1B156}, {0x1B163},   {0x1B168}, {0x1B16F}, {0x1B2FC}, {0x1D2FF}, {0x1D357}, {0x1D35F},
      {0x1D377}, {0x1F003},   {0x1F005}, {0x1F0CE}, {0x1F0D0}, {0x1F0FF}, {0x1F10B}, {0x1F10F},
      {0x1F12E}, {0x1F12F},   {0x1F16A}, {0x1F16F}, {0x1F1AD}, {0x1F1FF}, {0x1F203}, {0x1F20F},
      {0x1F23C}, {0x1F23F},   {0x1F249}, {0x1F24F}, {0x1F252}, {0x1F25F}, {0x1F266}, {0x1F2FF},
      {0x1F321}, {0x1F32C},   {0x1F336}, {0x1F336}, {0x1F37D}, {0x1F37D}, {0x1F394}, {0x1F39F},
      {0x1F3CB}, {0x1F3CE},   {0x1F3D4}, {0x1F3DF}, {0x1F3F1}, {0x1F3F3}, {0x1F3F5}, {0x1F3F7},
      {0x1F43F}, {0x1F43F},   {0x1F441}, {0x1F441}, {0x1F4FD}, {0x1F4FE}, {0x1F53E}, {0x1F54A},
      {0x1F54F}, {0x1F54F},   {0x1F568}, {0x1F579}, {0x1F57B}, {0x1F594}, {0x1F597}, {0x1F5A3},
      {0x1F5A5}, {0x1F5FA},   {0x1F650}, {0x1F67F}, {0x1F6C6}, {0x1F6CB}, {0x1F6CD}, {0x1F6CF},
      {0x1F6D3}, {0x1F6D4},   {0x1F6D8}, {0x1F6DB}, {0x1F6E0}, {0x1F6EA}, {0x1F6ED}, {0x1F6F3},
      {0x1F6FD}, {0x1F7DF},   {0x1F7EC}, {0x1F7EF}, {0x1F7F1}, {0x1F90B}, {0x1F93B}, {0x1F93B},
      {0x1F946}, {0x1F946},   {0x1FA00}, {0x1FA6F}, {0x1FA7D}, {0x1FA7F}, {0x1FA8A}, {0x1FA8E},
      {0x1FAC7}, {0x1FACD},   {0x1FADD}, {0x1FADE}, {0x1FAEA}, {0x1FAEF}, {0x1FAF9}, {0x1FFFF},
      {0x2FFFE}, {0x2FFFF},   {0x3FFFE}, {0xE00FF}, {0xE01F0}, {0xEFFFF}, {0xFFFFE}, {0xFFFFF},
      {0x10FFFE}, {0x10FFFF},
      // clang-format on
  };
  constexpr CodePointSetRef setRef(0, table, std::size(table));

  // BMP
  const auto bmpRanges = setRef.getBMPRanges();
  ASSERT_EQ(0, bmpRanges.size());

  // NonBMP
  const auto nonBmpRanges = setRef.getNonBMPRanges();
  ASSERT_EQ(std::size(orgNonBMP), nonBmpRanges.size());
  for (size_t i = 0; i < std::size(orgNonBMP); i++) {
    SCOPED_TRACE("index=" + std::to_string(i));
    ASSERT_EQ(orgNonBMP[i].first, nonBmpRanges[i].firstNonBMP());
    ASSERT_EQ(orgNonBMP[i].second, nonBmpRanges[i].lastNonBMP());
  }

  for (unsigned int i = 0; i < std::size(orgNonBMP); i++) {
    SCOPED_TRACE("index=" + std::to_string(i));
    auto [first, last] = orgNonBMP[i];
    for (; first <= last; first++) {
      SCOPED_TRACE("codePoint=" + std::to_string(first));
      ASSERT_TRUE(setRef.contains(first));
    }
  }
}

TEST(UCPTest, category) {
  // ASSERT_TRUE(ucp::getCategorySet(ucp::Category::Me));
  // ASSERT_TRUE(ucp::getCategorySet(ucp::Category::Me).contains(0x20DF));
  ASSERT_STREQ("Me", toString(ucp::Category::Me));
  ASSERT_TRUE(ucp::getCategory(0x20DF).hasValue());
  ASSERT_EQ(ucp::Category::Me, ucp::getCategory(0x20DF).unwrap());
  ASSERT_TRUE(ucp::parseCategory("Me").hasValue());
  ASSERT_EQ(ucp::Category::Me, ucp::parseCategory("Me").unwrap());
}

static void addRange(CodePointSetBuilder &builder, const int first, const int last) {
  int newFirst = std::max(0, std::min(first, last));
  int newLast = std::min(UnicodeUtil::CODE_POINT_MAX, std::max(first, last));
  BMPCodePointRange ranges[] = {{static_cast<uint32_t>(newFirst)},
                                {static_cast<uint32_t>(newLast)}};
  builder.add({0, ranges, std::size(ranges)});
}

static CodePointSet set(std::vector<std::pair<int, int>> &&ranges) {
  CodePointSetBuilder builder;
  for (auto [first, last] : ranges) {
    addRange(builder, first, last);
  }
  return builder.build();
}

TEST(SetBuilderTest, add) {
  static constexpr BMPCodePointRange table[] = {
      // clang-format off
      {0, 100},
      {200, 300},
      {400, 500},
      {UINT16_MAX-100, UINT16_MAX},
      {UINT16_MAX+1},{UINT16_MAX+100},
      // clang-format on
  };
  CodePointSetRef ref(4, table, std::size(table));

  CodePointSetBuilder builder;
  builder.add(ref);
  ASSERT_EQ(4, builder.getCodePointRanges().size());
  ASSERT_EQ(std::make_pair(0, 100), builder.getCodePointRanges()[0]);
  ASSERT_EQ(std::make_pair(200, 300), builder.getCodePointRanges()[1]);
  ASSERT_EQ(std::make_pair(400, 500), builder.getCodePointRanges()[2]);
  ASSERT_EQ(std::make_pair(UINT16_MAX - 100, UINT16_MAX + 100), builder.getCodePointRanges()[3]);
  auto set = builder.build();
  ref = set.ref();
  ASSERT_EQ(4, ref.getBMPRanges().size());
  ASSERT_EQ(0, ref.getBMPRanges()[0].firstBMP());
  ASSERT_EQ(100, ref.getBMPRanges()[0].lastBMP());
  ASSERT_EQ(200, ref.getBMPRanges()[1].firstBMP());
  ASSERT_EQ(300, ref.getBMPRanges()[1].lastBMP());
  ASSERT_EQ(400, ref.getBMPRanges()[2].firstBMP());
  ASSERT_EQ(500, ref.getBMPRanges()[2].lastBMP());
  ASSERT_EQ(UINT16_MAX - 100, ref.getBMPRanges()[3].firstBMP());
  ASSERT_EQ(UINT16_MAX, ref.getBMPRanges()[3].lastBMP());

  ASSERT_EQ(1, ref.getNonBMPRanges().size());
  ASSERT_EQ(UINT16_MAX + 1, ref.getNonBMPRanges()[0].firstNonBMP());
  ASSERT_EQ(UINT16_MAX + 100, ref.getNonBMPRanges()[0].lastNonBMP());
}

TEST(SetBuilderTest, sub) {
  static constexpr BMPCodePointRange table[] = {
      // clang-format off
      {0, 100},
      {200, 300},
      {400, 500},
      {UINT16_MAX-100, UINT16_MAX},
      {UINT16_MAX+1},{UINT16_MAX+100},
      // clang-format on
  };
  constexpr CodePointSetRef ref(4, table, std::size(table));
  ASSERT_TRUE(ref.contains(100));
  ASSERT_FALSE(ref.contains(101));
  ASSERT_FALSE(ref.contains(199));
  ASSERT_TRUE(ref.contains(200));
  ASSERT_TRUE(ref.contains(250));
  ASSERT_TRUE(ref.contains(300));

  // sub
  CodePointSetBuilder builder;
  builder.add(ref);
  auto other =
      set({{0, 10}, {50, 60}, {150, 180}, {280, 300}, {UINT16_MAX + 100, UINT16_MAX + 200}});
  builder.sub(other.ref());
  ASSERT_EQ(5, builder.getCodePointRanges().size());
  ASSERT_EQ(11, builder.getCodePointRanges()[0].first);
  ASSERT_EQ(49, builder.getCodePointRanges()[0].second);
  ASSERT_EQ(61, builder.getCodePointRanges()[1].first);
  ASSERT_EQ(100, builder.getCodePointRanges()[1].second);
  ASSERT_EQ(200, builder.getCodePointRanges()[2].first);
  ASSERT_EQ(279, builder.getCodePointRanges()[2].second);
  ASSERT_EQ(400, builder.getCodePointRanges()[3].first);
  ASSERT_EQ(500, builder.getCodePointRanges()[3].second);
  ASSERT_EQ(UINT16_MAX - 100, builder.getCodePointRanges()[4].first);
  ASSERT_EQ(UINT16_MAX + 99, builder.getCodePointRanges()[4].second);

  //
  builder = CodePointSetBuilder();
  builder.add(ref);
  other = set({{0, 250}, {280, 290}, {300, 350}, {400, 450}, {480, 500}});
  builder.sub(other.ref());
  ASSERT_EQ(4, builder.getCodePointRanges().size());
  ASSERT_EQ(251, builder.getCodePointRanges()[0].first);
  ASSERT_EQ(279, builder.getCodePointRanges()[0].second);
  ASSERT_EQ(291, builder.getCodePointRanges()[1].first);
  ASSERT_EQ(299, builder.getCodePointRanges()[1].second);
  ASSERT_EQ(451, builder.getCodePointRanges()[2].first);
  ASSERT_EQ(479, builder.getCodePointRanges()[2].second);
  ASSERT_EQ(UINT16_MAX - 100, builder.getCodePointRanges()[3].first);
  ASSERT_EQ(UINT16_MAX + 100, builder.getCodePointRanges()[3].second);
}

TEST(SetBuilderTest, intersect) {
  static constexpr BMPCodePointRange table[] = {
      // clang-format off
    {0, 100},
    {200, 300},
    {400, 500},
    {UINT16_MAX-100, UINT16_MAX},
    {UINT16_MAX+1},{UINT16_MAX+100},
      // clang-format on
  };
  constexpr CodePointSetRef ref(4, table, std::size(table));

  //
  CodePointSetBuilder builder;
  builder.add(ref);
  auto other =
      set({{0, 10}, {50, 60}, {150, 180}, {280, 300}, {UINT16_MAX + 100, UINT16_MAX + 200}});
  builder.intersect(other.ref());
  ASSERT_EQ(4, builder.getCodePointRanges().size());
  ASSERT_EQ(0, builder.getCodePointRanges()[0].first);
  ASSERT_EQ(10, builder.getCodePointRanges()[0].second);
  ASSERT_EQ(50, builder.getCodePointRanges()[1].first);
  ASSERT_EQ(60, builder.getCodePointRanges()[1].second);
  ASSERT_EQ(280, builder.getCodePointRanges()[2].first);
  ASSERT_EQ(300, builder.getCodePointRanges()[2].second);
  ASSERT_EQ(UINT16_MAX + 100, builder.getCodePointRanges()[3].first);
  ASSERT_EQ(UINT16_MAX + 100, builder.getCodePointRanges()[3].second);

  //
  builder = CodePointSetBuilder();
  builder.add(ref);
  other = set({{0, 250}, {280, 290}, {300, 350}, {400, 450}, {480, 500}});
  builder.intersect(other.ref());
  ASSERT_EQ(6, builder.getCodePointRanges().size());
  ASSERT_EQ(0, builder.getCodePointRanges()[0].first);
  ASSERT_EQ(100, builder.getCodePointRanges()[0].second);
  ASSERT_EQ(200, builder.getCodePointRanges()[1].first);
  ASSERT_EQ(250, builder.getCodePointRanges()[1].second);
  ASSERT_EQ(280, builder.getCodePointRanges()[2].first);
  ASSERT_EQ(290, builder.getCodePointRanges()[2].second);
  ASSERT_EQ(300, builder.getCodePointRanges()[3].first);
  ASSERT_EQ(300, builder.getCodePointRanges()[3].second);
  ASSERT_EQ(400, builder.getCodePointRanges()[4].first);
  ASSERT_EQ(450, builder.getCodePointRanges()[4].second);
  ASSERT_EQ(480, builder.getCodePointRanges()[5].first);
  ASSERT_EQ(500, builder.getCodePointRanges()[5].second);

  //
  builder = CodePointSetBuilder();
  builder.add(ref);
  other = set({{UINT16_MAX + 200, UINT16_MAX + 1000}});
  builder.intersect(other.ref());
  ASSERT_EQ(0, builder.getCodePointRanges().size());
}

TEST(SetBuilderTest, complement1) {
  static constexpr BMPCodePointRange table[] = {
      // clang-format off
    {0, 100},
    {200, 300},
    {400, 500},
    {UINT16_MAX-100, UINT16_MAX},
    {UINT16_MAX+1},{UINT16_MAX+100},
      // clang-format on
  };
  constexpr CodePointSetRef ref(4, table, std::size(table));

  //
  CodePointSetBuilder builder;
  builder.add(ref);
  builder.complement();
  ASSERT_EQ(4, builder.getCodePointRanges().size());
  ASSERT_EQ(101, builder.getCodePointRanges()[0].first);
  ASSERT_EQ(199, builder.getCodePointRanges()[0].second);
  ASSERT_EQ(301, builder.getCodePointRanges()[1].first);
  ASSERT_EQ(399, builder.getCodePointRanges()[1].second);
  ASSERT_EQ(501, builder.getCodePointRanges()[2].first);
  ASSERT_EQ(UINT16_MAX - 101, builder.getCodePointRanges()[2].second);
  ASSERT_EQ(UINT16_MAX + 101, builder.getCodePointRanges()[3].first);
  ASSERT_EQ(UnicodeUtil::CODE_POINT_MAX, builder.getCodePointRanges()[3].second);

  //
  builder.complement();
  ASSERT_EQ(4, builder.getCodePointRanges().size());
  ASSERT_EQ(0, builder.getCodePointRanges()[0].first);
  ASSERT_EQ(100, builder.getCodePointRanges()[0].second);
  ASSERT_EQ(200, builder.getCodePointRanges()[1].first);
  ASSERT_EQ(300, builder.getCodePointRanges()[1].second);
  ASSERT_EQ(400, builder.getCodePointRanges()[2].first);
  ASSERT_EQ(500, builder.getCodePointRanges()[2].second);
  ASSERT_EQ(UINT16_MAX - 100, builder.getCodePointRanges()[3].first);
  ASSERT_EQ(UINT16_MAX + 100, builder.getCodePointRanges()[3].second);

  //
  auto codePointSet = builder.build();
  builder.complement();
  builder.add(codePointSet.ref());
  ASSERT_EQ(1, builder.getCodePointRanges().size());
  ASSERT_EQ(0, builder.getCodePointRanges()[0].first);
  ASSERT_EQ(UnicodeUtil::CODE_POINT_MAX, builder.getCodePointRanges()[0].second);
}

TEST(SetBuilderTest, complement2) {
  CodePointSetBuilder builder;
  ASSERT_EQ(0, builder.getCodePointRanges().size());
  builder.complement();
  ASSERT_EQ(1, builder.getCodePointRanges().size());
  ASSERT_EQ(0, builder.getCodePointRanges()[0].first);
  ASSERT_EQ(UnicodeUtil::CODE_POINT_MAX, builder.getCodePointRanges()[0].second);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}