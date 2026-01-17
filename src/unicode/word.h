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

#ifndef ARSH_UNICODE_WORD_HPP
#define ARSH_UNICODE_WORD_HPP

#include "../misc/detect.hpp"
#include "../misc/string_ref.hpp"
#include "../misc/unicode.hpp"
#include "property.h"

namespace arsh {

enum class WordBreakProperty : unsigned char {
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

  WSegSpace_EFZ,        // for WB3d, WB4
  AHLetter_Mid_NumLetQ, // for WB6, WB7. AHLetter * (MidLetter|MidNumLetQ)
  Hebrew_Letter_DQ,     // for WB7c. Hebrew_Letter * Double_Quote
  Numeric_Mid_NumLetQ,  // for WB11. Numeric * (MidNum | MidNumLetQ)
};

WordBreakProperty getWordBreakProperty(int codePoint);

template <typename Stream>
class WordScanner {
private:
  WordBreakProperty state{WordBreakProperty::SOT};
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
  WordBreakProperty nextProperty() {
    int codePoint = this->stream.nextCodePoint();
    return getWordBreakProperty(codePoint);
  }

  /**
   * lookahead next property (except for Extend, Format, ZWJ)
   * restore stream state
   * @return
   */
  WordBreakProperty lookahead() {
    auto p = WordBreakProperty::EOT;
    auto oldState = this->stream.saveState();
    while (this->stream) {
      p = this->nextProperty();
      switch (p) {
      case WordBreakProperty::Extend:
      case WordBreakProperty::Format:
      case WordBreakProperty::ZWJ:
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
  const int codePoint = this->stream.nextCodePoint();
  const auto after = getWordBreakProperty(codePoint);
  const auto before = this->state;
  this->state = after;
  const bool prevZWJ = this->zwj;
  this->zwj = after == WordBreakProperty::ZWJ;

  if (prevZWJ && ucp::isExtendedPictographic(codePoint)) {
    return false; // WB3c
  }

  switch (before) {
  case WordBreakProperty::SOT:
    return false;
  case WordBreakProperty::CR:
    if (after == WordBreakProperty::LF) {
      return false; // WB3
    }
    return true; // WB3a
  case WordBreakProperty::Newline:
  case WordBreakProperty::LF:
    return true; // WB3a
  default:
    switch (after) {
    case WordBreakProperty::Newline:
    case WordBreakProperty::CR:
    case WordBreakProperty::LF:
      return true; // WB3b
    case WordBreakProperty::Extend:
    case WordBreakProperty::Format:
    case WordBreakProperty::ZWJ:
      if (before == WordBreakProperty::WSegSpace) {
        this->state = WordBreakProperty::WSegSpace_EFZ;
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

  auto requiredProperty = WordBreakProperty::SOT;

  switch (before) {
  case WordBreakProperty::WSegSpace:
    if (after == WordBreakProperty::WSegSpace) {
      return false; // WB3d
    }
    break;
  case WordBreakProperty::ALetter:
  case WordBreakProperty::Hebrew_Letter: // AHLetter = (ALetter | Hebrew_Letter)
    switch (after) {
    case WordBreakProperty::ALetter:
    case WordBreakProperty::Hebrew_Letter: // AHLetter = (ALetter | Hebrew_Letter)
      return false;                        // WB5
    case WordBreakProperty::MidLetter:
    case WordBreakProperty::MidNumLet:
    case WordBreakProperty::Single_Quote:
      requiredProperty = WordBreakProperty::AHLetter_Mid_NumLetQ; // for WB6
      break;
    default:
      break;
    }
    break;
  case WordBreakProperty::AHLetter_Mid_NumLetQ:
    // AHLetter = (ALetter | Hebrew_Letter)
    if (after == WordBreakProperty::ALetter || after == WordBreakProperty::Hebrew_Letter) {
      return false; // WB7
    }
    break;
  default:
    break;
  }
  if (before == WordBreakProperty::Hebrew_Letter) {
    if (after == WordBreakProperty::Single_Quote) {
      return false; // WB7a
    } else if (after == WordBreakProperty::Double_Quote) {
      requiredProperty = WordBreakProperty::Hebrew_Letter_DQ; // for WB7b
    }
  } else if (before == WordBreakProperty::Hebrew_Letter_DQ) {
    if (after == WordBreakProperty::Hebrew_Letter) {
      return false; // WB7c
    }
  }

  switch (before) {
  case WordBreakProperty::Numeric:
  case WordBreakProperty::ALetter:
  case WordBreakProperty::Hebrew_Letter: // AHLetter = (ALetter | Hebrew_Letter)
    if (after == WordBreakProperty::Numeric) {
      return false; // WB8, WB9
    }
    break;
  default:
    break;
  }

  switch (before) {
  case WordBreakProperty::Numeric:
    switch (after) {
    case WordBreakProperty::ALetter:
    case WordBreakProperty::Hebrew_Letter: // AHLetter = (ALetter | Hebrew_Letter)
      return false;                        // WB10
    case WordBreakProperty::MidNum:
    case WordBreakProperty::MidNumLet:
    case WordBreakProperty::Single_Quote: {
      requiredProperty = WordBreakProperty::Numeric_Mid_NumLetQ; // for WB12
      break;
    }
    default:
      break;
    }
    break;
  case WordBreakProperty::Numeric_Mid_NumLetQ:
    if (after == WordBreakProperty::Numeric) {
      return false; // WB11
    }
    break;
  case WordBreakProperty::Katakana:
    if (after == WordBreakProperty::Katakana) {
      return false; // WB13
    }
    break;
  default:
    break;
  }

  if (requiredProperty != WordBreakProperty::SOT) {
    auto next = this->lookahead();
    switch (requiredProperty) {
    case WordBreakProperty::Numeric_Mid_NumLetQ:
      if (next == WordBreakProperty::Numeric) {
        this->state = requiredProperty;
        return false; // WB12
      }
      break;
    case WordBreakProperty::Hebrew_Letter_DQ:
      if (next == WordBreakProperty::Hebrew_Letter) {
        this->state = requiredProperty;
        return false; // WB7b
      }
      break;
    case WordBreakProperty::AHLetter_Mid_NumLetQ:
      if (next == WordBreakProperty::ALetter || next == WordBreakProperty::Hebrew_Letter) {
        this->state = requiredProperty;
        return false; // WB6
      }
      break;
    default:
      break;
    }
  }

  switch (before) {
  case WordBreakProperty::ALetter:
  case WordBreakProperty::Hebrew_Letter: // AHLetter = (ALetter | Hebrew_Letter)
  case WordBreakProperty::Numeric:
  case WordBreakProperty::Katakana:
    if (after == WordBreakProperty::ExtendNumLet) {
      return false; // WB13a
    }
    break;
  case WordBreakProperty::ExtendNumLet:
    switch (after) {
    case WordBreakProperty::ExtendNumLet:
      return false; // WB13a
    case WordBreakProperty::ALetter:
    case WordBreakProperty::Hebrew_Letter: // AHLetter = (ALetter | Hebrew_Letter)
    case WordBreakProperty::Numeric:
    case WordBreakProperty::Katakana:
      return false; // WB13b
    default:
      break;
    }
    break;
  case WordBreakProperty::Regional_Indicator:
    if (after == WordBreakProperty::Regional_Indicator) {
      this->state = WordBreakProperty::Any;
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

} // namespace arsh

#endif // ARSH_UNICODE_WORD_HPP
