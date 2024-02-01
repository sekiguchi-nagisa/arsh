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

    // for Indic_Conjunct_Break
    InCB_Consonant,
    InCB_Extend,
    InCB_Linker,

    // indicate [InCB_Extend InCB_Linker]* InCB_Linker [InCB_Extend InCB_Linker]*
    InCB_Extend_Linker,
  };

  /**
   * get break property except for InCB_Extend, InCB_Linker
   * @param codePoint
   * @return
   */
  static BreakProperty getBreakProperty(int codePoint);

  /**
   * get InCB_* property
   * @param codePoint
   * @return
   * if has no InCB property, return Any
   * otherwise, return InCB_Linker or InCB_Extend
   */
  static BreakProperty getInCBExtendOrLinker(int codePoint);
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
#include "grapheme_break_property.in"

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

template <bool Bool>
typename GraphemeBoundary<Bool>::BreakProperty
GraphemeBoundary<Bool>::getInCBExtendOrLinker(int codePoint) {
  if (codePoint < 0) {
    return BreakProperty::Any; // invalid code points are always grapheme boundary
  }

  using PropertyInterval = std::tuple<int, int, BreakProperty>;

#define UNICODE_PROPERTY_RANGE PropertyInterval
#define PROPERTY(E) BreakProperty::E
#include "incb_property.in"

#undef PROPERTY
#undef UNICODE_PROPERTY_RANGE

  struct Comp {
    bool operator()(const PropertyInterval &l, int r) const { return std::get<1>(l) < r; }

    bool operator()(int l, const PropertyInterval &r) const { return l < std::get<0>(r); }
  };

  auto iter = std::lower_bound(std::begin(incb_property_table), std::end(incb_property_table),
                               codePoint, Comp());
  if (iter != std::end(incb_property_table)) {
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
private:
  StringRef ref; // grapheme cluster
  bool invalid{false};

public:
  GraphemeCluster() = default;

  GraphemeCluster(StringRef ref, bool invalid) : ref(ref), invalid(invalid) {}

  StringRef getRef() const { return this->ref; }

  bool hasInvalid() const { return this->invalid; }
};

template <typename Stream>
class GraphemeScanner {
public:
  using BreakProperty = GraphemeBoundary::BreakProperty;

private:
  Stream stream;
  int codePoint{-1};
  BreakProperty state{BreakProperty::SOT};

protected:
  GraphemeScanner(Stream &&stream, int codePoint, BreakProperty property)
      : stream(std::move(stream)), codePoint(codePoint), state(property) {}

public:
  explicit GraphemeScanner(Stream &&stream) : stream(std::move(stream)) {}

  const auto &getStream() const { return this->stream; }

  int getCodePoint() const { return this->codePoint; }

  BreakProperty getProperty() const { return this->state; }

  /**
   * scan grapheme cluster boundary
   * @return
   * if grapheme cluster boundary is between prev codePoint and codePoint, return true
   */
  bool scanBoundary();

private:
  BreakProperty nextProperty() {
    this->codePoint = this->stream.nextCodePoint();
    return GraphemeBoundary::getBreakProperty(this->codePoint);
  }

  bool lookaheadInCBSeq() {
    bool r = false;
    const auto oldState = this->stream.saveState();

    enum InCBState : unsigned char {
      Init,
      Pre_Extend,
      Linker,
      Post_Extend,
    } prevInCB = Init;

    int code = this->codePoint;
    for (;; code = this->stream.nextCodePoint()) {
      if (code < 0) {
        goto END;
      }
      auto p = GraphemeBoundary::getInCBExtendOrLinker(code);
      switch (prevInCB) {
      case Init:
      case Pre_Extend:
        if (p == BreakProperty::InCB_Extend) {
          prevInCB = Pre_Extend;
          continue;
        } else if (p == BreakProperty::InCB_Linker) {
          prevInCB = Linker;
          continue;
        }
        goto END;
      case Linker:
      case Post_Extend:
        if (p == BreakProperty::InCB_Extend) {
          prevInCB = Post_Extend;
          continue;
        } else if (p == BreakProperty::InCB_Linker) {
          prevInCB = Linker;
          continue;
        }
        goto TRY_CONSONANT;
      }
    }

  TRY_CONSONANT:
    if (auto p = GraphemeBoundary::getBreakProperty(code); p == BreakProperty::InCB_Consonant) {
      r = true;
    }

  END:
    this->stream.restoreState(oldState);
    return r;
  }
};

// see. https://unicode.org/reports/tr29/#Grapheme_Cluster_Boundary_Rules
template <typename Stream>
bool GraphemeScanner<Stream>::scanBoundary() {
  if (!this->stream) {
    return true;
  }

  const auto after = this->nextProperty();
  const auto before = this->state;
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
  case BreakProperty::InCB_Consonant:
    if (this->lookaheadInCBSeq()) {
      this->state = BreakProperty::InCB_Extend_Linker;
      return false; // GB9c
    }
    break;
  case BreakProperty::InCB_Extend_Linker:
    if (after != BreakProperty::InCB_Consonant) {
      this->state = BreakProperty::InCB_Extend_Linker;
    }
    return false; // GB9c
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

  explicit Utf8GraphemeScanner(Utf8Stream &&stream, const char *charBegin = nullptr,
                               int codePoint = -1, BreakProperty p = BreakProperty::SOT)
      : GraphemeScanner(std::move(stream), codePoint, p), charBegin(charBegin) { // NOLINT
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
  GraphemeCluster next() {
    bool invalid = false;
    if (this->getProperty() != BreakProperty::SOT && this->hasNext() && this->getCodePoint() < 0) {
      invalid = true;
    }
    auto *begin = this->charBegin;
    const char *end;
    while (true) {
      end = this->getIter();
      if (this->scanBoundary()) {
        break;
      }
      if (this->getCodePoint() < 0) {
        invalid = true;
      }
    }
    this->charBegin = end;
    StringRef ref(begin, static_cast<size_t>(end - begin));
    return {ref, invalid};
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
  size_t count = 0;
  for (; count < limit && scanner.hasNext(); count++) {
    GraphemeCluster ret = scanner.next();
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
