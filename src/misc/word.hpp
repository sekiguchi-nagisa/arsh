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
    EOT, // sentinel

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
};

template <bool Bool>
typename WordBoundary<Bool>::BreakProperty WordBoundary<Bool>::getBreakProperty(int codePoint) {
  if (codePoint < 0) {
    return BreakProperty::Newline; // invalid code points are always word boundary
  }

  using PropertyInterval = std::tuple<int, int, BreakProperty>;

#define UNICODE_PROPERTY_RANGE PropertyInterval
#define PROPERTY(E) BreakProperty::E
#include "word_break_property.in"
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

} // namespace detail

using WordBoundary = detail::WordBoundary<true>;

template <typename Stream>
class WordScanner {
private:
  WordBoundary::BreakProperty state{WordBoundary::BreakProperty::SOT};
  bool zwj{false};
  Stream &stream;

public:
  explicit WordScanner(Stream &stream) : stream(stream) {}

  const Stream &getStream() const { return this->stream; }

  /**
   * low level api. normally unused
   * @return
   * if code point between prev and cur is word boundary, return true
   */
  bool scanBoundary();

private:
  WordBoundary::BreakProperty nextProperty() {
    int codePoint = this->stream.nextCodePoint();
    return WordBoundary::getBreakProperty(codePoint);
  }

  /**
   * lookahead next property (except for Extend, Format, ZWJ)
   * restore stream state
   * @return
   */
  WordBoundary::BreakProperty lookahead() {
    auto p = WordBoundary::BreakProperty::EOT;
    auto oldState = this->stream.saveState();
    while (this->stream) {
      p = this->nextProperty();
      switch (p) {
      case WordBoundary::BreakProperty::Extend:
      case WordBoundary::BreakProperty::Format:
      case WordBoundary::BreakProperty::ZWJ:
        continue;
      default:
        break;
      }
      break;
    }
    this->stream.restoreState(oldState);
    return p;
  }
};

