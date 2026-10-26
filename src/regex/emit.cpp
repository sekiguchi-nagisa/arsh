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

#include "emit.h"

#include "misc/unicode.hpp"
#include "transform.h"
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

static void fill(const RadixTree &radix, FirstBitSet &set, const bool fold) {
  auto walker = [&](StringRef ref, unsigned char) {
    int codePoint;
    UnicodeUtil::wtf8ToCodePoint(ref.begin(), ref.end(), codePoint);
    fill(codePoint, set, fold);
    return true;
  };
  radix.iterate(walker);
}

static void fill(const ucp::RGIEmojiSeq emoji, FirstBitSet &set, const bool fold) {
  auto walker = [&](StringRef ref, unsigned char p) {
    if (hasFlag(toUnderlying(emoji), p)) {
      int codePoint;
      UnicodeUtil::wtf8ToCodePoint(ref.begin(), ref.end(), codePoint);
      fill(codePoint, set, fold);
    }
    return true;
  };
  ucp::getEmojiTrie().iterate(walker);
}

static bool fillFirstBitSet(const Node &node, FirstBitSet &set) {
  switch (node.getKind()) {
  case NodeKind::Empty:
  case NodeKind::Any:
    break;
  case NodeKind::Char:
    fill(cast<CharNode>(node).getCodePoint(), set, false);
    return true;
  case NodeKind::CharClass:
  case NodeKind::Property:
  case NodeKind::Boundary:
  case NodeKind::BackRef:
    break;
  case NodeKind::Repeat:
    if (auto &n = cast<RepeatNode>(node); n.getMin() > 0) {
      return fillFirstBitSet(*n.getPattern(), set);
    }
    break;
  case NodeKind::Seq: {
    auto &patterns = cast<SeqNode>(node).getPatterns();
    const unsigned int size = patterns.size();
    unsigned int i = 0;
    for (; i < size; i++) {
      auto &n = *patterns[i];
      if (isa<LookAroundNode>(n)) {
        continue;
      }
      if (isa<BoundaryNode>(n)) {
        if (const auto t = cast<BoundaryNode>(n).getType();
            t == BoundaryNode::Type::WORD || t == BoundaryNode::Type::NOT_WORD) {
          continue;
        }
      }
      break;
    }
    return i < size && fillFirstBitSet(*patterns[i], set);
  }
  case NodeKind::Alt:
    if (auto &alt = cast<AltNode>(node); alt.getPatterns().size() > 1) {
      for (auto &e : alt.getPatterns()) {
        TRY(fillFirstBitSet(*e, set));
      }
      return true;
    }
    break;
  case NodeKind::LookAround:
    break;
  case NodeKind::Group:
    return fillFirstBitSet(*cast<GroupNode>(node).getPattern(), set);
  case NodeKind::FoldChar:
    fill(cast<FoldCharNode>(node).getCodePoint(), set, true);
    return true;
  case NodeKind::CharSet: {
    auto &n = cast<CharSetNode>(node);
    if (const auto ref = n.getSet().ref(); ref && !n.isInvert()) { // TODO: invert
      fill(ref, set, n.isIgnoreCase());
      return true;
    }
    break;
  }
  case NodeKind::StrSet: {
    auto &n = cast<StrSetNode>(node);
    if (n.hasEmoji()) {
      auto emoji = n.getEmojiSeq();
      if (n.isIgnoreCase()) {
        setFlag(emoji, ucp::RGIEmojiSeq::CASE_IGNORE);
      }
      fill(emoji, set, n.isIgnoreCase());
    }
    fill(n.getRadix(), set, n.isIgnoreCase());
    if (n.getChars()) {
      TRY(fillFirstBitSet(*n.getChars(), set));
    }
    return true;
  }
  }
  return false;
}

static FirstBitSet createFirstBitSet(const Node &node) {
  FirstBitSet set;
  if (fillFirstBitSet(node, set) && !set.all()) {
    return set;
  }
  return {};
}

// #####################
// ##     CodeGen     ##
// #####################

