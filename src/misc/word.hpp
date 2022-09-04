/*
 * Copyright (C) 2022 Nagisa Sekiguchi
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

#ifndef MISC_LIB_WORD_HPP
#define MISC_LIB_WORD_HPP

#include <tuple>

#include "string_ref.hpp"
#include "unicode.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

namespace detail {

template <bool Bool>
class WordBoundary {
public:
  static_assert(Bool, "not allowed instantiation");

  // for word boundary
  enum class BreakProperty : unsigned char {
    SOT, // for WB1

    Any,
    CR,
    LF,
    Newline,
    Extend,
    ZWJ,
    Regional_Indicator,
    Format,
    Katakana,
    Hebrew_Letter,
    ALetter,
    Single_Quote,
    Double_Quote,
    MidNumLet,
    MidLetter,
    MidNum,
    Numeric,
    ExtendNumLet,
    WSegSpace,

    Extended_Pictographic,

    WSegSpace_EFZ,        // for WB3d, WB4
    AHLetter_Mid_NumLetQ, // for WB6, WB7. AHLetter * (MidLetter|MidNumLetQ)
    Hebrew_Letter_DQ,     // for WB7c. Hebrew_Letter * Double_Quote
    Numeric_Mid_NumLetQ,  // for WB11. Numeric * (MidNum | MidNumLetQ)
  };

  static BreakProperty getBreakProperty(int codePoint);

private:
  /**
   * may be indicate previous code point property
   */
  BreakProperty state{BreakProperty::SOT};

  /**
   * for WB3c
   */
  bool zwj{false};

public:
  WordBoundary() = default;

  BreakProperty getState() const { return this->state; }

  /**
   * scan word boundary
   * @param begin
   * @param end
   * @return
   * if word boundary is between prev codepoint and cur codepoint, return true
   */
  bool scanBoundary(const BreakProperty *begin, const BreakProperty *end);

private:
  static const BreakProperty *skipEFZ(const BreakProperty *begin, const BreakProperty *end) {
    for (; begin != end; ++begin) {
      switch (*begin) {
      case BreakProperty::Extend:
      case BreakProperty::Format:
      case BreakProperty::ZWJ:
        continue;
      default:
        break;
      }
      break;
    }
    return begin;
  }
};

template <bool Bool>
typename WordBoundary<Bool>::BreakProperty WordBoundary<Bool>::getBreakProperty(int codePoint) {
  using PropertyInterval = std::tuple<int, int, BreakProperty>;

#define UNICODE_PROPERTY_RANGE PropertyInterval
#define PROPERTY(E) BreakProperty::E
#include "word_break_property.h"
#undef PROPERTY
#undef UNICODE_PROPERTY_RANGE

  struct Comp {
    bool operator()(const PropertyInterval &l, int r) const { return std::get<1>(l) < r; }

    bool operator()(int l, const PropertyInterval &r) const { return l < std::get<0>(r); }
  };

  auto iter = std::lower_bound(std::begin(word_break_property_table),
                               std::end(word_break_property_table), codePoint, Comp());
  if (iter != std::end(word_break_property_table)) {
    auto &interval = *iter;
    if (codePoint >= std::get<0>(interval) && codePoint <= std::get<1>(interval)) {
      return std::get<2>(interval);
    }
  }
  return BreakProperty::Any;
}