// see, https://www.unicode.org/reports/tr29/tr29-39.html#Word_Boundaries
template <typename Stream>
bool WordScanner<Stream>::scanBoundary() {
  if (!this->stream) {
    return true;
  }
  const auto after = this->nextProperty();
  const auto before = this->state;
  this->state = after;
  const bool prevZWJ = this->zwj;
  this->zwj = after == WordBoundary::BreakProperty::ZWJ;

  if (prevZWJ && after == WordBoundary::BreakProperty::Extended_Pictographic) {
    return false; // WB3c
  }

  switch (before) {
  case WordBoundary::BreakProperty::SOT:
    return false;
  case WordBoundary::BreakProperty::CR:
    if (after == WordBoundary::BreakProperty::LF) {
      return false; // WB3
    }
    return true; // WB3a
  case WordBoundary::BreakProperty::Newline:
  case WordBoundary::BreakProperty::LF:
    return true; // WB3a
  default:
    switch (after) {
    case WordBoundary::BreakProperty::Newline:
    case WordBoundary::BreakProperty::CR:
    case WordBoundary::BreakProperty::LF:
      return true; // WB3b
    case WordBoundary::BreakProperty::Extend:
    case WordBoundary::BreakProperty::Format:
    case WordBoundary::BreakProperty::ZWJ:
      if (before == WordBoundary::BreakProperty::WSegSpace) {
        this->state = WordBoundary::BreakProperty::WSegSpace_EFZ;
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

  auto requiredProperty = WordBoundary::BreakProperty::SOT;

  switch (before) {
  case WordBoundary::BreakProperty::WSegSpace:
    if (after == WordBoundary::BreakProperty::WSegSpace) {
      return false; // WB3d
    }
    break;
  case WordBoundary::BreakProperty::ALetter:
  case WordBoundary::BreakProperty::Hebrew_Letter: // AHLetter = (ALetter | Hebrew_Letter)
    switch (after) {
    case WordBoundary::BreakProperty::ALetter:
    case WordBoundary::BreakProperty::Hebrew_Letter: // AHLetter = (ALetter | Hebrew_Letter)
      return false;                                  // WB5
    case WordBoundary::BreakProperty::MidLetter:
    case WordBoundary::BreakProperty::MidNumLet:
    case WordBoundary::BreakProperty::Single_Quote:
      requiredProperty = WordBoundary::BreakProperty::AHLetter_Mid_NumLetQ; // for WB6
      break;
    default:
      break;
    }
    break;
  case WordBoundary::BreakProperty::AHLetter_Mid_NumLetQ:
    // AHLetter = (ALetter | Hebrew_Letter)
    if (after == WordBoundary::BreakProperty::ALetter ||
        after == WordBoundary::BreakProperty::Hebrew_Letter) {
      return false; // WB7
    }
    break;
  default:
    break;
  }
  if (before == WordBoundary::BreakProperty::Hebrew_Letter) {
    if (after == WordBoundary::BreakProperty::Single_Quote) {
      return false; // WB7a
    } else if (after == WordBoundary::BreakProperty::Double_Quote) {
      requiredProperty = WordBoundary::BreakProperty::Hebrew_Letter_DQ; // for WB7b
    }
  } else if (before == WordBoundary::BreakProperty::Hebrew_Letter_DQ) {
    if (after == WordBoundary::BreakProperty::Hebrew_Letter) {
      return false; // WB7c
    }
  }

  switch (before) {
  case WordBoundary::BreakProperty::Numeric:
  case WordBoundary::BreakProperty::ALetter:
  case WordBoundary::BreakProperty::Hebrew_Letter: // AHLetter = (ALetter | Hebrew_Letter)
    if (after == WordBoundary::BreakProperty::Numeric) {
      return false; // WB8, WB9
    }
    break;
  default:
    break;
  }

  switch (before) {
  case WordBoundary::BreakProperty::Numeric:
    switch (after) {
    case WordBoundary::BreakProperty::ALetter:
    case WordBoundary::BreakProperty::Hebrew_Letter: // AHLetter = (ALetter | Hebrew_Letter)
      return false;                                  // WB10
    case WordBoundary::BreakProperty::MidNum:
    case WordBoundary::BreakProperty::MidNumLet:
    case WordBoundary::BreakProperty::Single_Quote: {
      requiredProperty = WordBoundary::BreakProperty::Numeric_Mid_NumLetQ; // for WB12
      break;
    }
    default:
      break;
    }
    break;
  case WordBoundary::BreakProperty::Numeric_Mid_NumLetQ:
    if (after == WordBoundary::BreakProperty::Numeric) {
      return false; // WB11
    }
    break;
  case WordBoundary::BreakProperty::Katakana:
    if (after == WordBoundary::BreakProperty::Katakana) {
      return false; // WB13
    }
    break;
  default:
    break;
  }

  if (requiredProperty != WordBoundary::BreakProperty::SOT) {
    auto next = this->lookahead();
    switch (requiredProperty) {
    case WordBoundary::BreakProperty::Numeric_Mid_NumLetQ:
      if (next == WordBoundary::BreakProperty::Numeric) {
        this->state = requiredProperty;
        return false; // WB12
      }
      break;
    case WordBoundary::BreakProperty::Hebrew_Letter_DQ:
      if (next == WordBoundary::BreakProperty::Hebrew_Letter) {
        this->state = requiredProperty;
        return false; // WB7b
      }
      break;
    case WordBoundary::BreakProperty::AHLetter_Mid_NumLetQ:
      if (next == WordBoundary::BreakProperty::ALetter ||
          next == WordBoundary::BreakProperty::Hebrew_Letter) {
        this->state = requiredProperty;
        return false; // WB6
      }
      break;
    default:
      break;
    }
  }

  switch (before) {
  case WordBoundary::BreakProperty::ALetter:
  case WordBoundary::BreakProperty::Hebrew_Letter: // AHLetter = (ALetter | Hebrew_Letter)
  case WordBoundary::BreakProperty::Numeric:
  case WordBoundary::BreakProperty::Katakana:
    if (after == WordBoundary::BreakProperty::ExtendNumLet) {
      return false; // WB13a
    }
    break;
  case WordBoundary::BreakProperty::ExtendNumLet:
    switch (after) {
    case WordBoundary::BreakProperty::ExtendNumLet:
      return false; // WB13a
    case WordBoundary::BreakProperty::ALetter:
    case WordBoundary::BreakProperty::Hebrew_Letter: // AHLetter = (ALetter | Hebrew_Letter)
    case WordBoundary::BreakProperty::Numeric:
    case WordBoundary::BreakProperty::Katakana:
      return false; // WB13b
    default:
      break;
    }
    break;
  case WordBoundary::BreakProperty::Regional_Indicator:
    if (after == WordBoundary::BreakProperty::Regional_Indicator) {
      this->state = WordBoundary::BreakProperty::Any;
      return false; // WB15, WB16
    }
    break;
  default:
    break;
  }
  return true; // WB999
}

class Utf8WordScanner : public WordScanner<Utf8Stream> {
private:
  const char *wordBegin;

public:
  explicit Utf8WordScanner(Utf8Stream &stream) : WordScanner(stream), wordBegin(this->getIter()) {}

  bool hasNext() const { return this->wordBegin != this->getStream().end; }

  StringRef next() {
    auto *begin = this->wordBegin;
    const char *end;
    while (true) {
      end = this->getIter();
      if (this->scanBoundary()) {
        break;
      }
    }
    this->wordBegin = end;
    return {begin, static_cast<size_t>(end - begin)};
  }

private:
  const char *getIter() const { return this->getStream().iter; }
};

template <typename Consumer>
static constexpr bool word_consumer_requirement_v =
    std::is_same_v<void, std::invoke_result_t<Consumer, StringRef>> ||
    std::is_same_v<bool, std::invoke_result_t<Consumer, StringRef>>;

template <typename Func, enable_when<word_consumer_requirement_v<Func>> = nullptr>
size_t iterateWordUntil(StringRef ref, size_t limit, Func consumer) {
  Utf8Stream stream(ref.begin(), ref.end());
  Utf8WordScanner scanner(stream);
  size_t count = 0;
  for (; count < limit && scanner.hasNext(); count++) {
    auto ret = scanner.next();
    constexpr auto v = std::is_same_v<bool, std::invoke_result_t<Func, StringRef>>;
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

template <typename Func, enable_when<word_consumer_requirement_v<Func>> = nullptr>
size_t iterateWord(StringRef ref, Func consumer) {
  return iterateWordUntil(ref, static_cast<size_t>(-1), std::move(consumer));
}

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_WORD_HPP
