/*
 * Copyright (C) 2025 Nagisa Sekiguchi
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "grapheme.h"

namespace arsh {

// ##############################
// ##     GraphemeBoundary     ##
// ##############################

GraphemeBoundary::BreakProperty GraphemeBoundary::getBreakProperty(const int codePoint) {
  if (codePoint < 0) {
    return BreakProperty::Control; // invalid code points are always grapheme boundary
  }

  using PropertyInterval = CodePointPropertyInterval<BreakProperty>;

#define UNICODE_PROPERTY_RANGE PropertyInterval
#define PROPERTY(E) BreakProperty::E
#include "grapheme_break_property.in"

#undef PROPERTY
#undef UNICODE_PROPERTY_RANGE

  auto iter = std::lower_bound(std::begin(grapheme_break_property_table),
                               std::end(grapheme_break_property_table), codePoint,
                               PropertyInterval::Comp());
  if (iter != std::end(grapheme_break_property_table)) {
    if (auto &interval = *iter; interval.contains(codePoint)) {
      return interval.property();
    }
  }
  return BreakProperty::Any;
}

GraphemeBoundary::BreakProperty GraphemeBoundary::getInCBExtendOrLinker(const int codePoint) {
  using PropertyInterval = CodePointPropertyInterval<BreakProperty>;

#define UNICODE_PROPERTY_RANGE PropertyInterval
#define PROPERTY(E) BreakProperty::E
#include "incb_property.in"

#undef PROPERTY
#undef UNICODE_PROPERTY_RANGE

  auto iter = std::lower_bound(std::begin(incb_property_table), std::end(incb_property_table),
                               codePoint, PropertyInterval::Comp());
  if (iter != std::end(incb_property_table)) {
    if (auto &interval = *iter; interval.contains(codePoint)) {
      return interval.property();
    }
  }
  return BreakProperty::Any; // if no inCB
}

// see. https://unicode.org/reports/tr29/#Grapheme_Cluster_Boundary_Rules
bool GraphemeBoundary::checkBoundary(const int codePoint) {
  const auto after = getBreakProperty(codePoint);
  const auto before = this->state;
  this->state = after;
  this->emojiSeq = false;

  switch (before) {
  case BreakProperty::SOT:
    return false;
  case BreakProperty::CR:
    if (after == BreakProperty::LF) {
      return false; // GB3
    }
    return true; // GB4
  case BreakProperty::LF:
  case BreakProperty::Control:
    return true; // GB4
  default:
    switch (after) {
    case BreakProperty::Control:
    case BreakProperty::CR:
    case BreakProperty::LF:
      return true; // GB5
    default:
      break;
    }
    break;
  }

  switch (before) {
  case BreakProperty::L:
    switch (after) {
    case BreakProperty::L:
    case BreakProperty::V:
    case BreakProperty::LV:
    case BreakProperty::LVT:
      return false; // GB6
    default:
      break;
    }
    break;
  case BreakProperty::LV:
  case BreakProperty::V:
    if (after == BreakProperty::V || after == BreakProperty::T) {
      return false; // GB7
    }
    break;
  case BreakProperty::LVT:
  case BreakProperty::T:
    if (after == BreakProperty::T) {
      return false; // GB8
    }
    break;
  case BreakProperty::InCB_Consonant:
    if (const auto inCB = getInCBExtendOrLinker(codePoint); inCB == BreakProperty::InCB_Extend) {
      this->state = BreakProperty::InCB_Consonant;
      return false; // GB9c
    } else if (inCB == BreakProperty::InCB_Linker) {
      this->state = BreakProperty::InCB_Consonant_with_Linker;
      return false; // GB9c
    }
    break;
  case BreakProperty::InCB_Consonant_with_Linker:
    if (after == BreakProperty::InCB_Consonant) {
      this->state = BreakProperty::InCB_Consonant;
      return false; // GB9c
    }
    if (const auto inCB = getInCBExtendOrLinker(codePoint);
        inCB == BreakProperty::InCB_Extend || inCB == BreakProperty::InCB_Linker) {
      this->state = BreakProperty::InCB_Consonant_with_Linker;
      return false; // GB9c
    }
    break;
  default:
    break;
  }

  switch (after) {
  case BreakProperty::Extend:
  case BreakProperty::ZWJ:
    if (before != BreakProperty::Extended_Pictographic) {
      return false; // GB9
    }
    break;
  case BreakProperty::SpacingMark:
    return false; // GB9a
  default:
    break;
  }

  switch (before) {
  case BreakProperty::Prepend:
    return false; // GB9b
  case BreakProperty::Regional_Indicator:
    if (after == BreakProperty::Regional_Indicator) {
      this->state = BreakProperty::Any;
      return false; // GB12, GB13
    }
    break;
  case BreakProperty::Extended_Pictographic:
    if (after == BreakProperty::Extend) {
      this->state = BreakProperty::Extended_Pictographic; // consume Extend
      this->emojiSeq = true;
      return false; // GB11
    } else if (after == BreakProperty::ZWJ) {
      this->state = BreakProperty::Extended_Pictographic_with_ZWJ;
      this->emojiSeq = true;
      return false; // GB11
    }
    break;
  case BreakProperty::Extended_Pictographic_with_ZWJ:
    if (after == BreakProperty::Extended_Pictographic) {
      return false; // GB11
    }
    break;
  default:
    break;
  }
  return true; // GB999
}

} // namespace arsh