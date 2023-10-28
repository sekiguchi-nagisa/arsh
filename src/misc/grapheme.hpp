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
#include "enum_util.hpp"
#include "string_ref.hpp"
#include "unicode.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

namespace detail {

template <bool Bool>
class GraphemeBoundary {
public:
  static_assert(Bool, "not allowed instantiation");

  // for grapheme cluster boundary. only support extended grapheme cluster
  enum class BreakProperty : unsigned char {
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
};

template <bool Bool>
typename GraphemeBoundary<Bool>::BreakProperty
GraphemeBoundary<Bool>::getBreakProperty(int codePoint) {
  if (codePoint < 0) {
    return BreakProperty::Control; // invalid code points are always grapheme boundary
  }

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

} // namespace detail

using GraphemeBoundary = detail::GraphemeBoundary<true>;

class GraphemeCluster {
public:
  static constexpr size_t MAX_GRAPHEME_CODE_POINTS = 32;

private:
  StringRef ref; // grapheme cluster
  bool invalid{false};
  unsigned short codePointCount{0}; // count of containing code points
  CodePointWithMeta data[MAX_GRAPHEME_CODE_POINTS];

public:
  bool add(int codePoint, GraphemeBoundary::BreakProperty p) {
    if (this->codePointCount == MAX_GRAPHEME_CODE_POINTS) {
      return false;
    }
    this->data[this->codePointCount] = CodePointWithMeta(codePoint, toUnderlying(p));
    this->codePointCount++;
    if (codePoint == -1) {
      invalid = true;
    }
    return true;
  }

  void clear() {
    this->codePointCount = 0;
    this->invalid = false;
    this->ref = "";
  }

  void setRef(StringRef v) { this->ref = v; }

  StringRef getRef() const { return this->ref; }

  bool hasInvalid() const { return this->invalid; }

  unsigned int getCodePointCount() const { return this->codePointCount; }

  int getCodePointAt(unsigned int index) const { return this->data[index].codePoint(); }

  GraphemeBoundary::BreakProperty getBreakPropertyAt(unsigned int index) const {
    return static_cast<GraphemeBoundary::BreakProperty>(this->data[index].getMeta());
  }
};

template <typename Stream>
class GraphemeScanner {
public:
  using BreakProperty = GraphemeBoundary::BreakProperty;

private:
  Stream stream;
  int codePoint;
  BreakProperty state;

protected:
  GraphemeScanner(Stream &&stream, int codePoint, BreakProperty property)
      : stream(std::move(stream)), codePoint(codePoint), state(property) {}

public:
  explicit GraphemeScanner(Stream &&stream)
      : GraphemeScanner(std::move(stream), -1, BreakProperty::SOT) {}

  const auto &getStream() const { return this->stream; }

  int getCodePoint() const { return this->codePoint; }

  BreakProperty getProperty() const { return this->state; }

  /**
   * scan grapheme cluster boundary
   * @param breakProperty
   * @return
   * if grapheme cluster boundary is between prev codePoint and codePoint, return true
   */
  bool scanBoundary(BreakProperty breakProperty);

  BreakProperty nextProperty() {
    this->codePoint = this->stream.nextCodePoint();
    return GraphemeBoundary::getBreakProperty(this->codePoint);
  }
};

// see. https://unicode.org/reports/tr29/#Grapheme_Cluster_Boundary_Rules
template <typename Stream>
bool GraphemeScanner<Stream>::scanBoundary(BreakProperty breakProperty) {
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

class Utf8GraphemeScanner : public GraphemeScanner<Utf8Stream> {
private:
  const char *charBegin;

public:
  explicit Utf8GraphemeScanner(StringRef ref)
      : Utf8GraphemeScanner(Utf8Stream(ref.begin(), ref.end())) {}

  Utf8GraphemeScanner(Utf8Stream &&stream, const char *charBegin = nullptr, int codePoint = -1,
                      BreakProperty p = BreakProperty::SOT)
      : GraphemeScanner(std::move(stream), codePoint, p), charBegin(charBegin) {
    if (!this->charBegin) {
      this->charBegin = this->getIter();
    }
  }

  const char *getCharBegin() const { return this->charBegin; }

  bool hasNext() const { return this->charBegin != this->getStream().end; }

  /**
   * get grapheme cluster
   * @param result
   * set scanned grapheme cluster info to result
   * @return
   * if reach eof, return false
   */
  void next(GraphemeCluster &result) {
    result.clear();
    if (this->getProperty() != BreakProperty::SOT && this->hasNext()) {
      result.add(this->getCodePoint(), this->getProperty());
    }
    auto *begin = this->charBegin;
    const char *end;
    while (true) {
      end = this->getIter();
      auto p = this->nextProperty();
      if (this->scanBoundary(p)) {
        break;
      }
      result.add(this->getCodePoint(), p);
    }
    this->charBegin = end;
    StringRef ref(begin, static_cast<size_t>(end - begin));
    result.setRef(ref);
  }

private:
  const char *getIter() const { return this->getStream().iter; }
};

template <typename Consumer>
static constexpr bool grapheme_consumer_requirement_v =
    std::is_same_v<void, std::invoke_result_t<Consumer, const GraphemeCluster &>> ||
    std::is_same_v<bool, std::invoke_result_t<Consumer, const GraphemeCluster &>>;

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
  Utf8GraphemeScanner scanner(Utf8Stream(ref.begin(), ref.end()));
  GraphemeCluster ret;
  size_t count = 0;
  for (; count < limit && scanner.hasNext(); count++) {
    scanner.next(ret);
    constexpr auto v = std::is_same_v<bool, std::invoke_result_t<Func, const GraphemeCluster &>>;
    if constexpr (v) {
      if (!consumer(ret)) {
        count++;
        break;
      }
    } else {
      consumer(ret);
    }
  }
  return count;
}

template <typename Func, enable_when<grapheme_consumer_requirement_v<Func>> = nullptr>
size_t iterateGrapheme(StringRef ref, Func consumer) {
  return iterateGraphemeUntil(ref, static_cast<size_t>(-1), std::move(consumer));
}

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_GRAPHEME_HPP