// see, https://www.unicode.org/reports/tr29/tr29-39.html#Word_Boundaries
template <bool Bool>
bool WordBoundary<Bool>::scanBoundary(const BreakProperty *begin, const BreakProperty *end) {
  if (begin == end) {
    return true;
  }
  const auto after = *(begin++);
  const auto before = this->state;
  this->state = after;
  const bool prevZWJ = this->zwj;
  this->zwj = after == BreakProperty::ZWJ;

  if (prevZWJ && after == BreakProperty::Extended_Pictographic) {
    return false; // WB3c
  }

  if (before == BreakProperty::SOT) {
    return false;
  }

  switch (before) {
  case BreakProperty::CR:
    if (after == BreakProperty::LF) {
      return false; // WB3
    }
    return true; // WB3a
  case BreakProperty::Newline:
  case BreakProperty::LF:
    return true; // WB3a
  default:
    switch (after) {
    case BreakProperty::Newline:
    case BreakProperty::CR:
    case BreakProperty::LF:
      return true; // WB3b
    case BreakProperty::Extend:
    case BreakProperty::Format:
    case BreakProperty::ZWJ:
      if (before == BreakProperty::WSegSpace) {
        this->state = BreakProperty::WSegSpace_EFZ;
        return false; // WB3d, WB4
      } else {
        this->state = before; // consume (Extend|Format|ZWJ)
        return false;         // WB4
      }
    default:
      break;
    }
    break;
  }

  switch (before) {
  case BreakProperty::WSegSpace:
    if (after == BreakProperty::WSegSpace) {
      return false; // WB3d
    }
    break;
  case BreakProperty::ALetter:
  case BreakProperty::Hebrew_Letter: // AHLetter = (ALetter | Hebrew_Letter)
    switch (after) {
    case BreakProperty::ALetter:
    case BreakProperty::Hebrew_Letter: // AHLetter = (ALetter | Hebrew_Letter)
      return false;                    // WB5
    case BreakProperty::MidLetter:
    case BreakProperty::MidNumLet:
    case BreakProperty::Single_Quote:
      begin = skipEFZ(begin, end);
      if (begin != end &&
          (*begin == BreakProperty::ALetter || *begin == BreakProperty::Hebrew_Letter)) {
        this->state = BreakProperty::AHLetter_Mid_NumLetQ;
        return false; // WB6
      }
      break;
    default:
      break;
    }
    break;
  case BreakProperty::AHLetter_Mid_NumLetQ:
    switch (after) {
    case BreakProperty::ALetter:
    case BreakProperty::Hebrew_Letter: // AHLetter = (ALetter | Hebrew_Letter)
      return false;                    // WB7
    default:
      break;
    }
    break;
  default:
    break;
  }
  if (before == BreakProperty::Hebrew_Letter) {
    if (after == BreakProperty::Single_Quote) {
      return false; // WB7a
    } else if (after == BreakProperty::Double_Quote) {
      begin = skipEFZ(begin, end);
      if (begin != end && *begin == BreakProperty::Hebrew_Letter) {
        this->state = BreakProperty::Hebrew_Letter_DQ;
        return false; // WB7b
      }
    }
  } else if (before == BreakProperty::Hebrew_Letter_DQ) {
    if (after == BreakProperty::Hebrew_Letter) {
      return false; // WB7c
    }
  }

  switch (before) {
  case BreakProperty::Numeric:
  case BreakProperty::ALetter:
  case BreakProperty::Hebrew_Letter: // AHLetter = (ALetter | Hebrew_Letter)
    if (after == BreakProperty::Numeric) {
      return false; // WB8, WB9
    }
    break;
  default:
    break;
  }

  switch (before) {
  case BreakProperty::Numeric:
    switch (after) {
    case BreakProperty::ALetter:
    case BreakProperty::Hebrew_Letter: // AHLetter = (ALetter | Hebrew_Letter)
      return false;                    // WB10
    case BreakProperty::MidNum:
    case BreakProperty::MidNumLet:
    case BreakProperty::Single_Quote:
      begin = skipEFZ(begin, end);
      if (begin != end && *begin == BreakProperty::Numeric) {
        this->state = BreakProperty::Numeric_Mid_NumLetQ;
        return false; // WB12
      }
      break;
    default:
      break;
    }
    break;
  case BreakProperty::Numeric_Mid_NumLetQ:
    if (after == BreakProperty::Numeric) {
      return false; // WB11
    }
    break;
  case BreakProperty::Katakana:
    if (after == BreakProperty::Katakana) {
      return false; // WB13
    }
    break;
  default:
    break;
  }

  switch (before) {
  case BreakProperty::ALetter:
  case BreakProperty::Hebrew_Letter: // AHLetter = (ALetter | Hebrew_Letter)
  case BreakProperty::Numeric:
  case BreakProperty::Katakana:
    if (after == BreakProperty::ExtendNumLet) {
      return false; // WB13a
    }
    break;
  case BreakProperty::ExtendNumLet:
    switch (after) {
    case BreakProperty::ExtendNumLet:
      return false; // WB13a
    case BreakProperty::ALetter:
    case BreakProperty::Hebrew_Letter: // AHLetter = (ALetter | Hebrew_Letter)
    case BreakProperty::Numeric:
    case BreakProperty::Katakana:
      return false; // WB13b
    default:
      break;
    }
    break;
  case BreakProperty::Regional_Indicator:
    if (after == BreakProperty::Regional_Indicator) {
      this->state = BreakProperty::Any;
      return false; // WB15, WB16
    }
    break;
  default:
    break;
  }
  return true; // WB999
}

} // namespace detail

using WordBoundary = detail::WordBoundary<true>;

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_WORD_HPP
