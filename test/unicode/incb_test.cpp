#include "../test_common.h"

#include <misc/codepoint_set.hpp>
#include <unicode/grapheme.h>
#include <unicode/property.h>

using namespace arsh;

static void iterate(const CodePointSet &set, const std::function<void(int)> &func) {
  assert(func);
  const auto ref = set.ref();
  for (auto &range : ref.getBMPRanges()) {
    for (int c = range.firstBMP(); c <= range.lastBMP(); c++) {
      func(c);
    }
  }
  for (auto &range : ref.getPackedNonBMPRanges()) {
    for (int c = range.firstNonBMP(); c <= range.lastNonBMP(); c++) {
      func(c);
    }
  }
  for (auto &range : ref.getNonBMPRanges()) {
    for (int c = range.firstNonBMP(); c <= range.lastNonBMP(); c++) {
      func(c);
    }
  }
}

TEST(EmojiTest, base) {
  auto set = ucp::getPropertySet(ucp::Property::lone(ucp::Lone::Extended_Pictographic));
  iterate(set, [&](const int codePoint) {
    auto p = GraphemeBoundary::getBreakProperty(codePoint);
    ASSERT_EQ(GraphemeBoundary::BreakProperty::Extended_Pictographic, p);
  });
}

TEST(InCBTest, consonat) {
  auto set = ucp::getPropertySet(ucp::Property::lone(ucp::Lone::InCB_Consonant));
  iterate(set, [&](const int codePoint) {
    auto p = GraphemeBoundary::getBreakProperty(codePoint);
    ASSERT_EQ(GraphemeBoundary::BreakProperty::InCB_Consonant, p);
  });
}

static const char *toString(GraphemeBoundary::BreakProperty p) {
  switch (p) {
  case GraphemeBoundary::BreakProperty::SOT:
    return "SOT";
  case GraphemeBoundary::BreakProperty::Any:
    return "Any";
  case GraphemeBoundary::BreakProperty::CR:
    return "CR";
  case GraphemeBoundary::BreakProperty::LF:
    return "LF";
  case GraphemeBoundary::BreakProperty::Control:
    return "Control";
  case GraphemeBoundary::BreakProperty::Extend:
    return "Extend";
  case GraphemeBoundary::BreakProperty::ZWJ:
    return "ZWJ";
  case GraphemeBoundary::BreakProperty::Regional_Indicator:
    return "Regional_Indicator";
  case GraphemeBoundary::BreakProperty::Prepend:
    return "Prepend";
  case GraphemeBoundary::BreakProperty::SpacingMark:
    return "SpacingMark";
  case GraphemeBoundary::BreakProperty::L:
    return "L";
  case GraphemeBoundary::BreakProperty::V:
    return "V";
  case GraphemeBoundary::BreakProperty::T:
    return "T";
  case GraphemeBoundary::BreakProperty::LV:
    return "LV";
  case GraphemeBoundary::BreakProperty::LVT:
    return "LVT";
  case GraphemeBoundary::BreakProperty::Extended_Pictographic:
    return "Extended_Pictographic";
  case GraphemeBoundary::BreakProperty::Extended_Pictographic_with_ZWJ:
    return "Extended_Pictographic_with_ZWJ";
  case GraphemeBoundary::BreakProperty::InCB_Consonant:
    return "InCB_Consonant";
  case GraphemeBoundary::BreakProperty::InCB_Extend:
    return "InCB_Extend";
  case GraphemeBoundary::BreakProperty::InCB_Linker:
    return "InCB_Linker";
  case GraphemeBoundary::BreakProperty::InCB_Consonant_with_Linker:
    break;
  }
  return "";
}

static bool contains(const std::vector<GraphemeBoundary::BreakProperty> &targets,
                     GraphemeBoundary::BreakProperty property) {
  return std::any_of(targets.begin(), targets.end(),
                     [property](GraphemeBoundary::BreakProperty p) { return p == property; });
}

TEST(InCBTest, linkerOrExtend) {
  {
    auto set = ucp::getPropertySet(ucp::Property::lone(ucp::Lone::InCB_Linker));
    const std::vector targets = {
        GraphemeBoundary::BreakProperty::Extend,
        GraphemeBoundary::BreakProperty::Any,
    };
    iterate(set, [&](const int codePoint) {
      auto p = GraphemeBoundary::getBreakProperty(codePoint);
      fprintf(stdout, "[note] 0x%04x InCB=Linker, GBP=%s\n", codePoint, toString(p));
      ASSERT_PRED2(contains, targets, p);
      ASSERT_FALSE(ucp::hasPrimeLoneProperty(codePoint, ucp::Lone::InCB_Extend));
    });
  }

  {
    auto set = ucp::getPropertySet(ucp::Property::lone(ucp::Lone::InCB_Extend));
    const std::vector targets = {
        GraphemeBoundary::BreakProperty::Extend,
        GraphemeBoundary::BreakProperty::ZWJ,
    };
    iterate(set, [&](const int codePoint) {
      auto p = GraphemeBoundary::getBreakProperty(codePoint);
      fprintf(stdout, "[note] 0x%04x InCB=Extend, GBP=%s\n", codePoint, toString(p));
      ASSERT_PRED2(contains, targets, p);
      ASSERT_FALSE(ucp::hasPrimeLoneProperty(codePoint, ucp::Lone::InCB_Linker));
    });
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}