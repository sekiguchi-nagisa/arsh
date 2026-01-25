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

#ifndef ARSH_UNICODE_GRAPHEME_HPP
#define ARSH_UNICODE_GRAPHEME_HPP

#include "../misc/detect.hpp"
#include "../misc/string_ref.hpp"
#include "../misc/unicode.hpp"

namespace arsh {

class GraphemeBoundary {
public:
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

    // InCB_Consonant [InCB_Extend InCB_Linker]* InCB_Linker [InCB_Extend InCB_Linker]*
    InCB_Consonant_with_Linker, // (at-least one InCB_Linker)
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
   * if no InCB property, return Any
   * otherwise, return InCB_Linker or InCB_Extend
   * (also BreakProperty::Extend or BreakProperty::ZWJ)
   */
  static BreakProperty getInCBExtendOrLinker(int codePoint);

private:
  BreakProperty state{BreakProperty::SOT};
  bool emojiSeq{false};

public:
  GraphemeBoundary() = default;

  explicit GraphemeBoundary(BreakProperty property) : state(property) {}

  BreakProperty getProperty() const { return this->state; }

  bool foundEmojiSeq() const { return this->emojiSeq; }

  /**
   * check grapheme cluster boundary
   * @param codePoint
   * @return
   * if grapheme cluster boundary is between prev codePoint (state) and codePoint, return true
   */
  bool checkBoundary(int codePoint);
};

class GraphemeCluster {
private:
  StringRef ref; // grapheme cluster
  bool invalid{false};
  bool emojiSeq{false}; // emoji modifier sequence or emoji ZWJ sequence

public:
  GraphemeCluster() = default;

  GraphemeCluster(StringRef ref, bool invalid, bool emojiSeq)
      : ref(ref), invalid(invalid), emojiSeq(emojiSeq) {}

  StringRef getRef() const { return this->ref; }

  bool hasInvalid() const { return this->invalid; }

  bool isEmojiSeq() const { return this->emojiSeq; }
};

template <typename Stream>
class GraphemeScanner {
public:
  using BreakProperty = GraphemeBoundary::BreakProperty;

protected:
  int codePoint{-1};
  GraphemeBoundary boundary;
  Stream stream;

  GraphemeScanner(Stream &&stream, int codePoint, GraphemeBoundary::BreakProperty property)
      : codePoint(codePoint), boundary(property), stream(std::move(stream)) {}

public:
  explicit GraphemeScanner(Stream &&stream) : stream(std::move(stream)) {}

  const auto &getStream() const { return this->stream; }

  int getCodePoint() const { return this->codePoint; }

  BreakProperty getProperty() const { return this->boundary.getProperty(); }

  /**
   * scan grapheme cluster boundary
   * @return
   * if grapheme cluster boundary is between prev codePoint and codePoint, return true
   */
  bool scanBoundary() {
    if (!this->stream) {
      return true;
    }
    this->codePoint = this->stream.nextCodePoint();
    return this->boundary.checkBoundary(this->codePoint);
  }
};

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

  GraphemeCluster next() {
    bool invalid = false;
    if (this->boundary.getProperty() != BreakProperty::SOT && this->hasNext() &&
        this->getCodePoint() < 0) {
      invalid = true;
    }
    auto *begin = this->charBegin;
    const char *end;
    bool emojiSeq = false;
    while (true) {
      end = this->getIter();
      emojiSeq = emojiSeq || this->boundary.foundEmojiSeq();
      if (this->scanBoundary()) {
        break;
      }
      if (this->getCodePoint() < 0) {
        invalid = true;
      }
    }
    this->charBegin = end;
    StringRef ref(begin, static_cast<size_t>(end - begin));
    return {ref, invalid, emojiSeq};
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

inline bool fuzzyFind(StringRef haystack, StringRef needle) { // NOLINT
  size_t pos = 0;
  bool matched = true;
  iterateGrapheme(needle, [&](const GraphemeCluster &grapheme) {
    auto ret = haystack.find(grapheme.getRef(), pos);
    if (ret == StringRef::npos) {
      matched = false;
      return false;
    }
    pos = ret + grapheme.getRef().size();
    return true;
  });
  return matched;
}

// for grapheme cluster width

#define EACH_CHAR_WIDTH_PROPERTY_CHECK(OP)                                                         \
  OP(EAW, "○")                                                                                     \
  OP(RGI, "🇯")                                                                                     \
  OP(EMOJI_FLAG_SEQ, "🇯🇵")                                                                         \
  OP(EMOJI_ZWJ_SEQ, "👩🏼‍🏭")

enum class CharWidthPropertyCheck : unsigned char {
#define GEN_ENUM(E, S) E,
  EACH_CHAR_WIDTH_PROPERTY_CHECK(GEN_ENUM)
#undef GEN_ENUM
};

ArrayRef<std::pair<CharWidthPropertyCheck, const char *>> getCharWidthPropertyChecks();

struct CharWidthProperties {
  AmbiguousCharWidth eaw{AmbiguousCharWidth::HALF};
  unsigned char reginalIndicatorWidth{0}; // if 0, use the original width
  unsigned char flagSeqWidth{2};
  bool zwjSeqFallback{false};
  bool replaceInvalid{false};

  void setProperty(CharWidthPropertyCheck p, std::size_t len) {
    switch (p) {
    case CharWidthPropertyCheck::RGI:
      this->reginalIndicatorWidth = len;
      break;
    case CharWidthPropertyCheck::EAW:
      this->eaw = len == 2 ? AmbiguousCharWidth::FULL : AmbiguousCharWidth::HALF;
      break;
    case CharWidthPropertyCheck::EMOJI_FLAG_SEQ:
      this->flagSeqWidth = len;
      break;
    case CharWidthPropertyCheck::EMOJI_ZWJ_SEQ:
      this->zwjSeqFallback = len > 2;
      break;
    }
  }
};

/**
 * get width of a grapheme cluster
 * @param ps
 * @param ret
 * @return
 */
unsigned int getGraphemeWidth(const CharWidthProperties &ps, const GraphemeCluster &ret);

} // namespace arsh

#endif // ARSH_UNICODE_GRAPHEME_HPP