Optional<Regex> CodeGen::operator()(SyntaxTree &&tree) {
  // prepare
  this->err.clear();
  this->directions.clear();
  this->directions.push_back(true);

  transform(tree);
  const auto firstSet = createFirstBitSet(*tree.getPattern()); // create before code gen
  if (!this->generate(*tree.getPattern())) {
    return {};
  }

  // finalize
  this->builder.emit<MatchIns>();
  auto flag = tree.getFlag();
  auto count = tree.getCaptureGroupCount();
  auto loopCount = tree.getLoopCount();
  return Regex(flag, loopCount, std::move(this->builder).build(), firstSet,
               std::move(this->matchers), std::move(tree).takeNamedCaptureGroups(), count);
}

static Optional<AsciiSet> tryToGenerateAsciiSet(const CodePointSetRef ref) {
  if (ref.getSize() != ref.getBMPSize()) {
    return {};
  }
  AsciiSet set;
  for (auto &e : ref.getBMPRanges()) {
    int first = e.firstBMP();
    const int last = e.lastBMP();
    if (Matcher::withinAsciiSet(first) && Matcher::withinAsciiSet(last)) {
      for (; first <= last; first++) {
        set.add(first);
      }
      continue;
    }
    return {};
  }
  return set;
}

bool CodeGen::generate(Node &node) {
  switch (node.getKind()) {
  case NodeKind::Empty:
    break; // do nothing
  case NodeKind::Any:
    if (const bool dotAll = hasFlag(cast<AnyNode>(node).getModifier(), Modifier::DOT_ALL);
        this->inLookBehind()) {
      this->builder.emit<LBAnyIns>(dotAll);
    } else if (dotAll) {
      this->builder.emit<AnyIns>();
    } else {
      this->builder.emit<AnyExceptNLIns>();
    }
    break;
  case NodeKind::Char:
    if (auto &n = cast<CharNode>(node); this->inLookBehind()) {
      this->builder.emit<LBCharIns>(n.getCodePoint(), false);
    } else {
      this->builder.emit<CharIns>(n.getCodePoint());
    }
    break;
  case NodeKind::CharClass:
    fatal("unreachable");
  case NodeKind::Property:
    assert(cast<PropertyNode>(node).getNormalizedType() == PropertyNode::Type::GRAPHEME);
    this->builder.emit<GraphemeIns>();
    break;
  case NodeKind::Boundary: {
    auto &n = cast<BoundaryNode>(node);
    switch (const auto t = n.getType()) {
    case BoundaryNode::Type::START:
      this->builder.emit<StartIns>(hasFlag(n.getModifier(), Modifier::MULTILINE));
      break;
    case BoundaryNode::Type::END:
      this->builder.emit<EndIns>(hasFlag(n.getModifier(), Modifier::MULTILINE));
      break;
    case BoundaryNode::Type::WORD:
    case BoundaryNode::Type::NOT_WORD:
      if (hasFlag(n.getModifier(), Modifier::IGNORE_CASE)) {
        this->builder.emit<IWordIns>(t == BoundaryNode::Type::NOT_WORD);
      } else {
        this->builder.emit<WordIns>(t == BoundaryNode::Type::NOT_WORD);
      }
      break;
    }
    break;
  }
  case NodeKind::BackRef:
    if (auto &n = cast<BackRefNode>(node); this->inLookBehind()) {
      this->builder.emit<LBBackRefIns>(n.getIndex(), n.isNamed(), n.isIgnoreCase());
    } else if (n.isIgnoreCase()) {
      this->builder.emit<IBackRefIns>(n.getIndex(), n.isNamed());
    } else {
      this->builder.emit<BackRefIns>(n.getIndex(), n.isNamed());
    }
    break;
  case NodeKind::Repeat:
    return this->generateRepeat(cast<RepeatNode>(node));
  case NodeKind::Seq:
    return this->generateSeq(cast<SeqNode>(node));
  case NodeKind::Alt:
    return this->generateAlt(cast<AltNode>(node));
  case NodeKind::LookAround:
    return this->generateLookAround(cast<LookAroundNode>(node));
  case NodeKind::Group:
    return this->generateGroup(cast<GroupNode>(node));
  case NodeKind::FoldChar:
    if (auto &n = cast<FoldCharNode>(node); this->inLookBehind()) {
      this->builder.emit<LBCharIns>(n.getCodePoint(), true);
    } else {
      this->builder.emit<ICharIns>(n.getCodePoint());
    }
    break;
  case NodeKind::CharSet: {
    auto &n = cast<CharSetNode>(node);
    Optional<unsigned short> ret;
    if (auto set = tryToGenerateAsciiSet(n.getSet().ref()); set.hasValue()) {
      ret = this->emitMatcher(Matcher(set.unwrap()));
    } else {
      ret = this->emitMatcher(Matcher(n.take()));
    }
    if (ret.hasValue()) {
      unsigned short matcherIndex = ret.unwrap();
      if (this->inLookBehind()) {
        this->builder.emit<LBCharSetIns>(matcherIndex, n.isInvert(), n.isIgnoreCase());
      } else if (n.isIgnoreCase()) {
        this->builder.emit<ICharSetIns>(matcherIndex, n.isInvert());
      } else {
        this->builder.emit<CharSetIns>(matcherIndex, n.isInvert());
      }
      return true;
    }
    return false;
  }
  case NodeKind::StrSet:
    return this->generateStrSet(cast<StrSetNode>(node));
  }
  return true;
}

