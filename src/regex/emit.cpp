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
#include "unicode/case_fold.h"

namespace arsh::regex {

// ###########################
// ##     StrSetBuilder     ##
// ###########################

void StrSetBuilder::add(StringRef ref) {
  if (!this->inEmoji(ref)) {
    this->radix.add(ref, 1);
  }
}

void StrSetBuilder::add(StrSetBuilder &&builder) {
  this->add(builder.emoji);
  if (builder.emptySeq) {
    this->emptySeq = true;
  }
  this->codePoints.add(builder.codePoints);
  builder.radix.iterate([this](StringRef ref, unsigned char p) {
    if (!this->inEmoji(ref)) {
      this->radix.add(ref, p);
    }
    return true;
  });
}

template <typename Func>
constexpr bool cp_iter_requirement_v =
    std::is_same_v<bool, std::invoke_result_t<Func, StringRef, int>>;

template <typename Func, enable_when<cp_iter_requirement_v<Func>> = nullptr>
static void iterateCodes(CodePointSetBuilder &builder, Func func) {
  for (auto [first, last] : builder.toCompactArrayRef()) {
    for (; first <= last; first++) {
      char buf[4];
      unsigned int len = UnicodeUtil::codePointToUtf8(first, buf);
      assert(len);
      if (!func(StringRef(buf, len), first)) {
        return;
      }
    }
  }
}

template <typename Func>
constexpr bool str_iter_requirement_v = std::is_same_v<bool, std::invoke_result_t<Func, StringRef>>;

template <typename Func, enable_when<str_iter_requirement_v<Func>> = nullptr>
static void iterateEmoji(const ucp::RGIEmojiSeq emoji, Func func) {
  if (StrSetBuilder::hasEmoji(emoji)) {
    ucp::getEmojiTrie().iterate([&](StringRef ref, unsigned char p) {
      if (hasFlag(toUnderlying(emoji), p)) {
        if (!func(ref)) {
          return false;
        }
      }
      return true;
    });
  }
}

static void mergeEmoji(ucp::RGIEmojiSeq &emoji, RadixTree &radixTree) {
  iterateEmoji(emoji, [&radixTree](StringRef ref) {
    radixTree.add(ref, 1);
    return true;
  });
  emoji = ucp::RGIEmojiSeq::None;
}

static bool findFromEmoji(const ucp::RGIEmojiSeq emoji, const StringRef ref) {
  auto p = ucp::getEmojiTrie().find(ref);
  return p && hasFlag(toUnderlying(emoji), p);
}

static Optional<int> tryToCodePoint(const StringRef ref) {
  int codePoint = -1;
  const char *iter = ref.begin();
  if (unsigned int len = UnicodeUtil::wtf8ToCodePoint(iter, ref.end(), codePoint);
      len && iter + len == ref.end()) {
    return codePoint;
  }
  return {};
}

void StrSetBuilder::intersect(StrSetBuilder &&builder) {
  StrSetBuilder newBuilder(hasFlag(this->emoji, ucp::RGIEmojiSeq::CASE_IGNORE));
  newBuilder.emptySeq = this->emptySeq && builder.emptySeq;

  // emoji
  newBuilder.emoji = this->emoji & builder.emoji;
  if (hasFlag(this->emoji, ucp::RGIEmojiSeq::CASE_IGNORE)) {
    setFlag(newBuilder.emoji, ucp::RGIEmojiSeq::CASE_IGNORE);
  }
  if (this->hasEmoji()) {
    builder.radix.iterate([&](StringRef ref, unsigned char) {
      if (findFromEmoji(this->emoji, ref)) {
        newBuilder.radix.add(ref, 1);
      }
      return true;
    });
    iterateCodes(builder.codePoints, [&](StringRef ref, int codePoint) {
      if (findFromEmoji(this->emoji, ref)) {
        newBuilder.codePoints.addRange(codePoint, codePoint);
      }
      return true;
    });
  }

  // radix
  if (!this->radix.empty()) {
    iterateEmoji(builder.emoji, [&](StringRef ref) {
      if (this->radix.find(ref)) {
        newBuilder.radix.add(ref, 1);
      }
      return true;
    });
    builder.radix.iterate([&](StringRef ref, unsigned char) {
      if (this->radix.find(ref)) {
        newBuilder.radix.add(ref, 1);
      }
      return true;
    });
    iterateCodes(builder.codePoints, [&](StringRef ref, int codePoint) {
      if (this->radix.find(ref)) {
        newBuilder.codePoints.addRange(codePoint, codePoint);
      }
      return true;
    });
  }

  // codepoints
  if (this->codePoints) {
    auto set = this->codePoints.build();
    iterateEmoji(builder.emoji, [&](StringRef ref) {
      if (auto cp = tryToCodePoint(ref); cp.hasValue() && set.ref().contains(cp.unwrap())) {
        newBuilder.codePoints.addRange(cp.unwrap(), cp.unwrap());
      }
      return true;
    });
    builder.radix.iterate([&](StringRef ref, unsigned char) {
      if (auto cp = tryToCodePoint(ref); cp.hasValue() && set.ref().contains(cp.unwrap())) {
        newBuilder.codePoints.addRange(cp.unwrap(), cp.unwrap());
      }
      return true;
    });
    if (builder.codePoints) {
      this->codePoints.intersect(builder.codePoints);
      newBuilder.codePoints.add(this->codePoints);
    }
  }

  *this = std::move(newBuilder);
}

void StrSetBuilder::sub(StrSetBuilder &&builder) {
  if (this->emptySeq == builder.emptySeq && this->emptySeq) {
    this->emptySeq = false;
  }

  // emoji
  const bool ignoreCase = hasFlag(this->emoji, ucp::RGIEmojiSeq::CASE_IGNORE);
  unsetFlag(this->emoji, builder.emoji);
  if (ignoreCase) {
    setFlag(this->emoji, ucp::RGIEmojiSeq::CASE_IGNORE);
  }
  if (this->hasEmoji()) {
    builder.radix.iterate([&](StringRef ref, unsigned char) {
      if (findFromEmoji(this->emoji, ref)) {
        mergeEmoji(this->emoji, this->radix);
        return false;
      }
      return true;
    });
  }
  if (this->hasEmoji()) {
    iterateCodes(builder.codePoints, [&](StringRef ref, int) {
      if (findFromEmoji(this->emoji, ref)) {
        mergeEmoji(this->emoji, this->radix);
        return false;
      }
      return true;
    });
  }

  // radix
  if (!this->radix.empty()) {
    iterateEmoji(builder.emoji, [&](StringRef ref) {
      this->radix.remove(ref);
      return !this->radix.empty();
    });
  }
  if (!this->radix.empty()) {
    builder.radix.iterate([&](StringRef ref, unsigned char) {
      this->radix.remove(ref);
      return !this->radix.empty();
    });
  }
  if (!this->radix.empty()) {
    iterateCodes(builder.codePoints, [&](StringRef ref, int) {
      this->radix.remove(ref);
      return !this->radix.empty();
    });
  }

  // codepoint
  CodePointSetBuilder sub;
  if (this->codePoints) {
    iterateEmoji(builder.emoji, [&](StringRef ref) {
      if (auto cp = tryToCodePoint(ref); cp.hasValue()) {
        sub.addRange(cp.unwrap(), cp.unwrap());
      }
      return true;
    });
    this->codePoints.sub(sub);
  }
  if (this->codePoints) {
    sub.clear();
    builder.radix.iterate([&](StringRef ref, unsigned char) {
      if (auto cp = tryToCodePoint(ref); cp.hasValue()) {
        sub.addRange(cp.unwrap(), cp.unwrap());
      }
      return true;
    });
    this->codePoints.sub(sub);
  }
  if (this->codePoints) {
    this->codePoints.sub(builder.codePoints);
  }
}

// #####################
// ##     CodeGen     ##
// #####################

static void normalizeStrCharClass(Node &node) {
  if (isa<NestedNode>(node)) {
    normalizeStrCharClass(*cast<NestedNode>(node).getPattern());
    return;
  }
  if (isa<ListNode>(node) && !isa<CharClassNode>(node)) {
    for (auto &e : cast<ListNode>(node).getPatterns()) {
      normalizeStrCharClass(*e);
    }
    return;
  }
  if (!isa<CharClassNode>(node)) {
    return;
  }
  auto &classNode = cast<CharClassNode>(node);
  if (!classNode.mayContainStrings() || classNode.getType() == CharClassNode::Type::INTERSECT ||
      classNode.getType() == CharClassNode::Type::SUBTRACT) {
    return;
  }

  /*
   * move string class to head (emoji, string literal, code points)
   * [A-B\w\p{Basic_Emoji}\q{12|@}\p{RGI_Emoji}あ]
   * => [\p{Basic_Emoji}\p{RGI_Emoji}\q{12|@}A-B\wあ]
   */
  std::stable_sort(
      classNode.refChars().begin(), classNode.refChars().end(),
      [](const std::unique_ptr<Node> &x, const std::unique_ptr<Node> &y) {
        unsigned int xv = isProperty(*x, PropertyNode::Type::EMOJI) ? 0 : isa<AltNode>(*x) ? 1 : 2;
        unsigned int yv = isProperty(*y, PropertyNode::Type::EMOJI) ? 0 : isa<AltNode>(*y) ? 1 : 2;
        return xv < yv;
      });

  for (auto &e : classNode.refChars()) {
    if (isa<CharClassNode>(*e)) {
      normalizeStrCharClass(*e);
    }
  }
}

static void optimize(Node &node) { normalizeStrCharClass(node); }

Optional<Regex> CodeGen::operator()(SyntaxTree &&tree) {
  // prepare
  this->mode = tree.getFlag().mode();
  this->err.clear();
  this->modifierStack.clear();
  this->modifierStack.push(tree.getFlag().modifiers());
  this->directions.clear();
  this->directions.push_back(true);
  this->namedCaptureGroups = makeObserver(tree.getNamedCaptureGroups());
  this->resolvedCaptures.clear();
  this->resolvedCaptures.resize(tree.getCaptureGroupCount() + 1, CaptureState::NOT_OPENED);

  optimize(*tree.getPattern());
  if (!this->generate(*tree.getPattern())) {
    return {};
  }

  // finalize
  this->builder.emit<MatchIns>();
  auto flag = tree.getFlag();
  auto count = tree.getCaptureGroupCount();
  auto loopCount = tree.getLoopCount();
  return Regex(flag, loopCount, std::move(this->builder).build(), std::move(this->matchers),
               std::move(tree).takeNamedCaptureGroups(), count);
}

int CodeGen::mayBeSimpleCaseFolding(int codePoint) const {
  if (this->needSimpleCaseFolding()) {
    return doSimpleCaseFolding(codePoint);
  }
  return codePoint;
}

void CodeGen::mayBeSimpleCaseFolding(CodePointSetBuilder &setBuilder) const {
  if (this->needSimpleCaseFolding()) {
    setBuilder.foldCase();
  }
}

void CodeGen::complement(CodePointSetBuilder &setBuilder) const {
  if (this->needSimpleCaseFolding()) {
    CodePointSetBuilder newBuilder;
    newBuilder.addRange(0, UnicodeUtil::CODE_POINT_MAX);
    newBuilder.foldCase();
    newBuilder.sub(setBuilder);
    setBuilder = std::move(newBuilder);
  } else {
    setBuilder.complement();
  }
}

#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (!(E)) {                                                                                    \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

bool CodeGen::generate(const Node &node) {
  switch (node.getKind()) {
  case NodeKind::Empty:
    this->builder.emit<NopIns>();
    break;
  case NodeKind::Any:
    if (this->inLookBehind()) {
      this->builder.emit<LBAnyIns>(this->has(Modifier::DOT_ALL));
    } else if (this->has(Modifier::DOT_ALL)) {
      this->builder.emit<AnyIns>();
    } else {
      this->builder.emit<AnyExceptNLIns>();
    }
    break;
  case NodeKind::Char:
    this->emitCharIns(cast<CharNode>(node).getCodePoint());
    break;
  case NodeKind::CharClass:
    return this->generateCharClass(cast<CharClassNode>(node));
  case NodeKind::Property:
    return this->generateProperty(cast<PropertyNode>(node));
  case NodeKind::Boundary: {
    auto t = cast<BoundaryNode>(node).getType();
    switch (t) {
    case BoundaryNode::Type::START:
      this->builder.emit<StartIns>(this->has(Modifier::MULTILINE));
      return true;
    case BoundaryNode::Type::END:
      this->builder.emit<EndIns>(this->has(Modifier::MULTILINE));
      return true;
    case BoundaryNode::Type::WORD:
    case BoundaryNode::Type::NOT_WORD:
      if (this->has(Modifier::IGNORE_CASE)) {
        this->builder.emit<IWordIns>(t == BoundaryNode::Type::NOT_WORD);
      } else {
        this->builder.emit<WordIns>(t == BoundaryNode::Type::NOT_WORD);
      }
      return true;
    }
    break;
  }
  case NodeKind::BackRef:
    this->generateBackRef(cast<BackRefNode>(node));
    return true;
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
  }
  return true;
}

bool CodeGen::generateSeq(const SeqNode &node) {
  if (this->direction()) {
    const auto end = node.getPatterns().end();
    for (auto iter = node.getPatterns().begin(); iter != end;) {
      if (!this->has(Modifier::IGNORE_CASE) && isa<CharNode>(**iter) && iter + 1 != end &&
          isa<CharNode>(**(iter + 1))) {
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
      if (!this->has(Modifier::IGNORE_CASE) && isa<CharNode>(**iter) && iter + 1 != end &&
          isa<CharNode>(**(iter + 1))) {
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
    this->resolvedCaptures[node.getGroupIndex()] = CaptureState::NOT_CLOSED;
    TRY(this->generate(*node.getPattern()));
    this->resolvedCaptures[node.getGroupIndex()] = CaptureState::CLOSED;
    if (this->inLookBehind()) {
      this->builder.emit<LBEndCaptureIns>(node.getGroupIndex());
    } else {
      this->builder.emit<EndCaptureIns>(node.getGroupIndex());
    }
    return true;
  case GroupNode::Type::NON_CAPTURE:
    return this->generate(*node.getPattern());
  case GroupNode::Type::MODIFIER: {
    auto newModifier = this->modifiers();
    if (auto set = node.getSetModifiers(); set != Modifier::NONE) {
      setFlag(newModifier, set);
    }
    if (auto unset = node.getUnsetModifiers(); unset != Modifier::NONE) {
      unsetFlag(newModifier, unset);
    }
    this->modifierStack.push(newModifier);
    TRY(this->generate(*node.getPattern()));
    this->modifierStack.pop();
    return true;
  }
  }
  return true;
}

template <typename Func>
constexpr bool range_walker_requirement_v =
    std::is_same_v<void, std::invoke_result_t<Func, int, int>>;

template <typename Func, enable_when<range_walker_requirement_v<Func>> = nullptr>
static bool intoCharRange(const CharClassNode &node, unsigned int offset, Func func) {
  if (node.getType() == CharClassNode::Type::RANGE && offset + 2 < node.getChars().size()) {
    auto &n1 = *node.getChars()[offset];
    auto &n2 = *node.getChars()[offset + 1];
    auto &n3 = *node.getChars()[offset + 2];
    if (isa<CharNode>(n1) && isProperty(n2, PropertyNode::Type::RANGE) && isa<CharNode>(n3)) {
      int first = cast<CharNode>(n1).getCodePoint();
      int last = cast<CharNode>(n3).getCodePoint();
      func(first, last);
      return true;
    }
  }
  return false;
}

void CodeGen::toCodePointSet(ucp::BuilderOrSet builderOrSet, const PropertyNode &node) const {
  switch (node.getNormalizedType()) {
  case PropertyNode::Type::RANGE:
  case PropertyNode::Type::INTERSECT:
  case PropertyNode::Type::SUBTRACT:
  case PropertyNode::Type::NOT_DIGIT:
  case PropertyNode::Type::NOT_WORD:
  case PropertyNode::Type::NOT_SPACE:
  case PropertyNode::Type::NOT_UNICODE:
  case PropertyNode::Type::EMOJI:
  case PropertyNode::Type::GRAPHEME:
    assert(false);
    break; // normally unreachable
  case PropertyNode::Type::DIGIT:
    ucp::getPropertySet(ucp::Property::lone(ucp::Lone::ESRegexClassDigit), builderOrSet);
    break;
  case PropertyNode::Type::WORD: {
    const auto p = this->has(Modifier::IGNORE_CASE) ? ucp::Lone::ESRegexClassExtendWord
                                                    : ucp::Lone::ESRegexClassWord;
    ucp::getPropertySet(ucp::Property::lone(p), builderOrSet);
    break;
  }
  case PropertyNode::Type::SPACE:
    ucp::getPropertySet(ucp::Property::lone(ucp::Lone::ESRegexClassSpace), builderOrSet);
    break;
  case PropertyNode::Type::UNICODE:
    ucp::getPropertySet(node.getUCP(), builderOrSet);
    break;
  }
}

static AsciiSet foldCase(const AsciiSet set) {
  AsciiSet newSet;
  for (int i = 0; i < 128; i++) {
    int code = i;
    if (set.contains(code)) {
      code = doSimpleCaseFolding(code);
      assert(code >= 0 && code < 128);
      newSet.add(code);
    }
  }
  return newSet;
}

bool CodeGen::generateProperty(const PropertyNode &node) {
  if (node.onlyAscii()) {
    AsciiSet set;
    if (node.getType() == PropertyNode::Type::DIGIT) {
      for (char i = 0; i < 10; i++) {
        set.add('0' + i);
      }
    } else {
      assert(node.getType() == PropertyNode::Type::WORD);
      for (char i = 0; i < 10; i++) {
        set.add('0' + i);
      }
      for (char i = 'a'; i <= 'z'; i++) {
        set.add(i);
        set.add('A' + (i - 'a'));
      }
      set.add('_');
    }
    if (this->has(Modifier::IGNORE_CASE)) {
      set = foldCase(set);
    }
    if (auto index = this->emitMatcher(Matcher(set)); index.hasValue()) {
      this->emitCharSetIns(index.unwrap(), false);
      return true;
    }
  } else if (node.mayContainString()) {
    if (node.getNormalizedType() == PropertyNode::Type::GRAPHEME) {
      this->builder.emit<GraphemeIns>();
      return true;
    }
    assert(node.getNormalizedType() == PropertyNode::Type::EMOJI);
    auto emoji = node.getEmojiSeq();
    if (this->has(Modifier::IGNORE_CASE)) {
      setFlag(emoji, ucp::RGIEmojiSeq::CASE_IGNORE);
    }
    if (this->inLookBehind()) {
      this->builder.emit<PrepareLBRadixIns>();
      this->builder.emit<LBRadixOrEmojiIns>(emoji, false, 0, 0);
    } else {
      this->builder.emit<PrepareRadixIns>();
      this->builder.emit<RadixOrEmojiIns>(emoji, false, 0, 0);
    }
    return true;
  } else {
    CodePointSet set;
    CodePointSetBuilder setBuilder;
    const bool useBuilder = node.isInvert() || this->has(Modifier::IGNORE_CASE);
    this->toCodePointSet(useBuilder ? ucp::BuilderOrSet(setBuilder) : ucp::BuilderOrSet(set), node);
    if (useBuilder) {
      this->mayBeSimpleCaseFolding(setBuilder);
      if (node.isInvert()) {
        this->complement(setBuilder);
      }
      if (this->has(Modifier::IGNORE_CASE)) {
        setBuilder.foldCase();
      }
      set = setBuilder.build();
    }
    if (auto index = this->emitMatcher(Matcher(std::move(set))); index.hasValue()) {
      this->emitCharSetIns(index.unwrap(), false);
      return true;
    }
  }
  return false;
}

void CodeGen::generateStrSet(StrSetBuilder &setBuilder, const unsigned int level,
                             const Node &node) const {
  switch (node.getKind()) {
  case NodeKind::Char: {
    int codePoint = cast<CharNode>(node).getCodePoint();
    codePoint = this->mayBeSimpleCaseFolding(codePoint);
    setBuilder.add(codePoint);
    break;
  }
  case NodeKind::Property: {
    auto &p = cast<PropertyNode>(node);
    if (p.mayContainString()) {
      assert(p.getNormalizedType() == PropertyNode::Type::EMOJI);
      auto emoji = p.getEmojiSeq();
      if (this->needSimpleCaseFolding()) {
        setFlag(emoji, ucp::RGIEmojiSeq::CASE_IGNORE);
      }
      setBuilder.add(emoji);
      break;
    }

    const bool useSubBuilder = p.isInvert() || this->has(Modifier::IGNORE_CASE);
    StrSetBuilder subBuilder(this->has(Modifier::IGNORE_CASE));
    StrSetBuilder *builderPtr = useSubBuilder ? &subBuilder : &setBuilder;
    this->toCodePointSet(ucp::BuilderOrSet(builderPtr->codePoints), p);
    if (useSubBuilder) {
      this->mayBeSimpleCaseFolding(subBuilder.codePoints);
      if (p.isInvert()) {
        this->complement(subBuilder.codePoints);
      }
      setBuilder.codePoints.add(subBuilder.codePoints);
    }
    break;
  }
  case NodeKind::CharClass: {
    auto &classNode = cast<CharClassNode>(node);
    const unsigned int size = classNode.getChars().size();
    StrSetBuilder classBuilder(this->has(Modifier::IGNORE_CASE));
    StrSetBuilder *builderPtr = level ? &classBuilder : &setBuilder;
    switch (classNode.getType()) {
    case CharClassNode::Type::UNION:
    case CharClassNode::Type::RANGE: {
      for (unsigned int i = 0; i < size; i++) {
        auto rangeConsumer = [&builderPtr, this](int first, int last) {
          builderPtr->addRange(first, last, this->needSimpleCaseFolding());
        };
        if (intoCharRange(classNode, i, rangeConsumer)) {
          i += 2;
        } else {
          this->generateStrSet(*builderPtr, level + 1, *classNode.getChars()[i]);
        }
      }
      break;
    }
    case CharClassNode::Type::INTERSECT:
    case CharClassNode::Type::SUBTRACT: {
      generateStrSet(*builderPtr, level + 1, *classNode.getChars()[0]);
      for (unsigned int i = 1; i < size; i++) {
        auto &e = *classNode.getChars()[i];
        if (isProperty(e, PropertyNode::Type::INTERSECT) ||
            isProperty(e, PropertyNode::Type::SUBTRACT)) {
          continue;
        }
        StrSetBuilder sub(this->has(Modifier::IGNORE_CASE));
        this->generateStrSet(sub, 0, e);
        if (classNode.getType() == CharClassNode::Type::INTERSECT) {
          builderPtr->intersect(std::move(sub));
        } else {
          assert(classNode.getType() == CharClassNode::Type::SUBTRACT);
          builderPtr->sub(std::move(sub));
        }
      }
      break;
    }
    }
    if (classNode.isInvert() && this->mode == Mode::UNICODE_SET) {
      this->complement(builderPtr->codePoints);
    }
    if (level) {
      setBuilder.add(std::move(classBuilder));
    }
    break;
  }
  case NodeKind::Alt: { // for \q{}, \q{A}, \q{A|B}, \q{AA|B}
    auto &altNode = cast<AltNode>(node);
    std::string buf;
    for (auto &e : altNode.getPatterns()) {
      if (isa<CharNode>(*e)) {
        this->generateStrSet(setBuilder, level + 1, *e);
        continue;
      }
      if (isa<EmptyNode>(*e)) {
        setBuilder.emptySeq = true;
        continue;
      }
      assert(isa<SeqNode>(*e));
      auto &seqNode = cast<SeqNode>(*e);
      assert(seqNode.getPatterns().size() > 1);
      buf.clear();
      for (auto &c : seqNode.getPatterns()) {
        assert(isa<CharNode>(*c));
        int codePoint = this->mayBeSimpleCaseFolding(cast<CharNode>(*c).getCodePoint());
        char data[4];
        if (unsigned int len = UnicodeUtil::codePointToUtf8(codePoint, data)) {
          buf.append(data, len);
        }
      }
      setBuilder.add(buf);
    }
    break;
  }
  default:
    assert(false); // unreachable
    break;
  }
}

static bool isSingleCharClass(const CharClassNode &node) {
  return node.getChars().size() == 1 && !node.isInvert() && !isa<AltNode>(*node.getChars()[0]);
}

bool CodeGen::generateCharClass(const CharClassNode &node) {
  if (isSingleCharClass(node)) {
    return this->generate(*node.getChars()[0]);
  }

  // generate str set or char set
  StrSetBuilder setBuilder(this->has(Modifier::IGNORE_CASE));
  this->generateStrSet(setBuilder, 0, node);
  bool invert = false;
  if (node.isInvert() && this->mode != Mode::UNICODE_SET) {
    invert = true;
  }
  if (this->has(Modifier::IGNORE_CASE)) {
    setBuilder.codePoints.foldCase();
  }
  if (setBuilder.radix.empty() && !setBuilder.hasEmoji()) { // for charset or empty seq (\q{})
    if (setBuilder.emptySeq && !setBuilder.codePoints) {
      return true; // do nothing
    }
    Optional<InstructionBuilder::ReservedPoint> point;
    if (setBuilder.emptySeq) { // [\w\q{}] => (?:[\w] | )
      point = this->builder.emitReservedPoint<AltIns>();
    }
    if (this->tryToEmitCharSetIns(setBuilder.codePoints, invert)) {
      if (point.hasValue()) {
        unsigned int addr = this->builder.currentAddr();
        this->builder.emitAt<AltIns>(point.unwrap(), addr);
      }
      return true;
    }
  } else {
    // prepare
    assert(node.mayContainStrings());
    auto emoji = setBuilder.emoji;
    if (this->has(Modifier::IGNORE_CASE)) {
      setFlag(emoji, ucp::RGIEmojiSeq::CASE_IGNORE);
    }
    Optional<unsigned short> radixIndex;
    if (!setBuilder.radix.empty()) {
      FlexBuffer<uint8_t> buf;
      if (!serialize(setBuilder.radix, buf)) {
        this->err = "too large string set";
        return false;
      }
      radixIndex = this->emitMatcher(Matcher(std::move(buf), setBuilder.radix.maxCodePointCount()));
      if (!radixIndex.hasValue()) {
        return false;
      }
    }

    // emit codes
    Optional<InstructionBuilder::ReservedPoint> point;
    if (setBuilder.emptySeq) { // [\w\q{}] => (?:[\w] | )
      point = this->builder.emitReservedPoint<AltIns>();
    }
    if (this->inLookBehind()) {
      this->builder.emit<PrepareLBRadixIns>();
    } else {
      this->builder.emit<PrepareRadixIns>();
    }
    auto reservedPoint = this->inLookBehind() ? this->builder.emitReservedPoint<LBRadixOrEmojiIns>()
                                              : this->builder.emitReservedPoint<RadixOrEmojiIns>();
    const unsigned int oldAddr = this->builder.currentAddr();
    if (setBuilder.codePoints) {
      if (!this->tryToEmitCharSetIns(setBuilder.codePoints, invert)) {
        return false;
      }
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
    if (point.hasValue()) {
      unsigned int addr = this->builder.currentAddr();
      this->builder.emitAt<AltIns>(point.unwrap(), addr);
    }
    return true;
  }
  return false;
}

void CodeGen::emitCharIns(int codePoint) {
  if (this->inLookBehind()) {
    if (this->has(Modifier::IGNORE_CASE)) {
      codePoint = doSimpleCaseFolding(codePoint);
    }
    this->builder.emit<LBCharIns>(codePoint, this->has(Modifier::IGNORE_CASE));
  } else if (this->has(Modifier::IGNORE_CASE)) {
    this->builder.emit<ICharIns>(doSimpleCaseFolding(codePoint));
  } else {
    this->builder.emit<CharIns>(codePoint);
  }
}

static Optional<AsciiSet> tryToGenerateAsciiSet(CodePointSetBuilder &setBuilder) {
  AsciiSet set;
  for (auto [first, last] : setBuilder.toCompactArrayRef()) {
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

bool CodeGen::tryToEmitCharSetIns(CodePointSetBuilder &setBuilder, bool invert) {
  const auto ranges = setBuilder.toCompactArrayRef();
  if (ranges.empty()) {
    if (invert) {
      if (this->inLookBehind()) {
        this->builder.emit<LBAnyIns>(true);
      } else {
        this->builder.emit<AnyIns>();
      }
      return true;
    }
  }
  if (ranges.size() == 1 && !invert) {
    auto [first, last] = ranges[0];
    if (first == last) { // only contains single code point
      this->emitCharIns(first);
      return true;
    }
    if (first == 0 && last == UnicodeUtil::CODE_POINT_MAX) { // Any
      if (this->inLookBehind()) {
        this->builder.emit<LBAnyIns>(true);
      } else {
        this->builder.emit<AnyIns>();
      }
      return true;
    }
  }

  if (auto set = tryToGenerateAsciiSet(setBuilder); set.hasValue()) {
    if (auto index = this->emitMatcher(Matcher(set.unwrap())); index.hasValue()) {
      this->emitCharSetIns(index.unwrap(), invert);
      return true;
    }
    return false;
  }

  if (auto index = this->emitMatcher(Matcher(setBuilder.build())); index.hasValue()) {
    this->emitCharSetIns(index.unwrap(), invert);
    return true;
  }
  return false;
}

void CodeGen::generateBackRef(const BackRefNode &node) {
  if (node.isNamed()) {
    auto *entry = this->namedCaptureGroups->find(node.getName());
    assert(entry);
    if (entry->hasMultipleIndices()) {
      unsigned int closedCount = 0;
      for (unsigned int i = 0; i < entry->getSize(); i++) {
        auto state = this->resolvedCaptures[(*entry)[i]];
        if (state == CaptureState::CLOSED) {
          closedCount++;
        } else if (state == CaptureState::NOT_CLOSED) {
          return; // refer to its own group, ex. (?:(?<A>.)|(?<A>\k<A>))
        }
      }
      if (!closedCount) {
        return;
      }
    } else if (this->resolvedCaptures[entry->getIndex()] != CaptureState::CLOSED) {
      return; // invalid capture, do nothing
    }
  } else if (this->resolvedCaptures[node.getIndex()] != CaptureState::CLOSED) {
    return; // invalid capture, do nothing
  }

  if (this->inLookBehind()) {
    this->builder.emit<LBBackRefIns>(node.getIndex(), node.isNamed(),
                                     this->has(Modifier::IGNORE_CASE));
  } else if (this->has(Modifier::IGNORE_CASE)) {
    this->builder.emit<IBackRefIns>(node.getIndex(), node.isNamed());
  } else {
    this->builder.emit<BackRefIns>(node.getIndex(), node.isNamed());
  }
}

bool CodeGen::generateRepeat(const RepeatNode &node) {
  if (node.getMax() == 0) { // do nothing
    return true;
  }
  if (node.getMin() == 1 && node.getMax() == 1) {
    return this->generate(*node.getPattern());
  }
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