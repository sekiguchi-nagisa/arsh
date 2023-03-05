/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#ifndef MISC_LIB_GRAPHEME_HPP
#define MISC_LIB_GRAPHEME_HPP

#include <tuple>

#include "detect.hpp"
#include "string_ref.hpp"
#include "unicode.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

namespace detail {

template <bool Bool>
class GraphemeBoundary {
public:
  static_assert(Bool, "not allowed instantiation");

  // for grapheme cluster boundary. only support extended grapheme cluster
  enum class BreakProperty {
    SOT, // for GB1

    Any,
    CR,
    LF,
    Control,
    Extend,
    ZWJ,
    Regional_Indicator,
    Prepend,
    SpacingMark,
    L,
    V,
    T,
    LV,
    LVT,

    Extended_Pictographic,

    Extended_Pictographic_with_ZWJ, // indicates \p{Extended_Pictographic} Extend* ZWJ
  };

  static BreakProperty getBreakProperty(int codePoint);

private:
  /**
   * may be indicate previous code point property
   */
  BreakProperty state{BreakProperty::SOT};

public:
  GraphemeBoundary() = default;

  explicit GraphemeBoundary(BreakProperty init) : state(init) {}

  BreakProperty getState() const { return this->state; }

  /**
   * scan grapheme cluster boundary
   * @param breakProperty
   * @return
   * if grapheme cluster boundary is between prev codePoint and codePoint, return true
   */
  bool scanBoundary(BreakProperty breakProperty);
};

template <bool Bool>
typename GraphemeBoundary<Bool>::BreakProperty
GraphemeBoundary<Bool>::getBreakProperty(int codePoint) {
  using PropertyInterval = std::tuple<int, int, BreakProperty>;

#define UNICODE_PROPERTY_RANGE PropertyInterval
#define PROPERTY(E) BreakProperty::E
#include "grapheme_break_property.h"
#undef PROPERTY
#undef UNICODE_PROPERTY_RANGE

  struct Comp {
    bool operator()(const PropertyInterval &l, int r) const { return std::get<1>(l) < r; }

    bool operator()(int l, const PropertyInterval &r) const { return l < std::get<0>(r); }
  };

  auto iter = std::lower_bound(std::begin(grapheme_break_property_table),
                               std::end(grapheme_break_property_table), codePoint, Comp());
  if (iter != std::end(grapheme_break_property_table)) {
    auto &interval = *iter;
    if (codePoint >= std::get<0>(interval) && codePoint <= std::get<1>(interval)) {
      return std::get<2>(interval);
    }
  }
  return BreakProperty::Any;
}

// see. https://unicode.org/reports/tr29/#Grapheme_Cluster_Boundary_Rules
template <bool Bool>
bool GraphemeBoundary<Bool>::scanBoundary(BreakProperty breakProperty) {
  auto after = breakProperty;
  auto before = this->state;
  this->state = after;

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
      return false;                                       // GB11
    } else if (after == BreakProperty::ZWJ) {
      this->state = BreakProperty::Extended_Pictographic_with_ZWJ;
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

template <bool Bool>
class GraphemeScanner {
private:
  static_assert(Bool, "not allowed instantiation");

  StringRef ref;
  size_t prevPos;
  size_t curPos;
  GraphemeBoundary<Bool> boundary;

public:
  explicit GraphemeScanner(StringRef ref, size_t prevPos = 0, size_t curPos = 0,
                           GraphemeBoundary<Bool> boundary = {})
      : ref(ref), prevPos(prevPos), curPos(curPos), boundary(boundary) {}

  StringRef getRef() const { return this->ref; }

  size_t getPrevPos() const { return this->prevPos; }

  size_t getCurPos() const { return this->curPos; }

  GraphemeBoundary<Bool> getBoundary() const { return this->boundary; }

  bool hasNext() const { return this->prevPos <= this->curPos && this->prevPos < this->ref.size(); }

  static constexpr size_t MAX_GRAPHEME_CODE_POINTS = 32;

  struct Result {
    StringRef ref; // grapheme cluster
    bool hasInvalid{false};
    unsigned short codePointCount{0}; // count of containing code points
    int codePoints[MAX_GRAPHEME_CODE_POINTS];
    typename GraphemeBoundary<Bool>::BreakProperty breakProperties[MAX_GRAPHEME_CODE_POINTS];
  };

  /**
   * get grapheme cluster
   * @param result
   * set scanned grapheme cluster info to result
   * @return
   * if reach eof, return false
   */
  bool next(Result &result);
};

template <bool Bool>
bool GraphemeScanner<Bool>::next(Result &result) {
  const size_t startPos = this->prevPos;
  result.hasInvalid = false;
  result.codePointCount = 0;
  if (this->prevPos != this->curPos) {
    result.codePointCount = 1;
    result.codePoints[0] =
        UnicodeUtil::utf8ToCodePoint(this->ref.begin() + this->prevPos, this->ref.end());
    result.hasInvalid = result.hasInvalid || result.codePoints[0] == -1;
  }

  while (this->curPos < this->ref.size()) {
    const size_t pos = this->curPos;
    int codePoint = 0;
    unsigned int consumedSize =
        UnicodeUtil::utf8ToCodePoint(this->ref.begin() + this->curPos, this->ref.end(), codePoint);
    if (consumedSize < 1) {
      consumedSize = 1;
    }
    auto breakProperty = GraphemeBoundary<Bool>::getBreakProperty(codePoint);
    assert(result.codePointCount < std::size(result.codePoints));
    this->curPos += consumedSize;
    if (this->boundary.scanBoundary(breakProperty)) {
      size_t byteSize = pos - this->prevPos;
      result.ref = this->ref.substr(startPos, byteSize);
      this->prevPos = pos;
      return true;
    }
    unsigned int index = result.codePointCount++;
    result.codePoints[index] = codePoint;
    result.breakProperties[index] = breakProperty;
    result.hasInvalid = result.hasInvalid || codePoint == -1;
  }
  if (this->curPos == this->ref.size()) {
    size_t byteSize = this->curPos - this->prevPos;
    result.ref = this->ref.substr(startPos, byteSize);
    this->prevPos = this->curPos;
  }
  return result.codePointCount > 0;
}

} // namespace detail

using GraphemeBoundary = detail::GraphemeBoundary<true>;

using GraphemeScanner = detail::GraphemeScanner<true>;

template <typename Consumer>
static constexpr bool grapheme_consumer_requirement_v =
    std::is_same_v<void, std::invoke_result_t<Consumer, const GraphemeScanner::Result &>>;

/**
 * iterate grapheme cluster
 * @tparam Func
 * @param ref
 * @param limit
 * if number of grapheme clusters reach limit, break iteration
 * @param consumer
 * callback for scanned grapheme
 * @return
 * total number of scanned grapheme clusters
 */
template <typename Func, enable_when<grapheme_consumer_requirement_v<Func>> = nullptr>
size_t iterateGraphemeUntil(StringRef ref, size_t limit, Func consumer) {
  GraphemeScanner scanner(ref);
  GraphemeScanner::Result ret;
  size_t count = 0;
  for (; count < limit && scanner.next(ret); count++) {
    consumer(ret);
  }
  return count;
}

template <typename Func, enable_when<grapheme_consumer_requirement_v<Func>> = nullptr>
size_t iterateGrapheme(StringRef ref, Func consumer) {
  return iterateGraphemeUntil(ref, static_cast<size_t>(-1), std::move(consumer));
}

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_GRAPHEME_HPP