bool CodeGen::generateSeq(const SeqNode &node) {
  if (this->direction()) {
    const auto end = node.getPatterns().end();
    for (auto iter = node.getPatterns().begin(); iter != end;) {
      if (isa<CharNode>(**iter) && iter + 1 != end && isa<CharNode>(**(iter + 1))) {
        ByteBuffer buf;
        while (iter != end && isa<CharNode>(**iter)) {
          char data[4];
          unsigned int len =
              UnicodeUtil::codePointToUtf8(cast<CharNode>(**iter).getCodePoint(), data);
          buf.append(data, len);
          ++iter;
        }
        if (auto index = this->emitMatcher(Matcher(std::move(buf))); index.hasValue()) {
          this->builder.emit<StringIns>(index.unwrap());
        } else {
          return false;
        }
      } else {
        TRY(this->generate(**iter));
        ++iter;
      }
    }
  } else {
    const auto end = node.getPatterns().rend();
    for (auto iter = node.getPatterns().rbegin(); iter != end;) {
      if (isa<CharNode>(**iter) && iter + 1 != end && isa<CharNode>(**(iter + 1))) {
        auto last = iter;
        auto first = last;
        while (iter != end && isa<CharNode>(**iter)) {
          first = iter;
          ++iter;
        }
        ByteBuffer buf;
        while (first >= last) {
          char data[4];
          unsigned int len =
              UnicodeUtil::codePointToUtf8(cast<CharNode>(**first).getCodePoint(), data);
          buf.append(data, len);
          --first;
        }
        if (auto index = this->emitMatcher(Matcher(std::move(buf))); index.hasValue()) {
          this->builder.emit<LBStringIns>(index.unwrap());
        } else {
          return false;
        }
      } else {
        TRY(this->generate(**iter));
        ++iter;
      }
    }
  }
  return true;
}

bool CodeGen::generateAlt(const AltNode &node) {
  const unsigned int size = node.getPatterns().size();
  if (size == 1) {
    auto point = this->builder.emitReservedPoint<AltIns>();
    TRY(this->generate(*node.getPatterns()[0]));
    auto addr = this->builder.currentAddr();
    this->builder.emitAt<AltIns>(point, addr);
    return true;
  }
  std::vector<InstructionBuilder::ReservedPoint> jumps;
  for (unsigned int i = 0; i < size; i++) {
    if (i == size - 1) {
      TRY(this->generate(*node.getPatterns()[i]));
      continue;
    }
    auto point = this->builder.emitReservedPoint<AltIns>();
    TRY(this->generate(*node.getPatterns()[i]));
    jumps.push_back(this->builder.emitReservedPoint<JumpIns>());
    auto addr = this->builder.currentAddr();
    this->builder.emitAt<AltIns>(point, addr);
  }
  unsigned int addr = this->builder.currentAddr();
  for (auto &e : jumps) {
    this->builder.emitAt<JumpIns>(e, addr);
  }
  return true;
}

