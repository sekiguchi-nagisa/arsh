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
#include "instruction.h"
#include "matcher.h"
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
    case OpCode::BackRef:
    case OpCode::IBackRef:
    case OpCode::LBAny:
    case OpCode::LBChar:
    case OpCode::LBCharSet:
    case OpCode::PrepareLBRadix:
    case OpCode::LBRadixOrEmoji:
    case OpCode::LBString:
    case OpCode::LBEndCapture:
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

FirstBitSet createFirstBitSet(const ArrayRef<Inst> instSeq, const ArrayRef<Matcher> matchers) {
  if (FirstBytesCollector collector(instSeq, matchers); collector()) {
    return collector.set;
  }
  return {};
}

Prefilter createPrefilter(const ArrayRef<Inst> instSeq, const ArrayRef<Matcher> matchers) {
  if (FirstBytesCollector collector(instSeq, matchers); collector()) {
    return {collector.set, 0};
  }
  return {};
}

} // namespace arsh::regex