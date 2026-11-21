/*
 * Copyright (C) 2026 Nagisa Sekiguchi
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

#include "prefilter.h"

#include "capture.h"
#include "input.h"
#include "instruction.h"
#include "matcher.h"
#include "misc/num_util.hpp"
#include "misc/rtti.hpp"
#include "misc/unicode.hpp"
#include "unicode/case_fold.h"

namespace arsh::regex {

#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (!(E)) {                                                                                    \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

static void fill(const int codePoint, FirstBitSet &set, const bool fold) {
  {
    char buf[4];
    UnicodeUtil::codePointToUtf8(codePoint, buf);
    set.set(static_cast<unsigned char>(buf[0]), true);
  }
  if (fold) {
    for (auto &e : doSimpleCaseUnfold(codePoint)) {
      char buf[4];
      UnicodeUtil::codePointToUtf8(e.unfold, buf);
      set.set(static_cast<unsigned char>(buf[0]), true);
    }
  }
}

static void fill(const CodePointSetRef ref, FirstBitSet &set, const bool fold) {
  for (auto &range : ref.getBMPRanges()) {
    for (int first = range.firstBMP(); first <= range.lastBMP(); first++) {
      fill(first, set, fold);
    }
  }
  for (auto &range : ref.getNonBMPRanges()) {
    for (int first = range.firstNonBMP(); first <= range.lastNonBMP(); first++) {
      fill(first, set, fold);
    }
  }
  for (auto &range : ref.getPackedNonBMPRanges()) {
    for (int first = range.firstNonBMP(); first <= range.lastNonBMP(); first++) {
      fill(first, set, fold);
    }
  }
}

static void fill(const AsciiSet asciiSet, FirstBitSet &set, const bool fold) {
  for (auto code = AsciiSet::MIN; code <= AsciiSet::MAX; code++) {
    if (asciiSet.contains(code)) {
      fill(code, set, fold);
    }
  }
}

static void fill(const StringRef ref, FirstBitSet &set, const bool fold) {
  assert(!ref.empty());
  int codePoint;
  unsigned int len = UnicodeUtil::wtf8ToCodePoint(ref.begin(), ref.end(), codePoint);
  static_cast<void>(len);
  assert(len);
  fill(codePoint, set, fold);
}

static void fill(const ucp::RGIEmojiSeq emoji, FirstBitSet &set, const bool fold) {
  auto walker = [&](StringRef ref, unsigned char p) {
    if (hasFlag(toUnderlying(emoji), p)) {
      fill(ref, set, fold);
    }
    return true;
  };
  ucp::getEmojiTrie().iterate(walker);
}

static void fill(const PackedRadixTree radix, FirstBitSet &set, const bool fold) {
  auto walker = [&](StringRef ref, unsigned char) {
    fill(ref, set, fold);
    return true;
  };
  radix.iterate(walker);
}

static unsigned char sizeOfInst(const Inst *inst) {
  constexpr unsigned char table[] = {
#define GEN_TABLE(E) sizeof(E##Ins),
      EACH_RE_OPCODE(GEN_TABLE)
#undef GEN_TABLE
  };
  return table[toUnderlying(inst->op)];
}

struct FirstBytesCollector {
  const ArrayRef<Inst> instSeq;
  const ArrayRef<Matcher> matchers;
  FirstBitSet set;

  FirstBytesCollector(ArrayRef<Inst> instSeq, ArrayRef<Matcher> matchers)
      : instSeq(instSeq), matchers(matchers) {}

  bool operator()() { return this->collect(this->instSeq.begin(), this->instSeq.end()); }

  bool collect(const Inst *begin, const Inst *end);
};

bool FirstBytesCollector::collect(const Inst *begin, const Inst *end) {
  while (begin != end) {
    switch (begin->op) {
    case OpCode::Nop:
    case OpCode::Match:
    case OpCode::Jump:
    case OpCode::Word:
    case OpCode::IWord:
    case OpCode::PrepareRadix:
    case OpCode::BeginCapture:
    case OpCode::EndCapture:
    case OpCode::ResetCaptures:
    case OpCode::EndLoop:
    case OpCode::EndLookAround:
      begin += sizeOfInst(begin);
      break;
    case OpCode::Alt: {
      auto &ins = cast<AltIns>(*begin);
      begin += sizeof(AltIns);
      TRY(this->collect(begin, this->instSeq.begin() + ins.getSecond()));
      begin = this->instSeq.begin() + ins.getSecond();
      continue;
    }
    case OpCode::Start:
    case OpCode::End:
    case OpCode::Any:
    case OpCode::AnyExceptNL:
    case OpCode::Grapheme:
    case OpCode::LBAny:
    case OpCode::LBChar:
    case OpCode::LBCharSet:
    case OpCode::PrepareLBRadix:
    case OpCode::LBRadixOrEmoji:
    case OpCode::LBString:
    case OpCode::LBEndCapture:
    case OpCode::BackRef:
    case OpCode::IBackRef:
    case OpCode::LBBackRef:
      return false;
    case OpCode::Char:
      fill(cast<CharIns>(*begin).getCodePoint(), this->set, false);
      return true;
    case OpCode::IChar:
      fill(cast<ICharIns>(*begin).getCodePoint(), this->set, true);
      return true;
    case OpCode::CharSet:
      if (auto &ins = cast<CharSetIns>(*begin); !ins.invert) {
        if (auto &m = this->matchers[ins.getMatcherIndex()]; m.type() == MatcherType::ASCII) {
          fill(m.asAsciiSet(), this->set, false);
        } else {
          fill(m.asCodePointSetRef(), this->set, false);
        }
        return true;
      }
      return false;
    case OpCode::ICharSet:
      if (auto &ins = cast<ICharSetIns>(*begin); !ins.invert) {
        if (auto &m = this->matchers[ins.getMatcherIndex()]; m.type() == MatcherType::ASCII) {
          fill(m.asAsciiSet(), this->set, true);
        } else {
          fill(m.asCodePointSetRef(), this->set, true);
        }
        return true;
      }
      return false;
    case OpCode::RadixOrEmoji: {
      auto &ins = cast<RadixOrEmojiIns>(*begin);
      if (ins.hasEmoji()) {
        fill(ins.emoji, this->set, ins.ignoreCase());
      }
      if (ins.hasRadix) {
        fill(this->matchers[ins.getIndex()].asRadixTree(), this->set, ins.ignoreCase());
      }
      if (ins.nextOffset) {
        begin += sizeof(RadixOrEmojiIns);
        continue;
      }
      return true;
    }
    case OpCode::String:
      fill(this->matchers[cast<StringIns>(*begin).getIndex()].asStrRef(), this->set, false);
      return true;
    case OpCode::BeginLoop:
      if (auto &ins = cast<BeginLoopIns>(*begin); ins.getMin() > 0) {
        begin += sizeof(BeginLoopIns);
        return this->collect(begin, this->instSeq.begin() + ins.getOuter());
      }
      return false;
    case OpCode::BeginLookAround:
      begin = this->instSeq.begin() + cast<BeginLookAroundIns>(*begin).getTarget();
      break; // skip look-around body
    }
  }
  return false;
}

class CodePointCounter {
private:
  static constexpr unsigned int MAX_COUNT = UINT16_MAX;

  static_assert(MAX_COUNT <=
                std::numeric_limits<decltype(Prefilter().getMaxPrevCodePoints())>::max());

  unsigned int min{0};
  unsigned int max{0};

public:
  unsigned short getMin() const { return this->min; }

  unsigned short getMax() const { return this->max; }

  bool increment(unsigned int deltaMin, unsigned int deltaMax) {
    if (deltaMin <= MAX_COUNT && this->min <= MAX_COUNT - deltaMin && deltaMax <= MAX_COUNT &&
        this->max <= MAX_COUNT - deltaMax) {
      this->min += deltaMin;
      this->max += deltaMax;
      return true;
    }
    return false;
  }
};

struct PrefilterBuilder {
  const ArrayRef<Inst> instSeq;
  const ArrayRef<Matcher> matchers;
  const unsigned int captureGroupCount; // 1-based index. if 0, indicate no captures
  const NamedCaptureGroups &namedGroups;

  PrefilterBuilder(ArrayRef<Inst> instSeq, ArrayRef<Matcher> matchers,
                   unsigned int captureGroupCount, const NamedCaptureGroups &named)
      : instSeq(instSeq), matchers(matchers), captureGroupCount(captureGroupCount),
        namedGroups(named) {}

  Prefilter operator()() const {
    Prefilter prefilter = this->resolveAnchor();
    if (prefilter.getType() == Prefilter::Type::NONE) {
      CodePointCounter counter;
      this->build(this->instSeq.begin(), this->instSeq.end(), counter, &prefilter);
    }
    return prefilter;
  }

private:
  bool build(const Inst *inst, const Inst *end, CodePointCounter &counter,
             Prefilter *prefilter) const;

  Prefilter resolveAnchor() const {
    Prefilter prefilter;
    std::vector<bool> captureUsage;
    if (this->resolveAnchor(this->instSeq.begin(), this->instSeq.end(), prefilter, captureUsage)) {
      assert(prefilter.getType() == Prefilter::Type::START ||
             prefilter.getType() == Prefilter::Type::LINE_START);
      return prefilter;
    }
    return {};
  }

  bool resolveAnchor(const Inst *inst, const Inst *end, Prefilter &prefilter,
                     std::vector<bool> &captureUsage) const;

  void tryToCollectCaptureUsage(std::vector<bool> &set) const;

  template <typename T>
  static void tryToUpdate(Prefilter &prefilter, const CodePointCounter &counter, T &&value) {
    Prefilter newFilter(counter.getMin(), std::forward<T>(value), counter.getMax());
    if (prefilter.isLowPriorityThan(newFilter)) {
      prefilter = newFilter;
    }
  }
};

bool PrefilterBuilder::build(const Inst *inst, const Inst *const end, CodePointCounter &counter,
                             Prefilter *prefilter) const {
  while (inst != end) {
    switch (inst->op) {
    case OpCode::Nop:
    case OpCode::Match:
    case OpCode::Jump:
    case OpCode::Word:
    case OpCode::IWord:
    case OpCode::PrepareRadix:
    case OpCode::BeginCapture:
    case OpCode::EndCapture:
    case OpCode::ResetCaptures:
    case OpCode::EndLookAround:
    case OpCode::EndLoop:
      inst += sizeOfInst(inst);
      continue;
    case OpCode::Alt: {
      if (prefilter) {
        FirstBytesCollector collector(this->instSeq, this->matchers);
        if (collector.collect(inst, end) && collector.set.any()) {
          tryToUpdate(*prefilter, counter, collector.set);
        }
      }
      return false; // TODO
    }
    case OpCode::Start:
    case OpCode::End:
    case OpCode::LBAny:
    case OpCode::Grapheme:
    case OpCode::LBChar:
    case OpCode::LBCharSet:
    case OpCode::PrepareLBRadix:
    case OpCode::LBRadixOrEmoji:
    case OpCode::LBString:
    case OpCode::LBEndCapture:
    case OpCode::BackRef:
    case OpCode::IBackRef:
    case OpCode::LBBackRef:
      return false;
    case OpCode::Any:
    case OpCode::AnyExceptNL:
      TRY(counter.increment(1, 1));
      inst += sizeOfInst(inst);
      continue;
    case OpCode::Char:
      if (prefilter) {
        Prefilter::CodePoint cp;
        cp.size = UnicodeUtil::codePointToUtf8(cast<CharIns>(*inst).getCodePoint(), cp.data.data());
        tryToUpdate(*prefilter, counter, cp);
      }
      TRY(counter.increment(1, 1));
      inst += sizeof(CharIns);
      continue;
    case OpCode::IChar:
    case OpCode::CharSet:
    case OpCode::ICharSet:
      if (prefilter) {
        FirstBytesCollector collector(this->instSeq, this->matchers);
        if (collector.collect(inst, end) && collector.set.any()) {
          tryToUpdate(*prefilter, counter, collector.set);
        }
      }
      TRY(counter.increment(1, 1));
      inst += sizeOfInst(inst);
      continue;
    case OpCode::RadixOrEmoji: {
      if (prefilter) {
        FirstBytesCollector collector(this->instSeq, this->matchers);
        if (collector.collect(inst, end) && collector.set.any()) {
          tryToUpdate(*prefilter, counter, collector.set);
        }
      }
      auto &ins = cast<RadixOrEmojiIns>(*inst);
      unsigned int codePointCount = 1;
      if (ins.hasEmoji()) {
        codePointCount = ucp::getEmojiTrie().getMaxCodePointCount();
      }
      if (ins.hasRadix) {
        codePointCount = std::max<unsigned int>(
            codePointCount, matchers[ins.getIndex()].asRadixTree().getMaxCodePointCount());
      }
      TRY(counter.increment(1, codePointCount));
      inst += sizeof(RadixOrEmojiIns);
      inst += ins.nextOffset;
      continue;
    }
    case OpCode::String: {
      const StringRef ref = this->matchers[cast<StringIns>(*inst).getIndex()].asStrRef();
      if (prefilter) {
        tryToUpdate(*prefilter, counter, ref);
      }
      unsigned int count = 0;
      for (auto iter = ref.begin(); iter != ref.end();) {
        count++;
        unsafeNextUtf8Noreturn(iter);
      }
      TRY(counter.increment(count, count));
      inst += sizeof(StringIns);
      continue;
    }
    case OpCode::BeginLoop: {
      auto &ins = cast<BeginLoopIns>(*inst);
      if (prefilter) {
        FirstBytesCollector collector(this->instSeq, this->matchers);
        if (collector.collect(inst, end) && collector.set.any()) {
          tryToUpdate(*prefilter, counter, collector.set);
        }
      }
      CodePointCounter sub;
      {
        auto subBegin = inst;
        subBegin += sizeof(BeginLoopIns); // due to suppress code analysis warning
        const auto subEnd = this->instSeq.begin() + ins.getOuter();
        TRY(this->build(subBegin, subEnd, sub, nullptr));
      }
      unsigned int deltaMin;
      TRY(!mul_overflow(sub.getMin(), ins.getMin(), deltaMin));
      unsigned int deltaMax;
      TRY(!mul_overflow(sub.getMax(), ins.getMax(), deltaMax));
      TRY(counter.increment(deltaMin, deltaMax));
      inst = this->instSeq.begin() + ins.getOuter();
      continue;
    }
    case OpCode::BeginLookAround:
      inst = this->instSeq.begin() + cast<BeginLookAroundIns>(*inst).getTarget();
      break; // skip look-around body
    }
  }
  return true;
}

static bool noneOr(const Prefilter &p, const Prefilter::Type t) {
  return p.getType() == Prefilter::Type::NONE || p.getType() == t;
}

bool PrefilterBuilder::resolveAnchor(const Inst *inst, const Inst *end, Prefilter &prefilter,
                                     std::vector<bool> &captureUsage) const {
  while (inst != end) {
    switch (inst->op) {
    case OpCode::Nop:
    case OpCode::Match:
    case OpCode::Jump:
    case OpCode::End:
    case OpCode::Word:
    case OpCode::IWord:
    case OpCode::Any:
    case OpCode::AnyExceptNL:
    case OpCode::LBAny:
    case OpCode::Grapheme:
    case OpCode::Char:
    case OpCode::IChar:
    case OpCode::LBChar:
    case OpCode::CharSet:
    case OpCode::ICharSet:
    case OpCode::LBCharSet:
    case OpCode::PrepareRadix:
    case OpCode::RadixOrEmoji:
    case OpCode::PrepareLBRadix:
    case OpCode::LBRadixOrEmoji:
    case OpCode::String:
    case OpCode::LBString:
    case OpCode::EndCapture:
    case OpCode::LBEndCapture:
    case OpCode::BackRef:
    case OpCode::IBackRef:
    case OpCode::LBBackRef:
    case OpCode::EndLoop:
    case OpCode::BeginLookAround:
    case OpCode::EndLookAround:
      break;
    case OpCode::Alt: {
      auto &ins = cast<AltIns>(*inst);
      inst += sizeof(AltIns);
      TRY(this->resolveAnchor(inst, this->instSeq.begin() + ins.getSecond(), prefilter,
                              captureUsage));
      inst = this->instSeq.begin() + ins.getSecond();
      continue;
    }
    case OpCode::Start:
      if (cast<StartIns>(*inst).multiline) {
        if (noneOr(prefilter, Prefilter::Type::LINE_START)) {
          prefilter = Prefilter::lineStart();
          return true;
        }
      } else if (noneOr(prefilter, Prefilter::Type::START)) {
        prefilter = Prefilter::start();
        return true;
      }
      return false;
    case OpCode::BeginCapture: {
      const auto captureIndex = cast<BeginCaptureIns>(*inst).getCaptureIndex();
      inst += sizeof(BeginCaptureIns);
      this->tryToCollectCaptureUsage(captureUsage);
      if (this->resolveAnchor(inst, end, prefilter, captureUsage)) {
        if (!captureUsage[captureIndex]) { // capture is not referenced
          return true;
        }
      }
      break;
    }
    case OpCode::ResetCaptures:
      inst += sizeof(ResetCapturesIns);
      continue;
    case OpCode::BeginLoop: {
      auto &ins = cast<BeginLoopIns>(*inst);
      auto subBegin = inst;
      subBegin += sizeof(BeginLoopIns); // due to suppress code analysis warning
      const auto subEnd = this->instSeq.begin() + ins.getOuter();
      auto next = subBegin;
      next += sizeOfInst(next);
      next += sizeof(EndLoopIns); // suppress code analysis warning
      if (ins.getMin() == 0 && ins.isUnlimited() && next == subEnd) {
        if (subBegin->op == OpCode::Any && noneOr(prefilter, Prefilter::Type::START)) {
          prefilter = Prefilter::start(); // (?s:.)* or (?s:.)*?
          return true;
        }
        if (subBegin->op == OpCode::AnyExceptNL && noneOr(prefilter, Prefilter::Type::LINE_START)) {
          prefilter = Prefilter::lineStart(); // .* or .*?
          return true;
        }
      } else if (ins.getMin() > 0) {
        return this->resolveAnchor(subBegin, subEnd, prefilter, captureUsage);
      }
      break;
    }
    }
    break;
  }
  return false;
}

void PrefilterBuilder::tryToCollectCaptureUsage(std::vector<bool> &usage) const {
  if (!usage.empty()) {
    return;
  }
  usage.resize(this->captureGroupCount + 1, false);
  for (auto *inst = this->instSeq.begin(); inst != this->instSeq.end(); inst += sizeOfInst(inst)) {
    unsigned int refIndex;
    bool named;
    switch (inst->op) {
    case OpCode::BackRef:
      refIndex = cast<BackRefIns>(*inst).getRefIndex();
      named = cast<BackRefIns>(*inst).named;
      break;
    case OpCode::IBackRef:
      refIndex = cast<IBackRefIns>(*inst).getRefIndex();
      named = cast<IBackRefIns>(*inst).named;
      break;
    case OpCode::LBBackRef:
      refIndex = cast<LBBackRefIns>(*inst).getRefIndex();
      named = cast<LBBackRefIns>(*inst).named;
      break;
    default:
      continue;
    }

    // backref
    if (named) {
      if (auto *entry = this->namedGroups.findByIndex(refIndex)) {
        if (entry->hasMultipleIndices()) {
          for (unsigned int i = 0; i < entry->getSize(); i++) {
            usage[(*entry)[i]] = true;
          }
        } else {
          usage[entry->getIndex()] = true;
        }
      }
    } else {
      usage[refIndex] = true;
    }
  }
}

FirstBitSet createFirstBitSet(const ArrayRef<Inst> instSeq, const ArrayRef<Matcher> matchers) {
  if (FirstBytesCollector collector(instSeq, matchers); collector()) {
    return collector.set;
  }
  return {};
}

Prefilter createPrefilter(const ArrayRef<Inst> instSeq, const ArrayRef<Matcher> matchers,
                          const unsigned int captureGroupCount, const NamedCaptureGroups &named) {
  PrefilterBuilder builder(instSeq, matchers, captureGroupCount, named);
  return builder();
}

} // namespace arsh::regex