bool CodeGen::generateGroup(const GroupNode &node) {
  switch (node.getType()) {
  case GroupNode::Type::CAPTURE:
    assert(node.getGroupIndex());
    this->builder.emit<BeginCaptureIns>(node.getGroupIndex());
    TRY(this->generate(*node.getPattern()));
    if (this->inLookBehind()) {
      this->builder.emit<LBEndCaptureIns>(node.getGroupIndex());
    } else {
      this->builder.emit<EndCaptureIns>(node.getGroupIndex());
    }
    return true;
  case GroupNode::Type::NON_CAPTURE:
  case GroupNode::Type::MODIFIER:
    fatal("unreachable");
  }
  return true;
}

bool CodeGen::generateStrSet(const StrSetNode &node) {
  // prepare
  auto emoji = node.getEmojiSeq();
  if (node.isIgnoreCase()) {
    setFlag(emoji, ucp::RGIEmojiSeq::CASE_IGNORE);
  }
  Optional<unsigned short> radixIndex;
  if (!node.getRadix().empty()) {
    FlexBuffer<uint8_t> buf;
    if (!serialize(node.getRadix(), buf)) {
      this->err = "too large string set";
      return false;
    }
    radixIndex = this->emitMatcher(Matcher(std::move(buf), node.getRadix().maxCodePointCount()));
    if (!radixIndex.hasValue()) {
      return false;
    }
  }

  // emit codes
  if (this->inLookBehind()) {
    this->builder.emit<PrepareLBRadixIns>();
  } else {
    this->builder.emit<PrepareRadixIns>();
  }
  auto reservedPoint = this->inLookBehind() ? this->builder.emitReservedPoint<LBRadixOrEmojiIns>()
                                            : this->builder.emitReservedPoint<RadixOrEmojiIns>();
  const unsigned int oldAddr = this->builder.currentAddr();
  if (node.getChars()) {
    TRY(this->generate(*node.getChars()));
  }
  const unsigned int nextOffset = this->builder.currentAddr() - oldAddr;
  assert(nextOffset <= UINT8_MAX);
  const unsigned short index = radixIndex.hasValue() ? radixIndex.unwrap() : 0;
  if (this->inLookBehind()) {
    this->builder.emitAt<LBRadixOrEmojiIns>(reservedPoint, emoji, radixIndex.hasValue(), index,
                                            nextOffset);
  } else {
    this->builder.emitAt<RadixOrEmojiIns>(reservedPoint, emoji, radixIndex.hasValue(), index,
                                          nextOffset);
  }
  return true;
}

bool CodeGen::generateRepeat(const RepeatNode &node) {
  const unsigned int beginAddr = this->builder.currentAddr();
  const auto p = this->builder.emitReservedPoint<BeginLoopIns>();
  if (node.getFirstGroupIndex()) {
    assert(node.getFirstGroupIndex() <= node.getLastGroupIndex());
    this->builder.emit<ResetCapturesIns>(node.getFirstGroupIndex(), node.getLastGroupIndex());
  }
  TRY(this->generate(*node.getPattern()));
  this->builder.emit<EndLoopIns>(beginAddr);
  const unsigned int outerAddr = this->builder.currentAddr();
  this->builder.emitAt<BeginLoopIns>(p, node.getLoopIndex(), node.getMin(), node.getMax(),
                                     node.isGreedy(), outerAddr);
  return true;
}

bool CodeGen::generateLookAround(const LookAroundNode &node) {
  const auto p = this->builder.emitReservedPoint<BeginLookAroundIns>();
  this->directions.push_back(node.isLookAhead());
  TRY(this->generate(*node.getPattern()));
  this->directions.pop_back();
  const auto endAddr = this->builder.currentAddr();
  this->builder.emit<EndLookAroundIns>();
  this->builder.emitAt<BeginLookAroundIns>(p, endAddr, node.isNegate());
  return true;
}

Optional<unsigned short> CodeGen::emitMatcher(Matcher &&matcher) {
  unsigned int index = this->matchers.size();
  if (index > UINT16_MAX) {
    this->err += "number of matcher index reaches limit";
    return {};
  }
  this->matchers.emplace_back(std::move(matcher));
  return static_cast<unsigned short>(index);
}

} // namespace arsh::regex